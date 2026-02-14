# Examples

[Docs Home](INDEX.md)

## Example Files

- `examples/minimal.cc`: Basic Region and Field definitions with compile-time checks.
- `examples/register_map.cc`: Realistic SPI peripheral register map layout.
- `examples/transport_mock.cc`: RAM-backed mock transport for host-side testing.

## Recommended Learning Order

1. `minimal.cc`
2. `register_map.cc`
3. `transport_mock.cc`

## Guidance

- Register maps are typically defined in a device-specific header (e.g., `spi_regs.hh`).
- Use `static_assert` to verify mask and offset values at compile time.
- For host testing, create a RAM-backed mock transport rather than accessing real MMIO.
- Transport objects hold no state for `DirectTransport`; I2C/SPI transports store a driver reference and device address.
