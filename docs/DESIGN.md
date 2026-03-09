# umimmio Design

[日本語](DESIGN.ja.md)

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
The `IsW1C` concept identifies them; `NotW1C` excludes them from `modify()` and `flip()`.

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
2. Runtime `DynamicValue` is checked by `CheckPolicy` + `ErrorPolicy` at write/modify time.
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
│   ├── INDEX.md
│   ├── DESIGN.md
│   ├── TESTING.md
│   └── ja/
├── examples/
│   ├── minimal.cc
│   ├── register_map.cc
│   └── transport_mock.cc
├── include/umimmio/
│   ├── mmio.hh              # Umbrella header
│   ├── policy.hh            # Foundation: AccessPolicy, transport tags, error policies
│   ├── region.hh            # Data model: Device, Register, Field, Value, concepts
│   ├── ops.hh               # Operations: RegOps, ByteAdapter
│   └── transport/
│       ├── detail.hh        # Shared helpers for address encoding
│       ├── direct.hh        # DirectTransport (volatile pointer)
│       ├── i2c.hh           # I2cTransport (HAL-based)
│       └── spi.hh           # SpiTransport (HAL-based)
└── tests/
    ├── test_main.cc
    ├── test_access_policy.cc
    ├── test_register_field.cc
    ├── test_transport.cc
    ├── test_byte_transport.cc
    ├── compile_fail/
    │   ├── clear_non_w1c.cc
    │   ├── cross_register_write.cc
    │   ├── field_overflow.cc
    │   ├── flip_ro.cc
    │   ├── flip_w1c.cc
    │   ├── flip_wo.cc
    │   ├── modify_w1c.cc
    │   ├── modify_wo.cc
    │   ├── read_field_eq_int.cc
    │   ├── read_wo.cc
    │   ├── value_signed.cc
    │   ├── value_typesafe.cc
    │   ├── write_ro.cc
    │   ├── write_ro_value.cc
    │   └── write_zero_args.cc
    └── xmake.lua
```

---

## 4. Programming Model

### 4.0 API Reference

Public entrypoint: `include/umimmio/mmio.hh`

Core types:

| Type | Purpose |
|------|---------|
| `Device<Access, Transports...>` | Device root with access policy and allowed transports. MMIO devices override `base_address`. |
| `Block<Parent, BaseAddr, Access>` | Address sub-region within a Device (inherits parent's transports). |
| `Register<Device, Offset, Bits, Access, Reset, W1cMask>` | Register at an offset within a device. `W1cMask` specifies which bits are W1C. |
| `Field<Reg, BitOffset, BitWidth, ...Traits>` | Bit field within a register (variadic traits) |
| `Value<Field, val>` | Named constant for a Field |
| `DynamicValue<Field, T>` | Runtime value for a Field |
| `RegionValue<R>` | Unified return type of `read()` and `get()` — `bits()` always; register: `get()`, `is()`; field: typed `==` only |
| `UnknownValue<Reg>` | Sentinel type for `read_variant()` when no named value matches |
| `Numeric` | Trait: enables raw `value()` on a Field |
| `Inherit` | Sentinel: Field inherits access policy from parent Register |
| `WriteBehavior` | Enum: `NORMAL` or `ONE_TO_CLEAR` |

Transport types:

| Transport | Use Case |
|-----------|----------|
| `DirectTransport` | Memory-mapped I/O (volatile pointer access) |
| `I2cTransport` | HAL-compatible I2C peripheral drivers |
| `SpiTransport` | HAL-compatible SPI peripheral drivers |

Access policies:

| Policy | `read()` | `write()` | `modify()` | `clear()` | `WriteBehavior` |
|--------|:--------:|:---------:|:----------:|:---------:|:---------------:|
| `RW` | Yes | Yes | Yes | — | `NORMAL` |
| `RO` | Yes | No | No | — | `NORMAL` |
| `WO` | No | Yes | No | — | `NORMAL` |
| `W1C` | Yes | — | No | Yes | `ONE_TO_CLEAR` |

Operations:

| Operation | Purpose | Constraint |
|-----------|---------|------------|
| `read(Reg{})` | Read register → `RegionValue<Reg>` | `Readable<Reg>` |
| `read(Field{})` | Read field → `RegionValue<F>` (use `.bits()` for raw) | `Readable<Field>` |
| `write(v1, v2, ...)` | Write values (from reset) | `WritableValue` |
| `modify(v1, v2, ...)` | Read-modify-write | `ModifiableValue` (excludes W1C) |
| `is(v)` | Compare field/register value | `ReadableValue` |
| `flip(F{})` | Toggle 1-bit field (W1C mask applied) | `ReadWritable && NotW1C` |
| `clear(F{})` | W1C field: write-1-to-clear (RMW for mixed registers) | `IsW1C<F>` |
| `reset(Reg{})` | Write `Reg::reset_value()` | `Writable<Reg>` |
| `read_variant(F{}, V1{}, ..., VN{})` | Pattern-match field value → `std::variant` | — |

Static methods on Register/Field:

| Method | Purpose | Availability |
|--------|---------|-------------|
| `<Register>::value(T)` | Create `DynamicValue` with range check | Register (always) |
| `<Field>::value(T)` | Create `DynamicValue` with range check | Field with `Numeric` trait |
| `mask()` | Compile-time bit mask | Register, Field |
| `reset_value()` | Compile-time reset value | Register, Field (inherited) |

Concurrency:

`modify()` is not atomic (read-modify-write). For ISR-safe or multi-context
access, the caller must serialize access externally (e.g. disable interrupts,
use a scoped lock). See §9.4 for the pattern.

### 4.1 Minimal Path

Required minimal flow for direct MMIO:

1. Define `Device` with base address and access policy.
2. Define `Register` within the device.
3. Define `Field` within the register.
4. Construct `DirectTransport`.
5. Call `transport.write(Field::Set{})` or `transport.read(Field{})`.

### 4.2 Register Map Organization

The recommended style is **hierarchical nesting**: Device contains Registers,
Registers contain Fields, Fields contain named Values. This mirrors the
physical device structure and avoids name collisions when multiple registers
share common field names (e.g. `EN`, `MODE`).

```cpp
namespace mm = umi::mmio;

struct MyDevice : mm::Device<mm::RW> {
    static constexpr mm::Addr base_address = 0x4000'0000;

    struct CTRL : mm::Register<MyDevice, 0x00, 32> {
        // 1-bit field — Set/Reset auto-generated
        struct EN : mm::Field<CTRL, 0, 1> {};

        // 2-bit field with named values — safe by default (no raw value())
        struct MODE : mm::Field<CTRL, 1, 2> {
            using Output  = mm::Value<MODE, 0b01>;
            using AltFunc = mm::Value<MODE, 0b10>;
        };

        // 9-bit numeric field — raw value() enabled
        struct PLLN : mm::Field<CTRL, 6, 9, mm::Numeric> {};

        // Read-only + numeric
        struct DR : mm::Field<CTRL, 0, 16, mm::RO, mm::Numeric> {};
    };

    // W1C status register with W1cMask
    struct SR : mm::Register<MyDevice, 0x04, 32, mm::RW, 0, 0x0003> {
        // W1C field — Clear alias auto-generated (instead of Set/Reset)
        struct OVR : mm::Field<SR, 0, 1, mm::W1C> {};
        struct EOC : mm::Field<SR, 1, 1, mm::W1C> {};
        struct READY : mm::Field<SR, 8, 1> {};  // Normal RW field
    };
};
```

> **Note:** Flat-style definitions (registers and fields as standalone structs outside
> the device) are also valid C++ and compile correctly. However, the hierarchical
> style is recommended because it naturally prevents name collisions and makes
> the type path (`MyDevice::CTRL::MODE::Output`) self-documenting.

### 4.2.1 Field Type Safety Model

Fields are **safe by default**: only named `Value<>` types are accepted.
The `Numeric` trait opts a field into raw `value()` access.
Use `RegionValue::bits()` for raw value extraction on the read side.

| Field kind | `value()` | `Value<>` types |
|-----------|:---------:|:---------------:|
| Default (safe) | Blocked | Yes |
| With `Numeric` | Yes (unsigned only) | Yes |
| 1-bit RW | — | `Set` / `Reset` auto |
| 1-bit W1C | — | `Clear` auto |

**`Field<Reg, BitOffset, BitWidth, ...Traits>`** — variadic traits pattern:
- Traits can include access policy (`RW`, `RO`, `WO`, `W1C`) and/or `Numeric`, in any order.
- Default access is `Inherit` (from parent register).
- 1-bit RW fields automatically provide `Set` and `Reset` type aliases.
- 1-bit W1C fields automatically provide `Clear` type alias.

### 4.3 Transport Selection

Transport is selected by constructing the appropriate type:

```cpp
umi::mmio::DirectTransport<> direct;                    // Volatile pointer
umi::mmio::I2cTransport<MyI2c> i2c(hal_i2c, 0x68);     // HAL I2C
umi::mmio::SpiTransport<MySpi> spi(hal_spi);            // HAL SPI
```

All transports expose the same `write()`, `read()`, `modify()`, `is()`, `flip()`, `clear()`, `reset()` API.

### 4.4 Advanced Path

Advanced usage includes:

1. multi-field write in a single bus transaction,
2. read-modify-write via `modify()`,
3. custom error policies (trap, ignore, callback),
4. 16-bit address space for I2C/SPI devices,
5. configurable address and data endianness via `std::endian`,
6. W1C field handling via `clear()`,
7. register reset via `reset()`,
8. pattern-matched field reading via `read_variant()`,
9. ISR-safe access by wrapping transport operations in a caller-provided critical section.

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
| `NotW1C<T>` | Field | Not W1C |
| `ReadableValue<V>` | Value/DynamicValue | Parent region is readable |
| `WritableValue<V>` | Value/DynamicValue | Parent region is writable |
| `ModifiableValue<V>` | Value/DynamicValue | Writable AND parent is not W1C |

### 5.3 ByteAdapter (deducing this)

Converts RegOps' typed register operations into `raw_read()` / `raw_write()` byte operations.
Uses C++23 deducing this — no CRTP parameter.
Handles endian conversion between host CPU and wire format using `std::byteswap` (`<bit>`).
Endianness is expressed with `std::endian` (no custom `Endian` enum).

### 5.4 Value and DynamicValue

- `Value<RegionT, EnumValue>`: compile-time constant with shifted representation.
  Holds a `RegionType` alias for the parent Register or Field type.
- `DynamicValue<RegionT, T>`: runtime value with deferred range check.

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

### 5.7 read_variant()

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
8. W1C field in `flip()` → `NotW1C` concept rejects W1C.
9. `RegionValue == integer` → no matching `operator==` (use `.bits()` for raw access).

### 6.2 Runtime Error Policies

Policy-based via `ErrorPolicy` template parameter:

| Policy | Behavior |
|--------|----------|
| `AssertOnError` | `assert(false && msg)` (default) |
| `TrapOnError` | `std::abort()` |
| `IgnoreError` | Silent no-op |
| `CustomErrorHandler<fn>` | User callback |

Each policy provides two entry points:

| Entry Point | Triggered By |
|---|---|
| `on_range_error(msg)` | Value out-of-range for field width (programming error) |
| `on_transport_error(msg)` | HAL driver reports failure (bus error, NACK, timeout) |

I2cTransport and SpiTransport use `if constexpr` to check whether the HAL driver's
return type is `void` or convertible to `bool`. If the HAL returns a falsy value,
`on_transport_error()` is invoked. Void-returning HALs skip the check.

---

## 7. Test Strategy

See [TESTING.md](TESTING.md) for test layout, run commands, and quality gates.

---

## 8. Design Principles

1. Zero-cost abstraction — all dispatch resolved at compile time.
2. Type-safe — access violations are compile errors, not runtime bugs.
3. Transport-agnostic — same register map, any bus.
4. Policy-based — error handling, range checking, and endianness are configurable.
5. Embedded-first — no heap, no exceptions, no RTTI.

---

## 9. write() / modify() Semantics Guide

### 9.1 Semantic Difference

| Operation | Base Value | Semantics | Safety |
|-----------|-----------|-----------|:------:|
| `write(v1, v2, ...)` | `reset_value()` | Initialize register from reset state | ✅ |
| `modify(v1, v2, ...)` | Current value (RMW) | Change specific fields, preserving others | ✅ |
| `write(single_v)` | `reset_value()` | Initialize register — other fields reset | ⚠️ |

### 9.2 Usage Rules

1. **Initialization**: Use `write()` with all relevant fields specified.
2. **Runtime change**: Use `modify()` to change specific fields.
3. **Single-field write**: `write(v)` resets other fields to `reset_value()`.
   This is intentional for single-field registers or full reset.
   Use `modify()` for runtime single-field changes.

### 9.3 W1C Fields

W1C fields must use `clear()`:

```cpp
hw.clear(MyDevice::SR::OVR{});             // ✅ Correct: clears OVR (RMW preserves non-W1C fields)
hw.modify(MyDevice::SR::OVR::Clear{});     // ✗ Compile error: W1C not ModifiableValue
hw.flip(MyDevice::SR::OVR{});              // ✗ Compile error: W1C not NotW1C
```

During `modify()` and `flip()`, W1C bits in the parent register are automatically masked to 0
before write-back via `Register::w1c_mask`. This prevents accidental clearing
of W1C status bits during read-modify-write operations on other fields.

`clear()` on mixed registers (containing both W1C and non-W1C fields) uses
read-modify-write to preserve non-W1C field values. For pure-W1C registers
(where all bits are W1C), a direct write is used for efficiency.

### 9.4 Atomicity

`modify()` performs read-modify-write and is **never atomic**.
For ISR-safe access, the caller must serialize access externally:

```cpp
// Example: wrap transport operations in a platform-specific critical section.
// The exact mechanism (disable IRQ, mutex, etc.) is determined by the caller.
{
    auto lock = enter_critical_section();  // platform-specific
    io.modify(ConfigEnable::Set{});        // ISR-safe RMW
}   // lock released (RAII)
```

umimmio itself is transport-level only and does not provide synchronization primitives.
Callers are responsible for choosing the appropriate locking mechanism for their platform.

### 9.5 reset()

`reset(Reg{})` writes the register's `reset_value()` directly. This is a pure write
(not read-modify-write) and is suitable for returning hardware to its initial state.
