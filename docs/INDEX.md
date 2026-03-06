# umimmio Documentation

[日本語](ja/INDEX.md)

Documentation entry for the umimmio library.

## Read in This Order

1. [README](../README.md) — Overview, quick start, comparison
2. [Testing](TESTING.md) — Test strategy, runtime & compile-fail tests
3. [Design](DESIGN.md) — Architecture, API design, error handling

## API Reference Map

- Public entrypoint: `include/umimmio/mmio.hh`
- Core register abstractions (strict layering: policy → region → ops):
  - `include/umimmio/policy.hh` — Addr, AccessPolicy, transport tags, error policies
  - `include/umimmio/region.hh` — Device, Register, Field, Value, RegionValue, concepts
  - `include/umimmio/ops.hh` — RegOps, ByteAdapter
- Concurrency (moved to `umisync`):
  - `include/umimmio/protected.hh` — deprecated redirect to `<umisync/protected.hh>`
- Transport implementations:
  - `include/umimmio/transport/direct.hh` — DirectTransport (volatile pointer)
  - `include/umimmio/transport/i2c.hh` — I2cTransport (HAL-based)
  - `include/umimmio/transport/spi.hh` — SpiTransport (HAL-based)
  - `include/umimmio/transport/bitbang_i2c.hh` — BitBangI2cTransport (GPIO)
  - `include/umimmio/transport/bitbang_spi.hh` — BitBangSpiTransport (GPIO)


