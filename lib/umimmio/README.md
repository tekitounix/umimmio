# umimmio

[日本語](docs/ja/README.md)

A type-safe, zero-cost memory-mapped I/O library for C++23.
Define register maps at compile time and access them through direct MMIO, I2C, or SPI transports with the same API.

## Why umimmio

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
using EN   = Field<CTRL, 0, 1>;

DirectTransport<> io;
io.write(EN::Set{});          // set bit 0
auto val = io.read(EN{});     // read bit 0
io.flip(EN{});                // toggle bit 0
```

## Build and Test

```bash
xmake test
```

## Public API

- Entrypoint: `include/umimmio/mmio.hh`
- Core: `Region`, `Field`, `Value`, `Block`, `DirectTransport`
- Transports: `I2cTransport`, `SpiTransport`, `BitBangI2cTransport`, `BitBangSpiTransport`

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
