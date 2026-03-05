# umimmio Documentation

[日本語](ja/INDEX.md)

This page is the canonical documentation entry for both GitHub and Doxygen.

## Read in This Order

1. [Testing](TESTING.md)
2. [Design](DESIGN.md)
3. [Implementation Plans](plans/)

## API Reference Map

- Public entrypoint: `include/umimmio/mmio.hh`
- Core register abstractions:
  - `include/umimmio/register.hh` — BitRegion, Register, Field, Value, RegOps, ByteAdapter, RegisterReader, concepts
- Concurrency:
  - `include/umimmio/protected.hh` — Protected, Guard, MutexPolicy, NoLockPolicy
- Transport implementations:
  - `include/umimmio/transport/direct.hh` — DirectTransport (volatile pointer)
  - `include/umimmio/transport/i2c.hh` — I2cTransport (HAL-based)
  - `include/umimmio/transport/spi.hh` — SpiTransport (HAL-based)
  - `include/umimmio/transport/bitbang_i2c.hh` — BitBangI2cTransport (GPIO)
  - `include/umimmio/transport/bitbang_spi.hh` — BitBangSpiTransport (GPIO)

## Local Generation

```bash
xmake doxygen -P . -o build/doxygen .
```

Generated entrypoint:

- `build/doxygen/html/index.html`

## Release Metadata

- Version file: `VERSION`
- Changelog: `CHANGELOG.md`
- Release policy: `RELEASE.md`

GitHub automation:

- Workflow file: `.github/workflows/umimmio-ci.yml`
- Pull requests: host test + compile-fail execution
- `main` branch push: CI validation
