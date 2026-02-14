# Getting Started

[Docs Home](INDEX.md)

## Prerequisites

- C++23 compiler (`clang++` or `g++`)
- `xmake`

## 1. Include the Header

```cpp
#include <umimmio/mmio.hh>        // all-in-one
// or individual headers:
#include <umimmio/register.hh>     // Region, Field, Value, Block
#include <umimmio/transport/i2c.hh> // I2cTransport
```

## 2. Define Registers

```cpp
using StatusReg = umi::mmio::Region<0x4000'0000, std::uint8_t>;
using Ready     = umi::mmio::Field<StatusReg, 0, 1>;  // bit 0
using Mode      = umi::mmio::Field<StatusReg, 4, 2>;  // bits 4-5
using ModeIdle  = umi::mmio::Value<Mode, 0>;
```

## 3. Use with a Transport

```cpp
// Direct memory-mapped I/O (bare-metal)
umi::mmio::DirectTransport<> io;
io.write(Ready{}, 1);       // set bit 0
auto val = io.read(Mode{});  // read bits 4-5
```

## 4. Read More

- Usage details: [`USAGE.md`](USAGE.md)
- Example files: [`EXAMPLES.md`](EXAMPLES.md)
- Design: [`DESIGN.md`](DESIGN.md)
- Testing: [`TESTING.md`](TESTING.md)
