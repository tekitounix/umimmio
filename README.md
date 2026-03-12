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

### Register::value() on registers with fields — antipatterns

When a register has defined fields, `Register::value()` bypasses all field-level type safety. Every such case has a type-safe alternative:

| Antipattern | Type-safe alternative |
|-------------|----------------------|
| `KEYR::value(0x4567'0123U)` | `Value<KEYR, 0x4567'0123U>` — named constant |
| `PLLCFGR::value((m<<0)\|(n<<6))` | Individual fields via `write()` / `modify()` |
| `ISER::value(1U << irq_num)` | Typed IRQ enum + template or switch dispatch |
| `MODER::value(手動RMW)` | `modify()` with per-pin field |

For runtime-to-compile-time dispatch, use explicit pattern matching:

```cpp
// ❌ Bypasses type safety
io.write(NVIC::ISER<0>::value(1U << bit_pos));

// ✅ Type-safe dispatch
switch (bit_pos) {
case 0:  io.write(NVIC::ISER<0>::IRQ0::Set{});  break;
case 1:  io.write(NVIC::ISER<0>::IRQ1::Set{});  break;
// ...
}

// ✅ Best — compile-time index
template <std::uint32_t IrqNum>
void enable_irq() {
    io.write(NVIC::ISER<IrqNum / 32>::IRQ<IrqNum % 32>::Set{});
}
```

> **Note**: Rust's svd2rust marks the equivalent `w.bits(n)` as `unsafe`.
> umimmio's `Register::value()` is not `unsafe` in C++ terms, but should be treated with equivalent caution — it exists for incremental migration, not as a recommended API.

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

## Field Type Summary

| Field kind | `value()` | `bits()` | Named `Value<>` | Auto aliases | `modify()` | `clear()` |
|-----------|:---------:|:--------:|:---------------:|:------------:|:----------:|:---------:|
| Default (safe) | Blocked | Blocked | Required | — | Yes | — |
| `Numeric` | Yes (unsigned) | Yes | Optional | — | Yes | — |
| 1-bit RW | — | Blocked | Optional (custom aliases) | `Set` / `Reset` | Yes | — |
| 1-bit W1C | — | Blocked | — | `Clear` | Compile error | Yes |

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

- [Design & API](docs/design.md)
- [Testing](tests/testing.md)

## License

MIT — See [LICENSE](LICENSE)
