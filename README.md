# umimmio

[日本語](docs/readme.ja.md)

A type-safe, zero-cost memory-mapped I/O library for C++23.
Define register maps at compile time and access them through direct MMIO, I2C, or SPI transports with the same API.

## The Problem

Traditional C/C++ vendor headers (CMSIS, ESP-IDF, Pico SDK) expose registers as raw `uint32_t` pointers and bit-mask macros. This allows bugs that pass compilation silently:

```c
USART1->SR |= USART_CR1_UE;        // Wrong register — compiles fine
GPIOA->ODR |= USART_CR1_UE;        // Wrong peripheral — compiles fine
USART1->SR = 0;                     // Write to RO bits — compiles fine
```

umimmio eliminates these classes of bugs through compile-time type enforcement:

| Safety check | Vendor CMSIS | umimmio |
|-------------|:----------:|:-------:|
| Cross-register bit-mask misuse | ❌ | ✅ Compile error |
| Cross-peripheral bit-mask misuse | ❌ | ✅ Compile error |
| Write to read-only register | ❌ | ✅ Compile error |
| Read from write-only register | ❌ | ✅ Compile error |
| Field width range check | ❌ | ✅ `if consteval` + runtime policy |
| Named value type safety | ❌ (macros) | ✅ NTTP `Value<F, V>` |
| W1C (Write-1-to-Clear) safety | ❌ | ✅ Compile error on `modify()` |

## Type Hierarchy

umimmio models hardware as a four-level nested struct hierarchy.
The nesting enforces that values belong to a specific field, fields to a specific register — cross-region misuse is a compile error.

```
Device > Register > Field > Value
```

```cpp
#include <umimmio/mmio.hh>
using namespace umi::mmio;

struct MyDevice : Device<RW> {
    static constexpr Addr base_address = 0x4000'0000;

    struct CTRL : Register<MyDevice, 0x00, 32> {
        // 1-bit field — Set/Reset auto-generated
        struct EN : Field<CTRL, 0, 1> {};

        // 1-bit field with domain-specific aliases — Set/Reset still available
        struct DIR : Field<CTRL, 3, 1> {
            using Transmit = Value<DIR, 1>;
            using Receive  = Value<DIR, 0>;
        };

        // Multi-bit field with named values (safe — no raw value())
        struct MODE : Field<CTRL, 1, 2> {
            using Output  = Value<MODE, 0b01>;
            using AltFunc = Value<MODE, 0b10>;
        };

        // Numeric field — opt-in for raw value()/bits(). Use ONLY when the number
        // itself is the data (counters, divisors, addresses). Most fields should
        // use named Value<> instead.
        struct DIV : Field<CTRL, 6, 9, Numeric> {};
    };

    // Write-only register with named values for unlock keys
    struct KEYR : Register<MyDevice, 0x04, 32, WO> {
        using Key1 = Value<KEYR, 0x4567'0123U>;
        using Key2 = Value<KEYR, 0xCDEF'89ABU>;
    };
};
```

### Template Parameters

**Device**`<Access = RW, AllowedTransports... = Direct>` — top-level peripheral. Requires `static constexpr Addr base_address`.

**Register**`<Parent, Offset, Bits, Access = RW, Reset = 0, W1cMask = 0>` — bit region at byte offset from parent.

**Field**`<Reg, BitOffset, BitWidth, Traits...>` — bit range within a register. Traits: access policy (`RO`, `WO`, `W1C`) and/or `Numeric`, in any order. `Numeric` enables raw `value()`/`bits()` — appropriate only for counters, divisors, addresses, and similar data-valued fields. Mode selectors, configuration bits, and other finite identifiers must use named `Value<>` instead.

**Value**`<RegionT, EnumValue>` — compile-time named constant. Defined as `using` alias inside Field or Register.

## Operations

All operations are called on a transport instance.

### write() vs modify() — the critical distinction

```cpp
DirectTransport<> io;
using CTRL = MyDevice::CTRL;

// write() — builds value from reset_value, applies specified fields, writes register.
//           Unspecified fields revert to reset_value. Use for INITIALIZATION.
io.write(CTRL::EN::Set{}, CTRL::MODE::Output{});

// modify() — reads current value, applies changes, writes back.
//            Preserves all unspecified fields. Use for RUNTIME CHANGES.
io.modify(CTRL::EN::Set{});           // only EN changes, MODE and DIV preserved
io.modify(CTRL::DIV::value(336));     // Numeric field
```

**Common mistake**: `io.write(EN::Set{})` at runtime reverts MODE and DIV to their reset values. Use `modify()`.

### Reading

```cpp
auto val = io.read(CTRL::EN{});             // → RegionValue<EN>
bool is_out = io.is(CTRL::MODE::Output{});  // direct comparison

// RegionValue — one bus read, multiple field extractions
auto cfg = io.read(CTRL{});                 // → RegionValue<CTRL>
auto en  = cfg.get(CTRL::EN{});             // no additional bus access
bool fast = cfg.is(CTRL::MODE::AltFunc{});
auto raw  = cfg.bits();                     // register-level: always available
```

`RegionValue` supports `==` with `Value` and `DynamicValue` only — raw integer comparison is a compile error. `bits()` requires `Numeric` for fields (symmetric with write-side `value()`); always available on registers.

### Other operations

| Operation | Description |
|-----------|-------------|
| `flip(Field{})` | Toggle 1-bit RW field (read-modify-write) |
| `clear(Field{})` | Clear W1C field by writing 1 to its bit position |
| `reset(Reg{})` | Write compile-time reset value to register |
| `read_variant<F, V1, V2, ...>(F{})` | Pattern matching via `std::variant` + `std::visit` |

## Common Pitfalls

| Mistake | What happens | Correct API |
|---------|-------------|-------------|
| `io.write(EN::Set{})` at runtime | Unspecified fields revert to reset value — silently destroys other settings | `io.modify(EN::Set{})` — preserves all other fields |
| `Field<R, 0, 4, Numeric>` on mode/selector fields | Enables `value(N)` / `bits()` — bypasses named-value type safety | Omit `Numeric`, define `Value<F, N>` aliases |
| `io.modify(W1C_Flag::Clear{})` | Compile error — `modify()` cannot clear W1C fields | `io.clear(W1C_Flag{})` |
| `io.read(ModeField{}).bits()` | Compile error — `bits()` blocked on non-Numeric fields | `io.is(ModeField::Expected{})` or `io.read_variant(...)` |

**Rule of thumb**: `write()` for initialization, `modify()` for runtime changes. Named `Value<>` for identifiers, `Numeric` only when the number itself is the data.

## Design Intent: value() and Numeric

### The type safety hierarchy

```
              Write                                   Read
              ─────                                   ────
Most safe     Named Value<>                           is() / read_variant()
              io.modify(MODE::Output{})               io.is(MODE::Output{})

              1-bit Set/Reset                         (same)
              io.modify(EN::Set{})                    io.is(EN::Set{})

              Field::value() [Numeric]                .bits() [Numeric field]
              io.modify(DIV::value(336))              io.read(DIV{}).bits()

              Register::value() [data register]       .bits() [data register]
              io.write(DR::value(tx_byte))            io.read(DR{}).bits()
```

`Register::value()` has two distinct roles:

- **Data registers** (SPI DR, USART DR) — the entire register is a single numeric value with no field structure. `Register::value()` is the normal API here, not an escape hatch.
- **Registers with fields** (PLLCFGR, MODER, CR1) — `Register::value()` bypasses field-level type safety. Every such use has a type-safe alternative (see below).

### Named Value — the default

Most register fields have a finite set of values with specific hardware meanings. These **must** use named `Value<>` types — including magic constants:

```cpp
// Unlock keys — finite constants with specific meaning, NOT arbitrary numbers
struct KEYR : Register<FLASH, 0x04, 32, WO> {
    using Key1 = Value<KEYR, 0x4567'0123U>;
    using Key2 = Value<KEYR, 0xCDEF'89ABU>;
};
io.write(FLASH::KEYR::Key1{});

// Mode selectors — finite set of hardware behaviors
struct MODER0 : Field<MODER, 0, 2> {
    using Input     = Value<MODER0, 0>;
    using Output    = Value<MODER0, 1>;
    using Alternate = Value<MODER0, 2>;
    using Analog    = Value<MODER0, 3>;
};
io.modify(MODER0::Output{});  // self-documenting, type-safe

// GPIO alternate function — MCU-specific peripheral mappings
struct AFR0 : Field<AFRL, 0, 4> {
    using System = Value<AFR0, 0>;   // AF0
    using I2C    = Value<AFR0, 4>;   // AF4: I2C1..I2C3
    using USART  = Value<AFR0, 7>;   // AF7: USART1..USART3
};
io.modify(AFR0::USART{});  // intent is clear
```

### Field::value() with Numeric — for data, not identifiers

`Numeric` is the **explicit opt-in** that enables `Field::value()`. Appropriate **only** when:

- The number **is** the data — counter reload values, baud rate divisors, DMA addresses, transfer counts
- The value range is wide and individual values carry no distinct semantic meaning

```cpp
struct RELOAD : Field<LOAD, 0, 24, Numeric> {};   // 24-bit counter value
struct BRR    : Field<USART, 0, 16, Numeric> {};   // baud rate divisor
struct MAR    : Field<DMA_S0, 0, 32, Numeric> {};  // memory address
struct NDT    : Field<DMA_S0, 0, 16, Numeric> {};  // transfer count
```

**Numeric is wrong** when values are finite identifiers:

```cpp
// ❌ AF numbers are peripheral mappings, not arbitrary numbers
struct AFR0 : Field<AFRL, 0, 4, Numeric> {};
io.modify(AFR0::value(7U));  // What is 7? Opaque, error-prone
```

### Register::value() — when it's correct and when it's not

`Register::value()` is **the intended API** for data registers and bitmap registers:

- **Data registers** (SPI DR, USART DR, ADC DR) — the entire register is a single numeric value with no field structure.
- **Bitmap registers** (NVIC ISER, GPIO BSRR, EXTI IMR/PR) — each bit is an independent control target (IRQ, pin, interrupt line). Per-bit Named Value definitions would be disproportionately expensive for the type safety gained.

```cpp
// Data register — Register::value() is the normal API
io.write(SPI::DR::value(tx_byte));

// Bitmap register — Register::value() is practical
io.write(NVIC::ISER<irq_num / 32>::value(1U << (irq_num % 32)));

// With compile-time dispatch for register index
dispatch<8>(reg_idx, [&]<std::size_t I>() {
    io.write(NVIC::ISER<I>::value(1U << bit_pos));
});
```

For **registers with structured fields** (PLLCFGR, MODER, CR1), `Register::value()` bypasses field-level type safety and should be avoided:

| Antipattern | Type-safe alternative |
|-------------|----------------------|
| `PLLCFGR::value((m<<0)\|(n<<6))` | Individual fields via `write()` / `modify()` |
| `MODER::value(manual_rmw)` | `modify()` with per-pin field |

> **Note**: Rust's svd2rust marks the equivalent `w.bits(n)` as `unsafe`.
> umimmio's `Register::value()` is not `unsafe` in C++ terms.
> For data registers and bitmap registers, it is the intended API.
> For registers with structured fields, it bypasses field-level type safety
> and should be treated with equivalent caution to svd2rust's `unsafe` `.bits()`.

## W1C (Write-1-to-Clear) Fields

W1C fields are common in status/interrupt registers. Define with three steps:

```cpp
struct SR : Register<MyDevice, 0x08, 32, RW, 0, /*W1cMask=*/0x03> {
    struct OVR : Field<SR, 0, 1, W1C> {};    // Clear auto-generated
    struct EOC : Field<SR, 1, 1, W1C> {};    // Clear auto-generated
    struct EN  : Field<SR, 8, 1> {};          // normal RW
};

io.clear(SR::OVR{});             // clears OVR without touching EOC or EN
io.modify(SR::EN::Set{});        // safe: W1C bits auto-masked to 0
// io.modify(SR::OVR::Clear{});  // compile error — use clear() for W1C
```

The `W1cMask` ensures that `modify()` and `flip()` automatically zero W1C bit positions before write-back, preventing accidental flag clearing.

## W1S / W1T (Write-1-to-Set / Write-1-to-Toggle) Fields

W1S and W1T fields provide atomic bit manipulation without read-modify-write.
They are defined as write-only (`WO`) — hardware provides separate set/toggle ports.

```cpp
// RP2040 SIO — separate WO registers for atomic GPIO operations
struct SIO : Device<> {
    static constexpr Addr base_address = 0xD000'0000;
    struct GPIO_OUT     : Register<SIO, 0x010, 32, RW>  {};
    struct GPIO_OUT_SET : Register<SIO, 0x014, 32, W1S> {};
    struct GPIO_OUT_CLR : Register<SIO, 0x018, 32, W1C> {};
    struct GPIO_OUT_XOR : Register<SIO, 0x01C, 32, W1T> {};
};

io.write(SIO::GPIO_OUT_SET::value(pin_mask));  // atomic set
io.write(SIO::GPIO_OUT_XOR::value(pin_mask));  // atomic toggle
```

1-bit W1S fields get `Set`/`Reset` aliases (same as normal RW — "write 1 to set" matches).
1-bit W1T fields get a `Toggle` alias instead.

`modify()` and `flip()` are compile errors on W1S/W1T fields — read-modify-write is unsafe or meaningless for special write behaviors.

## Field Type Summary

| Field kind | `value()` | `bits()` | Named `Value<>` | Auto aliases | `modify()` | `clear()` |
|-----------|:---------:|:--------:|:---------------:|:------------:|:----------:|:---------:|
| Default (safe) | Blocked | Blocked | Required | — | Yes | — |
| `Numeric` | Yes (unsigned) | Yes | Optional | — | Yes | — |
| 1-bit RW | — | Blocked | Optional (custom aliases) | `Set` / `Reset` | Yes | — |
| 1-bit W1C | — | Blocked | — | `Clear` | Compile error | Yes |
| 1-bit W1S | — | Blocked | — | `Set` / `Reset` | Compile error | — |
| 1-bit W1T | — | Blocked | — | `Toggle` | Compile error | — |

## Register Arrays and Runtime Dispatch

umimmio provides three primitives for working with indexed register banks (NVIC ISER[0..7], DMA streams, etc.):

### RegisterArray — compile-time array metadata

```cpp
// Template register bank: ISER<0>, ISER<1>, ..., ISER<7>
template <std::size_t N>
struct ISER : Register<NVIC, 0x100 + (N * 4), 32> {
    static_assert(N < 8);
};

using ISERArray = RegisterArray<ISER, 8>;
static_assert(ISERArray::size == 8);
// ISERArray::Element<3> is ISER<3>
```

### dispatch / dispatch_r — runtime-to-compile-time bridge

Converts a runtime index to a compile-time template parameter via fold expression:

```cpp
// void dispatch — no return value
std::size_t stream = get_active_stream();  // runtime
dispatch<8>(stream, [&]<std::size_t I>() {
    io.modify(DMA::Stream<I>::CR::EN::Set{});
});

// dispatch_r — with return value
auto count = dispatch_r<8, std::uint32_t>(stream, [&]<std::size_t I>() {
    return io.read(DMA::Stream<I>::NDTR{}).bits();
});
```

Out-of-range index invokes `ErrorPolicy::on_range_error()` (default: assert).

### IndexedArray — sub-register granularity

For register arrays where each entry is smaller than a register (e.g., 8-bit entries in a 32-bit register):

```cpp
// 32 bytes at offset 0x100 from device base, 8-bit entries, stride 1
using LUT = IndexedArray<MyDevice, 0x100, 32>;

// Compile-time access via Entry<N>
io.write(LUT::Entry<5>::value(0x42));

// Runtime access via write_entry/read_entry (Direct transport only)
LUT::write_entry(idx, 0x42);
auto val = LUT::read_entry(idx);

// Custom entry width and stride
using WideArray = IndexedArray<MyDevice, 0x200, 16, bits16, 4>;  // 16-bit entries, 4-byte stride
```

`write_entry`/`read_entry` require `Direct` in the device's `AllowedTransports` (compile-time enforced via `static_assert`).

## Transport Selection

Transport is selected by constructing the appropriate type:

```cpp
umi::mmio::DirectTransport<> direct;                    // Volatile pointer (Cortex-M, etc.)
umi::mmio::I2cTransport<MyI2c> i2c(hal_i2c, 0x68);     // HAL I2C
umi::mmio::SpiTransport<MySpi> spi(hal_spi);            // HAL SPI
```

All transports expose the same `write()`, `read()`, `modify()`, `is()`, `flip()`, `clear()`, `reset()` API.
The Device's `AllowedTransports...` parameter makes unauthorized transport usage a compile error.

### CsrTransport — RISC-V CSR Access

CSR (Control and Status Register) access uses the same type-safe API via a dedicated transport:

```cpp
#include <umimmio/transport/csr.hh>

// Device definition — CSR numbers as register offsets (base_address = 0)
struct RiscvMachine : Device<RW, Csr> {
    static constexpr Addr base_address = 0;
    struct MSTATUS : Register<RiscvMachine, 0x300, 32> {
        struct MIE : Field<MSTATUS, 3, 1> {};
        struct MPP : Field<MSTATUS, 11, 2> {
            using MACHINE = Value<MPP, 3>;
        };
    };
};

// On RISC-V targets, use DefaultCsrAccessor (inline asm csrr/csrw)
// For testing, inject a MockCsrAccessor that satisfies CsrAccessor concept
CsrTransport<MockCsrAccessor> csr;
csr.modify(RiscvMachine::MSTATUS::MIE::Set{});
```

`CsrAccessor` concept requires `csr_read<CsrNum>()` and `csr_write<CsrNum>(value)` — CSR number as compile-time template parameter (12-bit immediate constraint).

### AtomicDirectTransport — Write-Only Alias Registers

For MCUs with atomic register aliases (e.g., RP2040 SET/CLR/XOR), `AtomicDirectTransport` adds a fixed offset to all writes:

```cpp
#include <umimmio/transport/atomic_direct.hh>

// RP2040: SET alias at +0x2000, CLR alias at +0x3000, XOR alias at +0x1000
AtomicDirectTransport<0x2000> gpio_set;   // write() targets reg + 0x2000
AtomicDirectTransport<0x3000> gpio_clr;   // write() targets reg + 0x3000

gpio_set.write(GPIO::OUT::Pin5::Set{});   // atomic bit set via alias
gpio_clr.write(GPIO::OUT::Pin5::Set{});   // atomic bit clear via alias
```

This is a **write-only** transport — `read()`, `modify()`, `flip()`, and `is()` are compile errors because no `reg_read()` is provided. `write()` and `reset()` work normally.

## Error Handling

### Compile-Time Errors

Access policy violations, value range overflow, and type mismatches are all compile errors.
The design minimizes the categories of bugs that can reach runtime.

### Runtime Error Policies

Select behavior via the `ErrorPolicy` template parameter:

| Policy | Behavior |
|--------|----------|
| `AssertOnError` | `assert(false && msg)` (default) |
| `TrapOnError` | `std::abort()` |
| `IgnoreError` | Silent no-op |
| `CustomErrorHandler<fn>` | User callback |

Two entry points:

| Entry Point | Triggered By |
|---|---|
| `on_range_error(msg)` | Value out-of-range for field width (programming error) |
| `on_transport_error(msg)` | HAL driver reports failure (bus error, NACK, timeout) |

I2cTransport and SpiTransport use `if constexpr` to check the HAL return type, invoking `on_transport_error()` when a falsy value is returned.

## Concurrency

`modify()` performs read-modify-write and is **never atomic**.
For ISR-safe access, the caller must serialize access externally:

```cpp
{
    auto lock = enter_critical_section();  // platform-specific
    io.modify(ConfigEnable::Set{});        // ISR-safe RMW
}   // lock released (RAII)
```

umimmio is transport-level only and does not provide synchronization primitives.
Callers are responsible for choosing the appropriate locking mechanism.

## Features

- **Safe by default** — fields only accept named `Value<>` types; raw numeric access requires opt-in via `Numeric`
- **Zero-cost** — all dispatch resolved at compile time, no vtable, no heap
- **Multiple transports** — same register map works across `DirectTransport`, `I2cTransport`, `SpiTransport`
- **Policy-based error handling** — `AssertOnError`, `TrapOnError`, `IgnoreError`, `CustomErrorHandler`
- **C++23** — deducing this (no CRTP), `if consteval`, `std::byteswap`

## Build and Test

```bash
xmake test 'test_umimmio/*'
```

## Documentation

- [Design](docs/design.md) — Architecture and design decisions
- [Testing](tests/testing.md) — Test strategy and quality gates

## License

MIT — See [LICENSE](LICENSE)
