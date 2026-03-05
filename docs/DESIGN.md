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

### 2.5 Dependency Boundaries

Layering is strict:

1. `umimmio` depends only on C++23 standard library headers.
2. `umibench/platforms` depends on `umimmio` for DWT/CoreSight register access.
3. `tests/` depends on `umitest` for assertions.

Reference dependency graph:

```text
umibench/platforms/* -> umimmio
umimmio/tests        -> umitest
```

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
│   ├── ja/
│   └── plans/
├── examples/
│   ├── minimal.cc
│   ├── register_map.cc
│   └── transport_mock.cc
├── include/umimmio/
│   ├── mmio.hh              # Umbrella header
│   ├── register.hh          # Core: RegOps, ByteAdapter, BitRegion, Field, Value, concepts
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

## 4. Growth Layout

```text
lib/umimmio/
├── include/umimmio/
│   ├── mmio.hh
│   ├── register.hh
│   ├── protected.hh
│   └── transport/
│       ├── direct.hh
│       ├── i2c.hh
│       ├── spi.hh
│       ├── bitbang_i2c.hh
│       ├── bitbang_spi.hh
│       └── uart.hh           # Future: UART register transport
├── examples/
│   ├── minimal.cc
│   ├── register_map.cc
│   ├── transport_mock.cc
│   └── multi_transport.cc    # Future: same map, different transports
└── tests/
    ├── test_main.cc
    ├── test_*.cc
    ├── compile_fail/
    │   └── *.cc
    └── xmake.lua
```

Notes:

1. Public headers stay under `include/umimmio/`.
2. New transports are added as separate headers under `transport/`.
3. `register.hh` is the core and should remain stable.
4. Transport-specific error policies may be added per transport.

---

## 5. Programming Model

### 5.0 API Reference

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
| `UnknownValue<Reg>` | Sentinel type for `read_variant()` when no named value matches |
| `Numeric` | Trait: enables raw `value()` on a Field |
| `raw<Field>(val)` | Escape hatch: raw value for any Field |
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
| `read(Field{})` | Read field → extracted value | `Readable<Field>` |
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
| `CriticalSectionPolicy` | ARM Cortex-M: `cpsid`/`cpsie` |
| `MutexPolicy<MutexT>` | RTOS mutex wrapper |
| `NoLockPolicy` | No-op lock for single-threaded or test contexts |

### 5.1 Minimal Path

Required minimal flow for direct MMIO:

1. Define `Device` with base address and access policy.
2. Define `Register` within the device.
3. Define `Field` within the register.
4. Construct `DirectTransport`.
5. Call `transport.write(Field::Set{})` or `transport.read(Field{})`.

### 5.2 Register Map Organization

Typical device register map structure:

```cpp
namespace mm = umi::mmio;

struct MyDevice : mm::Device<mm::RW> {
    static constexpr mm::Addr base_address = 0x4000'0000;
};

using CTRL = mm::Register<MyDevice, 0x00, 32>;

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

// W1C status register with W1cMask
using SR = mm::Register<MyDevice, 0x04, 32, mm::RW, 0, 0x0003>;

// W1C field — Clear alias auto-generated (instead of Set/Reset)
struct OVR : mm::Field<SR, 0, 1, mm::W1C> {};
struct EOC : mm::Field<SR, 1, 1, mm::W1C> {};
struct READY : mm::Field<SR, 8, 1> {};  // Normal RW field
```

### 5.2.1 Field Type Safety Model

Fields are **safe by default**: only named `Value<>` types and the `raw<>()` escape hatch are accepted.
The `Numeric` trait opts a field into raw `value()` access.

| Field kind | `value()` | `Value<>` types | `raw<>()`  |
|-----------|:---------:|:---------------:|:----------:|
| Default (safe) | Blocked | Yes | Yes |
| With `Numeric` | Yes (unsigned only) | Yes | Yes |
| 1-bit RW | — | `Set` / `Reset` auto | Yes |
| 1-bit W1C | — | `Clear` auto | Yes |

**`Field<Reg, BitOffset, BitWidth, ...Traits>`** — variadic traits pattern:
- Traits can include access policy (`RW`, `RO`, `WO`, `W1C`) and/or `Numeric`, in any order.
- Default access is `Inherit` (from parent register).
- 1-bit RW fields automatically provide `Set` and `Reset` type aliases.
- 1-bit W1C fields automatically provide `Clear` type alias.

**`raw<Field>(val)`** — escape hatch:
- Creates a `DynamicValue` for any field, bypassing type safety.
- Analogous to `const_cast` — the name signals deliberate bypassing.
- Range-checked at compile time when `val` is a literal.

### 5.3 Transport Selection

Transport is selected by constructing the appropriate type:

```cpp
umi::mmio::DirectTransport<> direct;                    // Volatile pointer
umi::mmio::I2cTransport<MyI2c> i2c(hal_i2c, 0x68);     // HAL I2C
umi::mmio::SpiTransport<MySpi> spi(hal_spi);            // HAL SPI
```

All transports expose the same `write()`, `read()`, `modify()`, `is()`, `flip()`, `clear()`, `reset()` API.

### 5.4 Advanced Path

Advanced usage includes:

1. multi-field write in a single bus transaction,
2. read-modify-write via `modify()`,
3. custom error policies (trap, ignore, callback),
4. 16-bit address space for I2C/SPI devices,
5. configurable address and data endianness via `std::endian`,
6. W1C field handling via `clear()`,
7. register reset via `reset()`,
8. pattern-matched field reading via `read_variant()`,
9. ISR-safe access via `Protected<Transport, CriticalSectionPolicy>`.

---

## 6. Core Abstraction Hierarchy

### 6.1 BitRegion

Unified compile-time base for both registers and fields:

- `Register` = `BitRegion` with `IsRegister=true` (full-width, has address offset).
- `Field` = `BitRegion` with `IsRegister=false` (sub-width, has bit offset).
- 5 `static_assert`s validate: bit width > 0, offset + width ≤ register width,
  register width is power of 2, register width ≥ 8, no zero-width register.

### 6.2 RegOps (deducing this)

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

### 6.3 ByteAdapter (deducing this)

Converts RegOps' typed register operations into `raw_read()` / `raw_write()` byte operations.
Uses C++23 deducing this — no CRTP parameter.
Handles endian conversion between host CPU and wire format using `std::byteswap` (`<bit>`).
Endianness is expressed with `std::endian` (no custom `Endian` enum).

### 6.4 Value and DynamicValue

- `Value<RegionT, EnumValue>`: compile-time constant with shifted representation.
  Uses `RegionType` (not `FieldType`) as the primary type reference.
- `DynamicValue<RegionT, T>`: runtime value with deferred range check.

### 6.5 Field Trait System

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

### 6.6 RegisterReader

`read(Register{})` returns `RegisterReader<Reg>`, not a raw value.
This enables fluent chained access:

```cpp
auto cfg = hw.read(ConfigReg{});
auto en  = cfg.get(ConfigEnable{});   // Extract field value
bool is_fast = cfg.is(ModeFast{});    // Match named value
uint32_t raw = cfg.bits();           // Raw register value
```

`RegisterReader` stores the raw value and provides:
- `bits()` — raw register value
- `get(Field{})` — extract a field value
- `is(ValueType{})` — match against a named value
- Implicit conversion to `RegValueType` for backward compatibility

---

## 7. Error Handling Model

### 7.1 Compile-Time Errors

1. Access policy violations → `requires` clause failure with clear concept names.
2. Value out of range in `consteval` context → `mmio_compile_time_error_value_out_of_range`.
3. `value()` on non-Numeric field → concept constraint failure (`requires(is_numeric)`).
4. `value()` with signed type → concept constraint failure (`std::unsigned_integral`).
5. Transport not allowed for device → `static_assert` failure.
6. `BitRegion` overflow (offset + width > register width) → `static_assert` failure.
7. W1C field in `modify()` → `ModifiableValue` concept rejects W1C.
8. W1C field in `flip()` → `NotW1C` concept rejects W1C.

### 7.2 Runtime Error Policies

Policy-based via `ErrorPolicy` template parameter:

| Policy | Behavior |
|--------|----------|
| `AssertOnError` | `assert(false && msg)` (default) |
| `TrapOnError` | `__builtin_trap()` |
| `IgnoreError` | Silent no-op |
| `CustomErrorHandler<fn>` | User callback |

---

## 8. Test Strategy

1. Tests split by concern: access policy, register/field, transport, protected access.
2. Compile-fail tests verify API contract enforcement (8 test files).
3. Transport tests use RAM-backed mock implementing `reg_read` / `reg_write`.
4. Hardware-level MMIO tests require actual hardware and are out of scope for host tests.
5. CI runs host tests and compile-fail checks.

### 8.1 Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_access_policy.cc`: RW/RO/WO/W1C policy enforcement, WriteBehavior
- `tests/test_register_field.cc`: BitRegion, Register, Field, Value, mask/shift, RegisterReader
- `tests/test_transport.cc`: RAM-backed mock transport for read/write/modify/is/flip/clear/reset/read_variant
- `tests/test_spi_bitbang.cc`: SPI, I2C, BitBang transports, ByteAdapter endian tests
- `tests/test_protected.cc`: Protected<T, LockPolicy> with NoLockPolicy
- `tests/compile_fail/read_wo.cc`: reading a write-only register
- `tests/compile_fail/write_ro.cc`: writing a read-only register
- `tests/compile_fail/write_ro_value.cc`: writing a read-only register via value
- `tests/compile_fail/value_typesafe.cc`: `value()` on non-Numeric field
- `tests/compile_fail/value_signed.cc`: `value()` with signed integer
- `tests/compile_fail/modify_w1c.cc`: `modify()` on W1C field
- `tests/compile_fail/flip_w1c.cc`: `flip()` on W1C field
- `tests/compile_fail/field_overflow.cc`: BitRegion overflow (offset + width > reg width)

### 8.2 Running Tests

```bash
xmake test                              # all targets
xmake test 'test_umimmio/*'             # host only
xmake test 'test_umimmio_compile_fail/*'  # compile-fail only
```

### 8.3 Quality Gates

- All host tests pass (59 tests)
- All compile-fail contract tests pass (8 tests)
- Transport mock tests cover single and multi-field write, modify, is, flip, clear, reset, read_variant
- W1C compile-fail tests pass (modify, flip)
- BitRegion overflow compile-fail test passes
- Embedded cross-build passes in CI (gcc-arm)

---

## 9. Example Strategy

Examples represent learning stages:

1. `minimal`: basic register and field definition with compile-time checks.
2. `register_map`: realistic SPI peripheral register map layout.
3. `transport_mock`: RAM-backed mock transport for host-side testing.

---

## 10. Near-Term Improvement Plan

1. Add UART transport header.
2. Add multi-transport example (same register map, different buses).
3. Document register map generation workflow for vendor SVD/CMSIS files.
4. Add batch register dump utility for debugging.
5. **Constrain byte-transport template parameters with HAL concepts.**
   Currently, `I2cTransport<I2C>` and `SpiTransport<SpiDevice>` accept any type
   as the HAL driver parameter (duck typing). Adding explicit `requires` clauses
   would produce clear concept-mismatch errors instead of deep template failures.
6. **Resolve naming collision between `umi::hal` and `umi::mmio` Transport types.**

---

## 11. Design Principles

1. Zero-cost abstraction — all dispatch resolved at compile time.
2. Type-safe — access violations are compile errors, not runtime bugs.
3. Transport-agnostic — same register map, any bus.
4. Policy-based — error handling, range checking, and endianness are configurable.
5. Embedded-first — no heap, no exceptions, no RTTI.

---

## 12. write() / modify() Semantics Guide

### 12.1 Semantic Difference

| Operation | Base Value | Semantics | Safety |
|-----------|-----------|-----------|:------:|
| `write(v1, v2, ...)` | `reset_value()` | Initialize register from reset state | ✅ |
| `modify(v1, v2, ...)` | Current value (RMW) | Change specific fields, preserving others | ✅ |
| `write(single_v)` | `reset_value()` | Initialize register — other fields reset | ⚠️ |

### 12.2 Usage Rules

1. **Initialization**: Use `write()` with all relevant fields specified.
2. **Runtime change**: Use `modify()` to change specific fields.
3. **Single-field write**: `write(v)` resets other fields to `reset_value()`.
   This is intentional for single-field registers or full reset.
   Use `modify()` for runtime single-field changes.

### 12.3 W1C Fields

W1C fields must use `clear()`:

```cpp
hw.clear(OVR{});             // ✅ Correct: writes 1 to OVR only
hw.modify(OVR::Clear{});     // ✗ Compile error: W1C not ModifiableValue
hw.flip(OVR{});              // ✗ Compile error: W1C not NotW1C
```

During `modify()`, W1C bits in the parent register are automatically masked to 0
before write-back via `Register::w1c_mask`. This prevents accidental clearing
of W1C status bits during read-modify-write operations on other fields.

### 12.4 Atomicity

`modify()` performs read-modify-write and is **never atomic**.
For ISR-safe access, use `Protected<Transport, CriticalSectionPolicy>`:

```cpp
Protected<DirectTransport<>, CriticalSectionPolicy> protected_hw;

auto guard = protected_hw.lock();   // __disable_irq()
guard->modify(ConfigEnable::Set{}); // ISR-safe RMW
// ~Guard() → __enable_irq() (RAII)
```

On non-ARM platforms, use `MutexPolicy<MutexT>` or `NoLockPolicy` as appropriate.

### 12.5 reset()

`reset(Reg{})` writes the register's `reset_value()` directly. This is a pure write
(not read-modify-write) and is suitable for returning hardware to its initial state.
