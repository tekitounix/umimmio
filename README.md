# umimmio

[日本語](docs/ja/README.md)

A type-safe, zero-cost memory-mapped I/O library for C++23.
Define register maps at compile time and access them through direct MMIO, I2C, or SPI transports with the same API.

## Why umimmio

- **Safe by default** — fields only accept named `Value<>` types; raw numeric access requires opt-in
- Type-safe registers — compile-time verified access policies (RW/RO/WO)
- Zero-cost bit field operations — all dispatch resolved at compile time
- Multiple transports — Direct MMIO, I2C, SPI, and bitbang variants
- Policy-based error handling — assert, trap, ignore, or custom callback
- Compile-fail guards — illegal access is rejected at compile time

## Quick Start

```cpp
#include <umimmio/mmio.hh>
using namespace umi::mmio;

struct MyDevice : Device<RW> {
    static constexpr Addr base_address = 0x4000'0000;
};

using CTRL = Register<MyDevice, 0x00, 32>;

// 1-bit field — Set/Reset auto-generated
struct EN : Field<CTRL, 0, 1> {};

// 2-bit field with named values (safe — no raw value())
struct MODE : Field<CTRL, 1, 2> {
    using Output  = Value<MODE, 0b01>;
    using AltFunc = Value<MODE, 0b10>;
};

// 9-bit numeric field — raw value() enabled via Numeric trait
struct PLLN : Field<CTRL, 6, 9, Numeric> {};

DirectTransport<> io;
io.write(EN::Set{});            // set bit 0
io.write(EN::Reset{});          // clear bit 0
io.write(MODE::Output{});       // write named value
io.write(PLLN::value(336));     // write raw numeric (Numeric fields only)
io.write(raw<MODE>(0b11));      // escape hatch for any field
auto val = io.read(EN{});       // read bit 0 → FieldValue<EN>
auto raw_val = val.bits();      // escape hatch for raw access
io.flip(EN{});                  // toggle bit 0
```

## Build and Test

```bash
xmake test
```

## Public API

- Entrypoint: `include/umimmio/mmio.hh`
- Core: `Device`, `Register`, `Field`, `Value`, `DynamicValue`, `FieldValue`, `Numeric`, `raw<>()`
- Transports: `DirectTransport`, `I2cTransport`, `SpiTransport`, `BitBangI2cTransport`, `BitBangSpiTransport`

## Field Type Safety

| Field kind | `value()` (write) | `Value<>` types | `raw<>()` | `read()` returns |
|-----------|:---------:|:---------------:|:---------:|:----------------:|
| Default (safe) | Blocked | Yes | Yes | `FieldValue<F>` |
| `Numeric` trait | Yes | Yes | Yes | `FieldValue<F>` |
| 1-bit | — | `Set` / `Reset` auto | Yes | `FieldValue<F>` |

`FieldValue<F>` supports `==` with `Value<F,V>` and `DynamicValue<F,T>` only — raw integer comparison is a compile error. Use `.bits()` to extract the underlying value.

## Examples

- [`examples/minimal.cc`](examples/minimal.cc) — basic register and field definition
- [`examples/register_map.cc`](examples/register_map.cc) — realistic SPI peripheral register map
- [`examples/transport_mock.cc`](examples/transport_mock.cc) — RAM-backed mock transport

## Documentation

- [Design & API](docs/DESIGN.md)
- [Common Guides](../docs/INDEX.md)
- API docs: `doxygen Doxyfile` → `build/doxygen/html/index.html`

## License

MIT — See [LICENSE](../../LICENSE)
