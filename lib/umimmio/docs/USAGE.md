# Usage

[Docs Home](INDEX.md)

## Core Concepts

| Type | Purpose |
|------|---------|
| `Region<addr, T>` | Register at a fixed address with storage type T. |
| `Field<Region, offset, width>` | Bit field within a Region. |
| `Value<Field, val>` | Named constant for a Field. |
| `DynamicValue<Field>` | Runtime value for a Field. |
| `Block<addr, Regions...>` | Group of contiguous registers. |

## Transport Types

| Transport | Use Case |
|-----------|----------|
| `DirectTransport` | Memory-mapped I/O (volatile pointer access). |
| `I2cTransport` | HAL-compatible I2C peripheral drivers. |
| `SpiTransport` | HAL-compatible SPI peripheral drivers. |
| `BitBangI2cTransport` | Software I2C via GPIO. |
| `BitBangSpiTransport` | Software SPI via GPIO. |

## Access Policies

Fields have compile-time access policies:

| Policy | `read()` | `write()` | `modify()` |
|--------|:--------:|:---------:|:----------:|
| `RW` | Yes | Yes | Yes |
| `RO` | Yes | No | No |
| `WO` | No | Yes | No |

## Read-Modify-Write

```cpp
// Set a single field
transport.write(Enable{}, 1);

// Read a field
auto val = transport.read(Mode{});

// Check value
bool idle = transport.is(ModeIdle{});

// Toggle a field
transport.flip(Enable{});
```

## Register Map Organisation

Use `Block` to group registers:

```cpp
using SPI_Block = umi::mmio::Block<0x4001'3000,
    CR1, CR2, SR, DR>;
```

## Static Verification

All Region, Field, and Value properties are `constexpr`:

```cpp
static_assert(Ready::mask == 0x01);
static_assert(Mode::offset == 4);
static_assert(ModeIdle::shifted_value == 0);
```
