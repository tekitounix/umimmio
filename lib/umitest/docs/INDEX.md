# umitest Documentation

[日本語](ja/INDEX.md)

This page is the canonical documentation entry for both GitHub and Doxygen.

## Read in This Order

1. [Getting Started](GETTING_STARTED.md)
2. [Usage](USAGE.md)
3. [Examples](EXAMPLES.md)
4. [Testing](TESTING.md)
5. [Design](DESIGN.md)

## API Reference Map

- Public entrypoint: `include/umitest/test.hh`
- Core components:
  - `include/umitest/suite.hh` — Suite class, inline checks, TestContext implementation
  - `include/umitest/context.hh` — TestContext declaration (assert_* methods)
  - `include/umitest/format.hh` — format_value for diagnostic output

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

- Workflow file: `.github/workflows/umitest-ci.yml`
- Pull requests: host test execution
- `main` branch push: CI validation
