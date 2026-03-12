# umimmio Design

English | [日本語](design.ja.md)

## 1. Vision

`umimmio` is a type-safe, zero-cost memory-mapped I/O library for C++23:

1. Register maps are defined at compile time — no runtime discovery or parsing.
2. Bit-field access is type-safe: write-only registers cannot be read, and vice versa.
3. The same register map description works across direct MMIO, I2C, and SPI transports.
4. Transport selection is a template parameter, not runtime dispatch (no vtable).
5. Error handling is policy-based: assert, trap, ignore, or custom handler.

---

## 2. Non-Negotiable Requirements

### 2.1 Compile-Time Register Maps

All register addresses, field widths, access policies, and reset values are `constexpr`.
No runtime tables, no initialization step, no allocation.

### 2.2 Access Policy Enforcement

Reading a write-only register or writing a read-only register is a compile error.
Enforcement uses C++20 `requires` clauses on `read()`, `write()`, `modify()`, etc.
through `Readable`, `Writable`, `ReadWritable`, and `ReadableValue`/`WritableValue`/`ModifiableValue` concepts.

W1C (Write-1-to-Clear) fields have `WriteBehavior::ONE_TO_CLEAR` and are only accepted by `clear()`.
W1S (Write-1-to-Set) and W1T (Write-1-to-Toggle) fields are defined as WO ports for atomic bit manipulation.
`IsW1C`/`IsW1S`/`IsW1T` concepts identify them; `NormalWrite` excludes all non-NORMAL WriteBehaviors from `modify()` and `flip()`.

### 2.3 Transport Abstraction

Register operations are decoupled from bus protocol.
The same `Device/Block/Register/Field` hierarchy works with:

1. Direct volatile pointer (Cortex-M, RISC-V memory-mapped peripherals).
2. I2C bus.
3. SPI bus.

Transport is a template parameter, not a base-class pointer.

### 2.4 Range Checking

Field values are range-checked at compile time when possible:

1. `value()` with a literal exceeding field width triggers `detail::mmio_compile_time_error_value_out_of_range` via `if consteval`.
2. Runtime `DynamicValue` is not validated at `value()` creation time. Validation is performed by operations (`write()`/`modify()`/`is()`) according to `CheckPolicy` and `ErrorPolicy`.
3. `value()` requires `std::unsigned_integral` — signed values are a compile error.
4. `BitRegion` has 5 `static_assert`s validating offset, width, and register-width consistency.

### 2.5 Dependencies

`umimmio` depends only on C++23 standard library headers.
Tests depend on `umitest` for assertions.

---

## 3. Current Layout

```text
lib/umimmio/
├── README.md
├── xmake.lua
├── docs/
│   ├── design.md / design.ja.md
│   └── readme.ja.md
├── examples/
│   ├── minimal.cc
│   ├── register_map.cc
│   └── transport_mock.cc
├── include/umimmio/
│   ├── mmio.hh              # Umbrella header (excludes csr.hh)
│   ├── policy.hh            # Foundation: AccessPolicy, transport tags, error policies
│   ├── region.hh            # Data model: Device, Register, Field, Value, concepts,
│   │                        #   RegisterArray, dispatch, IndexedArray
│   ├── ops.hh               # Operations: RegOps, ByteAdapter
│   └── transport/
│       ├── atomic_direct.hh # AtomicDirectTransport (write-only alias, explicit include)
│       ├── csr.hh           # CsrTransport (RISC-V CSR, explicit include)
│       ├── detail.hh        # Shared helpers for address encoding
│       ├── direct.hh        # DirectTransport (volatile pointer)
│       ├── i2c.hh           # I2cTransport (HAL-based)
│       └── spi.hh           # SpiTransport (HAL-based)
└── tests/
    ├── testing.md / testing.ja.md   # Test documentation
    ├── test_main.cc
    ├── test_mock.hh             # MockTransport and shared device definitions
    ├── test_access_policy.hh    # W1C/W1S/W1T access policy tests
    ├── test_register_field.hh   # Field, RegisterArray, dispatch, IndexedArray tests
    ├── test_transport.hh        # Transport tests (Direct, I2C, CSR)
    ├── test_byte_transport.hh   # ByteAdapter tests
    ├── smoke/
    │   └── standalone.cc
    ├── compile_fail/            # 31 negative compile tests (glob-collected)
    │   ├── bits_non_numeric.cc
    │   ├── clear_non_w1c.cc
    │   ├── cross_register_write.cc
    │   ├── field_overflow.cc
    │   ├── flip_atomic_direct.cc  # AtomicDirectTransport flip rejected (no reg_read)
    │   ├── flip_multi_bit.cc
    │   ├── flip_ro.cc
    │   ├── flip_w1c.cc
    │   ├── flip_w1s.cc          # W1S rejected by NormalWrite
    │   ├── flip_w1t.cc          # W1T rejected by NormalWrite
    │   ├── flip_wo.cc
    │   ├── get_wrong_field.cc
    │   ├── indexed_array_oob.cc # IndexedArray::Entry out-of-range
    │   ├── modify_atomic_direct.cc # AtomicDirectTransport modify rejected (no reg_read)
    │   ├── modify_cross_register.cc
    │   ├── modify_w1c.cc
    │   ├── modify_w1s.cc        # W1S rejected by NormalWrite
    │   ├── modify_w1t.cc        # W1T rejected by NormalWrite
    │   ├── modify_wo.cc
    │   ├── read_atomic_direct.cc  # AtomicDirectTransport read rejected (no reg_read)
    │   ├── read_field_eq_int.cc
    │   ├── read_w1s.cc          # W1S not Readable
    │   ├── read_w1t.cc          # W1T not Readable
    │   ├── read_wo.cc
    │   ├── transport_tag_mismatch.cc
    │   ├── value_signed.cc
    │   ├── value_typesafe.cc
    │   ├── write_ro.cc
    │   ├── write_ro_csr.cc      # RO CSR write rejected via CsrTransport
    │   ├── write_ro_value.cc
    │   └── write_zero_args.cc
    └── xmake.lua
```

---

## 4. Usage

For type hierarchy, operations, transport selection, error handling, and concurrency,
see [README](../README.md) ([日本語](design.ja.md)).

---

## 5. Core Abstraction Hierarchy

### 5.1 BitRegion

Unified compile-time base for both registers and fields:

- `Register` = `BitRegion` with `IsRegister=true` (full-width, has address offset).
- `Field` = `BitRegion` with `IsRegister=false` (sub-width, has bit offset).
- 5 `static_assert`s validate: bit width > 0, register width ≤ 64,
  offset + width ≤ register width, register has bit offset 0,
  register bit width equals register full width.

### 5.2 RegOps (deducing this)

Provides type-safe `write()`, `read()`, `modify()`, `is()`, `flip()`, `clear()`, `reset()`, `read_variant()` methods.
Uses C++23 deducing this (P0847R7) — no CRTP `Derived` parameter.
Delegates to `self.reg_read()` / `self.reg_write()` for actual bus I/O.

Concept constraints:

| Concept | Applies to | Description |
|---------|-----------|-------------|
| `Readable<T>` | Register/Field | `can_read == true` |
| `Writable<T>` | Register/Field | `can_write == true` |
| `ReadWritable<T>` | Register/Field | Both readable and writable |
| `IsRegister<T>` | Register | `is_register == true` |
| `IsField<T>` | Field | `is_register == false` |
| `IsW1C<T>` | Field | `write_behavior == ONE_TO_CLEAR` |
| `IsW1S<T>` | Field | `write_behavior == ONE_TO_SET` |
| `IsW1T<T>` | Field | `write_behavior == ONE_TO_TOGGLE` |
| `NormalWrite<T>` | Register/Field | `write_behavior == NORMAL` (RMW-safe) |
| `ReadableValue<V>` | Value/DynamicValue | Parent region is readable |
| `WritableValue<V>` | Value/DynamicValue | Parent region is writable |
| `ModifiableValue<V>` | Value/DynamicValue | Writable AND NormalWrite |

### 5.3 ByteAdapter (deducing this)

Converts RegOps' typed register operations into `raw_read()` / `raw_write()` byte operations.
Uses C++23 deducing this — no CRTP parameter.
Handles endian conversion between host CPU and wire format using `std::byteswap` (`<bit>`).
Endianness is expressed with `std::endian` (no custom `Endian` enum).

### 5.4 Value and DynamicValue

- `Value<RegionT, EnumValue>`: compile-time constant with shifted representation.
  Holds a `RegionType` alias for the parent Register or Field type.
- `DynamicValue<RegionT, T>`: runtime value. Not validated at `value()` creation time; validation is performed by operations (`write()`/`modify()`/`is()`) according to `CheckPolicy` and `ErrorPolicy`.

### 5.5 Field Trait System

`Field<Reg, BitOffset, BitWidth, ...Traits>` uses a variadic parameter pack for traits:

- **Access policy extraction**: `detail::ExtractAccess_t<Traits...>` finds `RW`/`RO`/`WO`/`W1C` in the pack (default: `Inherit`).
  Uses `detail::IsAccessPolicy<T>` concept for reliable detection.
- **Numeric detection**: `detail::contains_v<Numeric, Traits...>` enables `value()`.
- **OneBitAliases**: 1-bit RW fields inherit `Set`/`Reset` via `detail::OneBitBase`.
- **OneBitW1CAliases**: 1-bit W1C fields inherit `Clear` via `detail::OneBitBase`.

Traits can appear in any order:
```cpp
// All equivalent for access:
struct A : mm::Field<REG, 0, 8, mm::RO, mm::Numeric> {};   // RO + Numeric
struct B : mm::Field<REG, 0, 8, mm::Numeric, mm::RO> {};   // same
struct C : mm::Field<REG, 0, 8, mm::Numeric> {};           // Inherit + Numeric
struct D : mm::Field<REG, 0, 8> {};                        // Inherit, safe
struct E : mm::Field<SR, 0, 1, mm::W1C> {};                // W1C: Clear alias
```

### 5.6 RegionValue

`read(Register{})` returns `RegionValue<Reg>`, not a raw value.
`read(Field{})` returns `RegionValue<F>`. Both are the same `RegionValue<R>` template
specialized for registers or fields.
This enables fluent chained access:

```cpp
auto cfg = hw.read(ConfigReg{});
auto en  = cfg.get(ConfigEnable{});   // RegionValue<ConfigEnable>
bool is_fast = cfg.is(ModeFast{});    // Match named value
uint32_t raw = cfg.bits();           // Raw register value
auto en_raw = en.bits();             // Raw field value (escape hatch)
```

`RegionValue<R>` stores the raw value and provides:
- `bits()` — raw value (always available)
- `operator==(RegionValue)` — same-region equality (always available)
- `get(Field{})` — extract a field value as `RegionValue<F>` (register only)
- `is(ValueType{})` — match against a named value (register only)
- `operator==` with `Value`/`DynamicValue` — typed comparison (field only)

### 5.7 Register Arrays and Runtime Dispatch

#### RegisterArray

`RegisterArray<Template, N>` captures compile-time metadata for template register banks (e.g., NVIC ISER[0..7]):

- `size` — number of elements
- `Element<I>` — access the I-th register type

No data member, no runtime cost — pure type-level metadata.

#### dispatch / dispatch_r

Converts a runtime index to a compile-time template parameter. Uses a fold expression over `std::index_sequence`:

```cpp
dispatch<N>(idx, [&]<std::size_t I>() { ... });           // void
auto val = dispatch_r<N, R>(idx, [&]<std::size_t I>() { return ...; });  // with return
```

Out-of-range index invokes `ErrorPolicy::on_range_error()`.

#### IndexedArray

`IndexedArray<Parent, BaseOffset, Count, EntryWidth, Stride>` models sub-register granularity arrays (e.g., lookup tables, FIFO arrays):

- `Entry<N>` — compile-time access as a `Register` type. `static_assert` rejects N ≥ Count.
- `write_entry(index, value)` / `read_entry(index)` — runtime access via volatile pointer. Requires `Direct` in `AllowedTransportsType` (`static_assert`). Out-of-range index invokes `ErrorPolicy::on_range_error()`.
- `EntryWidth` — bit width per entry (default: `bits8`). Determines `EntryType` via `UintFit<EntryWidth>` (e.g., `bits8` → `uint8_t`, `bits16` → `uint16_t`).
- `Stride` — byte spacing between consecutive entries (default: `EntryWidth / 8`, i.e., dense packing with no gaps). Override for hardware with alignment padding (e.g., 16-bit entries at 4-byte boundaries: `Stride = 4`). Address of entry N = `base_address + BaseOffset + N * Stride`.

### 5.8 CsrTransport

`CsrTransport<Accessor>` extends umimmio's transport model to RISC-V CSR registers.

**Key design decisions**:

- CSR numbers map to `Register::address` (Device `base_address = 0`).
- `CsrAccessor` concept provides the customization point — `csr_read<CsrNum>()` / `csr_write<CsrNum>(value)` where `CsrNum` is a compile-time constant (required by RISC-V's 12-bit immediate encoding).
- `DefaultCsrAccessor` (RISC-V only, `#if __riscv`) provides inline asm for Phase 1 CSRs (mstatus, misa, mie, mtvec, mscratch, mepc, mcause, mtval, mip).
- For host testing, any type satisfying `CsrAccessor` (e.g., RAM-backed mock) can be injected.
- Not included in the umbrella header (`mmio.hh`) — users explicitly include `<umimmio/transport/csr.hh>`.

### 5.9 AtomicDirectTransport

`AtomicDirectTransport<AliasOffset>` is a write-only transport that adds a fixed byte offset to all register writes.

**Key design decisions**:

- Writes target `(Reg::address + AliasOffset)` via volatile pointer. No `reg_read()` — `read()`, `modify()`, `flip()`, and `is()` are compile errors because the `Readable` concept is not satisfied.
- `write()` and `reset()` work normally (both are write-only operations).
- Primary use case: RP2040 atomic register aliases where SET (+0x2000), CLR (+0x3000), and XOR (+0x1000) are write-only aliases of base registers.
- The transport is generic — any MCU with write-aliased registers can use it.
- Not included in the umbrella header (`mmio.hh`) — users explicitly include `<umimmio/transport/atomic_direct.hh>`.
- 3 compile_fail tests verify that `read()`, `modify()`, and `flip()` are rejected.

### 5.10 read_variant()

`read_variant()` reads a field and pattern-matches its value against a set of
named `Value<>` types, returning a `std::variant`. If no match is found,
`UnknownValue<F>` is returned as the last alternative.

```cpp
auto result = hw.read_variant<CTRL::MODE,
                              CTRL::MODE::Normal,
                              CTRL::MODE::Fast,
                              CTRL::MODE::LowPwr>();

// result type: std::variant<Normal, Fast, LowPwr, UnknownValue<MODE>>

std::visit([](auto v) {
    if constexpr (std::is_same_v<decltype(v), CTRL::MODE::Fast>) {
        // handle Fast mode
    } else if constexpr (std::is_same_v<decltype(v), UnknownValue<CTRL::MODE>>) {
        // handle unexpected value — v.value holds the raw bits
    }
}, result);
```

This is particularly useful when a field has many named values and the caller
wants exhaustive handling via `std::visit`.

---

## 6. Error Handling Model

### 6.1 Compile-Time Errors

1. Access policy violations → `requires` clause failure with clear concept names.
2. Value out of range in `consteval` context → `detail::mmio_compile_time_error_value_out_of_range`.
3. `value()` on non-Numeric field → concept constraint failure (`requires(is_numeric)`).
4. `value()` with signed type → concept constraint failure (`std::unsigned_integral`).
5. Transport not allowed for device → `static_assert` failure.
6. `BitRegion` overflow (offset + width > register width) → `static_assert` failure.
7. W1C field in `modify()` → `ModifiableValue` concept rejects W1C.
8. W1S/W1T field in `modify()`/`flip()` → `NormalWrite` concept rejects non-NORMAL WriteBehavior.
9. `RegionValue == integer` → no matching `operator==` (use `.bits()` for raw access).

For runtime error policies, see [README](../README.md).

---

## 7. Test Strategy

See [testing.md](../tests/testing.md) for test layout, run commands, and quality gates.

---

## 8. Design Principles

1. Zero-cost abstraction — all dispatch resolved at compile time.
2. Type-safe — access violations are compile errors, not runtime bugs.
3. Transport-agnostic — same register map, any bus.
4. Policy-based — error handling, range checking, and endianness are configurable.
5. Embedded-first — no heap, no exceptions, no RTTI.

---

For write()/modify() semantics, W1C fields, and concurrency,
see [README](../README.md).
