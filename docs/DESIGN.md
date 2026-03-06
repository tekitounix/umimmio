# umimmio Design

[日本語](ja/DESIGN.md)

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
2. I2C bus (HAL-based or bit-bang).
3. SPI bus (HAL-based or bit-bang).

Transport is a template parameter, not a base-class pointer.

### 2.4 Range Checking

Field values are range-checked at compile time when possible:

1. `value()` with a literal exceeding field width triggers `mmio_compile_time_error_value_out_of_range` via `if consteval`.
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
│   ├── ops.hh               # Operations: RegOps, ByteAdapter, RegisterReader
│   ├── protected.hh         # Protected<T, LockPolicy>, Guard, lock policies
│   └── transport/
│       ├── direct.hh        # DirectTransport (volatile pointer)
│       ├── i2c.hh           # I2cTransport (HAL-based)
│       ├── spi.hh           # SpiTransport (HAL-based)
│       ├── bitbang_i2c.hh   # BitBangI2cTransport (GPIO)
│       └── bitbang_spi.hh   # BitBangSpiTransport (GPIO)
└── tests/
    ├── test_main.cc
    ├── test_access_policy.cc
    ├── test_register_field.cc
    ├── test_transport.cc
    ├── test_spi_bitbang.cc
    ├── test_protected.cc
    ├── compile_fail/
    │   ├── read_wo.cc
    │   ├── write_ro.cc
    │   ├── write_ro_value.cc
    │   ├── value_typesafe.cc
    │   ├── value_signed.cc
    │   ├── modify_w1c.cc
    │   ├── flip_w1c.cc
    │   └── field_overflow.cc
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
| `Register<Device, Offset, Bits, Access, Reset, W1cMask>` | Register at an offset within a device. `W1cMask` specifies which bits are W1C. |
| `Field<Reg, BitOffset, BitWidth, ...Traits>` | Bit field within a register (variadic traits) |
| `Value<Field, val>` | Named constant for a Field |
| `DynamicValue<Field, T>` | Runtime value for a Field |
| `RegisterReader<Reg>` | Return type of `read(Register{})` — provides `bits()`, `get()`, `is()` |
| `FieldValue<F>` | Return type of `read(Field{})` and `get(Field{})` — type-safe, use `.bits()` for raw |
| `UnknownValue<Reg>` | Sentinel type for `read_variant()` when no named value matches |
| `Numeric` | Trait: enables raw `value()` on a Field |
| `WriteBehavior` | Enum: `NORMAL` or `ONE_TO_CLEAR` |

Transport types:

| Transport | Use Case |
|-----------|----------|
| `DirectTransport` | Memory-mapped I/O (volatile pointer access) |
| `I2cTransport` | HAL-compatible I2C peripheral drivers |
| `SpiTransport` | HAL-compatible SPI peripheral drivers |
| `BitBangI2cTransport` | Software I2C via GPIO |
| `BitBangSpiTransport` | Software SPI via GPIO |

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
| `read(Reg{})` | Read register → `RegisterReader<Reg>` | `Readable<Reg>` |
| `read(Field{})` | Read field → `FieldValue<F>` (use `.bits()` for raw) | `Readable<Field>` |
| `write(v1, v2, ...)` | Write values (from reset) | `WritableValue` |
| `modify(v1, v2, ...)` | Read-modify-write | `ModifiableValue` (excludes W1C) |
| `is(v)` | Compare field/register value | `ReadableValue` |
| `flip(F{})` | Toggle 1-bit field | `ReadWritable && NotW1C` |
| `clear(F{})` | Write-1-to-clear a W1C field | `IsW1C<F>` |
| `reset(Reg{})` | Write `Reg::reset_value()` | `Writable<Reg>` |
| `read_variant(F{}, V1{}, ..., VN{})` | Pattern-match field value → `std::variant` | — |

Concurrency types:

| Type | Purpose |
|------|---------|
| `Protected<T, LockPolicy>` | Wraps T, only accessible via `lock()` → `Guard` |
| `Guard<T, LockPolicy>` | RAII scoped access to Protected inner value |
| `MutexPolicy<MutexT>` | RTOS mutex wrapper |
| `NoLockPolicy` | No-op lock for single-threaded or test contexts |

`CriticalSectionPolicy` (ARM Cortex-M `cpsid`/`cpsie`) is provided by `umiport` — see `<umiport/platform/embedded/critical_section.hh>`.

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
Use `FieldValue::bits()` or `RegisterReader::bits()` for raw value extraction on the read side.

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
9. ISR-safe access via `Protected<Transport, LockPolicy>` (platform-specific lock policy injected via DI).

---

## 5. Core Abstraction Hierarchy

### 5.1 BitRegion

Unified compile-time base for both registers and fields:

- `Register` = `BitRegion` with `IsRegister=true` (full-width, has address offset).
- `Field` = `BitRegion` with `IsRegister=false` (sub-width, has bit offset).
- 5 `static_assert`s validate: bit width > 0, offset + width ≤ register width,
  register width is power of 2, register width ≥ 8, no zero-width register.

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
  Uses `RegionType` (not `FieldType`) as the primary type reference.
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

### 5.6 RegisterReader

`read(Register{})` returns `RegisterReader<Reg>`, not a raw value.
This enables fluent chained access:

```cpp
auto cfg = hw.read(ConfigReg{});
auto en  = cfg.get(ConfigEnable{});   // FieldValue<ConfigEnable>
bool is_fast = cfg.is(ModeFast{});    // Match named value
uint32_t raw = cfg.bits();           // Raw register value
auto en_raw = en.bits();             // Raw field value (escape hatch)
```

`RegisterReader` stores the raw value and provides:
- `bits()` — raw register value
- `get(Field{})` — extract a field value as `FieldValue<F>`
- `is(ValueType{})` — match against a named value

---

## 6. Error Handling Model

### 6.1 Compile-Time Errors

1. Access policy violations → `requires` clause failure with clear concept names.
2. Value out of range in `consteval` context → `mmio_compile_time_error_value_out_of_range`.
3. `value()` on non-Numeric field → concept constraint failure (`requires(is_numeric)`).
4. `value()` with signed type → concept constraint failure (`std::unsigned_integral`).
5. Transport not allowed for device → `static_assert` failure.
6. `BitRegion` overflow (offset + width > register width) → `static_assert` failure.
7. W1C field in `modify()` → `ModifiableValue` concept rejects W1C.
8. W1C field in `flip()` → `NotW1C` concept rejects W1C.
9. `FieldValue == integer` → no matching `operator==` (use `.bits()` for raw access).

### 6.2 Runtime Error Policies

Policy-based via `ErrorPolicy` template parameter:

| Policy | Behavior |
|--------|----------|
| `AssertOnError` | `assert(false && msg)` (default) |
| `TrapOnError` | `__builtin_trap()` |
| `IgnoreError` | Silent no-op |
| `CustomErrorHandler<fn>` | User callback |

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
hw.clear(MyDevice::SR::OVR{});             // ✅ Correct: writes 1 to OVR only
hw.modify(MyDevice::SR::OVR::Clear{});     // ✗ Compile error: W1C not ModifiableValue
hw.flip(MyDevice::SR::OVR{});              // ✗ Compile error: W1C not NotW1C
```

During `modify()`, W1C bits in the parent register are automatically masked to 0
before write-back via `Register::w1c_mask`. This prevents accidental clearing
of W1C status bits during read-modify-write operations on other fields.

### 9.4 Atomicity

`modify()` performs read-modify-write and is **never atomic**.
For ISR-safe access, use `Protected<Transport, LockPolicy>` with a platform-specific policy:

```cpp
// ARM Cortex-M: #include <umiport/platform/embedded/critical_section.hh>
using umi::port::platform::CriticalSectionPolicy;
Protected<DirectTransport<>, CriticalSectionPolicy> protected_hw;

auto guard = protected_hw.lock();   // __disable_irq()
guard->modify(ConfigEnable::Set{}); // ISR-safe RMW
// ~Guard() → __enable_irq() (RAII)
```

On non-ARM platforms, use `MutexPolicy<MutexT>` or `NoLockPolicy` as appropriate.

### 9.5 reset()

`reset(Reg{})` writes the register's `reset_value()` directly. This is a pure write
(not read-modify-write) and is suitable for returning hardware to its initial state.
