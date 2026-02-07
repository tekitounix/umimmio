# umimmio

English | [日本語](docs/ja/README.md)

`umimmio` is a type-safe, zero-cost memory-mapped I/O library for C++23.
It lets you define register maps at compile time and access them through direct MMIO, I2C, or SPI transports with the same API.

## Release Status

- Current version: `0.1.0`
- Stability: initial release
- Versioning policy: [`RELEASE.md`](RELEASE.md)
- Changelog: [`CHANGELOG.md`](CHANGELOG.md)

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
xmake build test_umimmio
xmake test "test_umimmio/*"
```

## Documentation

- Documentation index (recommended entry): [`docs/INDEX.md`](docs/INDEX.md)
- Getting started: [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md)
- Detailed usage: [`docs/USAGE.md`](docs/USAGE.md)
- Testing and quality gates: [`docs/TESTING.md`](docs/TESTING.md)
- Example guide: [`docs/EXAMPLES.md`](docs/EXAMPLES.md)
- Design note: [`docs/DESIGN.md`](docs/DESIGN.md)

Japanese versions are available under [`docs/ja/`](docs/ja/README.md).

Generate Doxygen HTML locally:

```bash
xmake doxygen -P . -o build/doxygen .
```

## License

MIT (`LICENSE`)
