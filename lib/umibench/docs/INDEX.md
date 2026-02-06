# umibench Documentation

[日本語](ja/INDEX.md)

This page is the canonical documentation entry for both GitHub and Doxygen.

## Read in This Order

1. [Getting Started](GETTING_STARTED.md)
2. [Usage](USAGE.md)
3. [Platforms](PLATFORMS.md)
4. [Examples](EXAMPLES.md)
5. [Testing](TESTING.md)
6. [Design](DESIGN.md)

## API Reference Map

- Public entrypoint: `include/umibench/bench.hh`
- Core measurement API:
  - `include/umibench/core/measure.hh`
  - `include/umibench/core/runner.hh`
  - `include/umibench/core/stats.hh`
- Concepts:
  - `include/umibench/timer/concept.hh`
  - `include/umibench/output/concept.hh`

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

- Workflow file: `.github/workflows/umibench-doxygen.yml`
- Pull requests: HTML artifact upload
- `main` branch push: GitHub Pages deploy
