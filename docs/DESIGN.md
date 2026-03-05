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

Reading a write-only register or writing a read-only register is a compile error:

```cpp
static_assert(Reg::AccessType::can_read, "Cannot read WO register");
```

### 2.3 Transport Abstraction

Register operations are decoupled from bus protocol.
The same `Device/Block/Register/Field` hierarchy works with:

1. Direct volatile pointer (Cortex-M, RISC-V memory-mapped peripherals).
2. I2C bus (HAL-based or bit-bang).
3. SPI bus (HAL-based or bit-bang).

Transport is a template parameter, not a base-class pointer.

### 2.4 Range Checking

Field values are range-checked at compile time when possible:

1. `value()` with a literal exceeding field width triggers `mmio_compile_time_error_value_out_of_range`.
2. Runtime `DynamicValue` is checked by `CheckPolicy` + `ErrorPolicy` at write/modify time.

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
│   └── ja/
├── examples/
│   ├── minimal.cc
│   ├── register_map.cc
│   └── transport_mock.cc
├── include/umimmio/
│   ├── mmio.hh              # Umbrella header
│   ├── register.hh          # Core: RegOps, ByteAdapter, BitRegion, Field, Value
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
    ├── compile_fail/
    │   ├── read_wo.cc
    │   └── write_ro.cc
    └── xmake.lua
```

---

## 4. Growth Layout

```text
lib/umimmio/
├── include/umimmio/
│   ├── mmio.hh
│   ├── register.hh
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
    │   ├── read_wo.cc
    │   └── write_ro.cc
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
| `Device<Access, Transports...>` | Device root with base address, access policy, and allowed transports |
| `Register<Device, Offset, Bits, Access, Reset>` | Register at an offset within a device |
| `Field<Reg, BitOffset, BitWidth, ...Traits>` | Bit field within a register (variadic traits) |
| `Value<Field, val>` | Named constant for a Field |
| `DynamicValue<Field, T>` | Runtime value for a Field |
| `Numeric` | Trait: enables raw `value()` on a Field |
| `raw<Field>(val)` | Escape hatch: raw value for any Field |

Transport types:

| Transport | Use Case |
|-----------|----------|
| `DirectTransport` | Memory-mapped I/O (volatile pointer access) |
| `I2cTransport` | HAL-compatible I2C peripheral drivers |
| `SpiTransport` | HAL-compatible SPI peripheral drivers |
| `BitBangI2cTransport` | Software I2C via GPIO |
| `BitBangSpiTransport` | Software SPI via GPIO |

Access policies:

| Policy | `read()` | `write()` | `modify()` |
|--------|:--------:|:---------:|:----------:|
| `RW` | Yes | Yes | Yes |
| `RO` | Yes | No | No |
| `WO` | No | Yes | No |

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
```

### 5.2.1 Field Type Safety Model

Fields are **safe by default**: only named `Value<>` types and the `raw<>()` escape hatch are accepted.
The `Numeric` trait opts a field into raw `value()` access.

| Field kind | `value()` | `Value<>` types | `raw<>()`  |
|-----------|:---------:|:---------------:|:----------:|
| Default (safe) | Blocked | Yes | Yes |
| With `Numeric` | Yes | Yes | Yes |
| 1-bit | — | `Set` / `Reset` auto | Yes |

**`Field<Reg, BitOffset, BitWidth, ...Traits>`** — variadic traits pattern:
- Traits can include access policy (`RW`, `RO`, `WO`) and/or `Numeric`, in any order.
- Default access is `Inherit` (from parent register).
- 1-bit fields automatically provide `Set` and `Reset` type aliases via CRTP.

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

All transports expose the same `write()`, `read()`, `modify()`, `is()`, `flip()` API.

### 5.4 Advanced Path

Advanced usage includes:

1. multi-field write in a single bus transaction,
2. read-modify-write via `modify()`,
3. custom error policies (trap, ignore, callback),
4. 16-bit address space for I2C/SPI devices,
5. configurable address and data endianness.

---

## 6. Core Abstraction Hierarchy

### 6.1 BitRegion

Unified compile-time base for both registers and fields:

- `Register` = `BitRegion` with `IsRegister=true` (full-width, has address offset).
- `Field` = `BitRegion` with `IsRegister=false` (sub-width, has bit offset).

### 6.2 RegOps (CRTP Base)

Provides type-safe `write()`, `read()`, `modify()`, `is()`, `flip()` methods.
Delegates to `Derived::reg_read()` / `Derived::reg_write()` for actual bus I/O.

### 6.3 ByteAdapter (CRTP Bridge)

Converts RegOps' typed register operations into `raw_read()` / `raw_write()` byte operations.
Handles endian conversion between host CPU and wire format.

### 6.4 Value and DynamicValue

- `Value<Field, EnumValue>`: compile-time constant with shifted representation.
- `DynamicValue<Field, T>`: runtime value with deferred range check.

### 6.5 Field Trait System

`Field<Reg, BitOffset, BitWidth, ...Traits>` uses a variadic parameter pack for traits:

- **Access policy extraction**: `detail::ExtractAccess_t<Traits...>` finds `RW`/`RO`/`WO` in the pack (default: `Inherit`).
- **Numeric detection**: `detail::contains_v<Numeric, Traits...>` enables `value()`.
- **OneBitAliases**: 1-bit fields inherit `Set`/`Reset` via `detail::OneBitBase<Field, Width>` (CRTP).

Traits can appear in any order:
```cpp
// All equivalent for access:
struct A : mm::Field<REG, 0, 8, mm::RO, mm::Numeric> {};   // RO + Numeric
struct B : mm::Field<REG, 0, 8, mm::Numeric, mm::RO> {};   // same
struct C : mm::Field<REG, 0, 8, mm::Numeric> {};           // Inherit + Numeric
struct D : mm::Field<REG, 0, 8> {};                        // Inherit, safe
```

---

## 7. Error Handling Model

### 7.1 Compile-Time Errors

1. Access policy violations → `static_assert` failure.
2. Value out of range in `consteval` context → `mmio_compile_time_error_value_out_of_range`.
3. `value()` on non-Numeric field → concept constraint failure (`requires(is_numeric)`).
4. Transport not allowed for device → `static_assert` failure.

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

1. Tests split by concern: access policy, register/field, transport.
2. Compile-fail tests verify API contract enforcement (`read_wo.cc`, `write_ro.cc`).
3. Transport tests use RAM-backed mock implementing `raw_read` / `raw_write`.
4. Hardware-level MMIO tests require actual hardware and are out of scope for host tests.
5. CI runs host tests and compile-fail checks.

### 8.1 Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_access_policy.cc`: RW/RO/WO policy enforcement
- `tests/test_register_field.cc`: BitRegion, Register, Field, Value, mask/shift correctness
- `tests/test_transport.cc`: RAM-backed mock transport for read/write/modify/is/flip
- `tests/compile_fail/read_wo.cc`: compile-fail guard — reading a write-only register
- `tests/compile_fail/write_ro.cc`: compile-fail guard — writing a read-only register

### 8.2 Running Tests

```bash
xmake test                              # all targets
xmake test 'test_umimmio/*'             # host only
xmake test 'test_umimmio_compile_fail/*'  # compile-fail only
```

### 8.3 Quality Gates

- All host tests pass
- Compile-fail contract tests pass (read_wo, write_ro)
- Transport mock tests cover single and multi-field write, modify, is, flip
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
   would produce clear concept-mismatch errors instead of deep template failures:

   ```cpp
   // Current: unconstrained — errors appear deep in raw_read/raw_write
   template <typename I2C, ...>
   class I2cTransport : public ByteAdapter<...> { ... };

   // Improved: concept-constrained — early, clear error at instantiation
   template <umi::hal::I2cTransport I2C, ...>
   class I2cTransport : public ByteAdapter<...> { ... };
   ```

   **Prerequisite:** The `umi::hal::I2cTransport` concept signature
   (`write(addr, reg, tx)`) does not match what `mmio::I2cTransport`
   actually calls (`write(addr, payload)` without separate `reg`).
   The HAL concept must be aligned first (see `03_ARCHITECTURE.md` §10.3).

6. **Resolve naming collision between `umi::hal` and `umi::mmio` Transport types.**
   Both namespaces define `I2cTransport` and `SpiTransport`, but at different
   abstraction levels:

   | Name | Namespace | Kind | Abstraction |
   |------|-----------|------|-------------|
   | `I2cTransport` | `umi::hal` | concept | Bus-level: send/receive bytes |
   | `I2cTransport` | `umi::mmio` | class | Register-level: address framing + endian conversion |

   Consider renaming the mmio class to `I2cRegisterBus` or `I2cRegisterTransport`
   to eliminate ambiguity. The mmio Transport *consumes* the HAL concept (takes
   a HAL-conformant type as template parameter), so they are complementary,
   not competing abstractions.

---

## 11. Design Principles

1. Zero-cost abstraction — all dispatch resolved at compile time.
2. Type-safe — access violations are compile errors, not runtime bugs.
3. Transport-agnostic — same register map, any bus.
4. Policy-based — error handling, range checking, and endianness are configurable.
5. Embedded-first — no heap, no exceptions, no RTTI.
