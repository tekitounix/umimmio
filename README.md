# umimmio

[日本語](docs/readme.ja.md)

> [!CAUTION]
> **Experimental — Do not depend on this repository.**
>
> This library is published as a git subtree on an experimental basis.
> The API, structure, and contents may change without notice, and the repository may be made private or removed entirely in the future.
> Do not add this repository as a dependency in your projects.
>
> Additionally, some test dependencies are not publicly available, so tests cannot be fully built or run from this repository alone.

A type-safe, zero-cost memory-mapped I/O library for C++23.
Define register maps at compile time and access them through direct MMIO, I2C, or SPI transports with the same API.

## The Problem

Traditional C/C++ vendor headers (CMSIS, ESP-IDF, Pico SDK) expose registers as raw `uint32_t` pointers and bit-mask macros. This allows bugs that pass compilation silently:

```c
USART1->SR |= USART_CR1_UE;        // Wrong register — compiles fine
GPIOA->ODR |= USART_CR1_UE;        // Wrong peripheral — compiles fine
USART1->SR = 0;                     // Write to RO bits — compiles fine
```

## How umimmio Solves It

| Safety check | Vendor CMSIS | umimmio |
|-------------|:----------:|:-------:|
| Cross-register bit-mask misuse | ❌ | ✅ Type-enforced |
| Cross-peripheral bit-mask misuse | ❌ | ✅ Type-enforced |
| Write to read-only register | ❌ | ✅ Compile error |
| Read from write-only register | ❌ | ✅ Compile error |
| Field width range check | ❌ | ✅ `if consteval` + runtime policy |
| Named value type safety | ❌ (macros) | ✅ NTTP `Value<F, V>` |
| W1C (Write-1-to-Clear) safety | ❌ | ✅ Field-level `W1C` policy |

## Features

- **Safe by default** — fields only accept named `Value<>` types; raw numeric access requires opt-in via `Numeric` trait
- **Type-safe registers** — compile-time verified access policies (RW/RO/WO/W1C)
- **Zero-cost** — all dispatch resolved at compile time, no vtable, no heap
- **Multiple transports** — same register map works across Direct MMIO, I2C, SPI
- **Policy-based error handling** — `AssertOnError`, `TrapOnError`, `IgnoreError`, `CustomErrorHandler`
- **Compile-fail guards** — 15 compile-fail tests verify illegal access is rejected at compile time
- **RegionValue** — single bus read, multiple field extraction via `read(Reg{}).get(Field{})`
- **Pattern matching** — `read_variant()` with `std::variant` + `std::visit` for exhaustive field matching
- **Concurrency** — transport operations are not atomic; callers serialize access via platform-specific critical sections
- **C++23** — deducing this (no CRTP), `if consteval`, `std::byteswap`

## Quick Start

```cpp
#include <umimmio/mmio.hh>
using namespace umi::mmio;

// Hierarchical register map — the nested struct hierarchy
// Device > Register > Field > Value mirrors the physical device structure.
struct MyDevice : Device<RW> {
    static constexpr Addr base_address = 0x4000'0000;

    struct CTRL : Register<MyDevice, 0x00, 32> {
        // 1-bit field — Set/Reset auto-generated
        struct EN : Field<CTRL, 0, 1> {};

        // 2-bit field with named values (safe — no raw value())
        struct MODE : Field<CTRL, 1, 2> {
            using Output  = Value<MODE, 0b01>;
            using AltFunc = Value<MODE, 0b10>;
        };

        // 9-bit numeric field — raw value() enabled via Numeric trait
        struct PLLN : Field<CTRL, 6, 9, Numeric> {};
    };
};

using CTRL = MyDevice::CTRL;  // alias for brevity

DirectTransport<> io;
io.write(CTRL::EN::Set{});            // set bit 0
io.write(CTRL::EN::Reset{});          // clear bit 0
io.write(CTRL::MODE::Output{});       // write named value
io.write(CTRL::PLLN::value(336));     // write raw numeric (Numeric fields only)
io.modify(CTRL::EN::Set{});           // read-modify-write (preserves other fields)

// Reading
auto val = io.read(CTRL::EN{});       // → RegionValue<EN>
auto raw = val.bits();                // escape hatch for raw access
bool is_out = io.is(CTRL::MODE::Output{});  // named value comparison
io.flip(CTRL::EN{});                  // toggle 1-bit field

// RegionValue — one bus read, multiple field access
auto cfg = io.read(CTRL{});           // → RegionValue<CTRL>
auto en  = cfg.get(CTRL::EN{});       // extract field — no additional bus access
bool fast = cfg.is(CTRL::MODE::AltFunc{});
```

## Field Type Safety

| Field kind | `value()` (write) | `Value<>` types | `read()` returns |
|-----------|:---------:|:---------------:|:----------------:|
| Default (safe) | Blocked | Yes | `RegionValue<F>` |
| `Numeric` trait | Yes (unsigned only) | Yes | `RegionValue<F>` |
| 1-bit RW | — | `Set` / `Reset` auto | `RegionValue<F>` |
| 1-bit W1C | — | `Clear` auto | `RegionValue<F>` |

`RegionValue<F>` supports `==` with `Value<F,V>` and `DynamicValue<F,T>` only — raw integer comparison is a compile error. Use `.bits()` to extract the underlying value.

## Build and Test

```bash
xmake test 'test_umimmio/*'
```

## Public API

- Entrypoint: `include/umimmio/mmio.hh`
- Core: `Device`, `Register`, `Field`, `Value`, `DynamicValue`, `RegionValue`, `Numeric`
- Operations: `read()`, `write()`, `modify()`, `is()`, `flip()`, `clear()`, `reset()`, `read_variant()`
- Transports: `DirectTransport`, `I2cTransport`, `SpiTransport`
- Concurrency: not provided — callers use platform-specific locking (see Design §9.4)
- Error policies: `AssertOnError`, `TrapOnError`, `IgnoreError`, `CustomErrorHandler`

## Examples

- [`examples/minimal.cc`](examples/minimal.cc) — basic register and field definition
- [`examples/register_map.cc`](examples/register_map.cc) — realistic SPI peripheral register map
- [`examples/transport_mock.cc`](examples/transport_mock.cc) — RAM-backed mock transport

## Documentation

- [Design & API](docs/design.md)
- [Testing](tests/testing.md)

## License

MIT — See [LICENSE](LICENSE)
