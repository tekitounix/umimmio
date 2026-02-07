# umirtm Documentation

[日本語](ja/INDEX.md)

This page is the canonical documentation entry for both GitHub and Doxygen.

## Read in This Order

1. [Getting Started](GETTING_STARTED.md)
2. [Usage](USAGE.md)
3. [Examples](EXAMPLES.md)
4. [Testing](TESTING.md)
5. [Design](DESIGN.md)

## API Reference Map

- Monitor (ring buffer transport):
  - `include/umirtm/rtm.hh` — Monitor class, Mode enum, terminal colors
- Printf (embedded printf):
  - `include/umirtm/printf.hh` — PrintConfig, vsnprintf, snprintf, printf
- Print (`{}` placeholders):
  - `include/umirtm/print.hh` — print, println, FormatConverter
- Host bridge (desktop utilities):
  - `include/umirtm/rtm_host.hh` — HostMonitor (stdout, shared memory, TCP)

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

- Workflow file: `.github/workflows/umirtm-ci.yml`
- Pull requests: host test execution
- `main` branch push: CI validation
