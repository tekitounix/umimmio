# Release and Versioning Policy

## Current Release Line

- Current version: `0.9.0-beta.1`
- Stability intent: release-quality beta (production-like validation before `1.0.0`)

## Versioning Rules

`umibench` uses Semantic Versioning with pre-release labels until stable.

- Beta release: `0.9.0-beta.N`
- Next beta patch: `0.9.0-beta.(N+1)`
- Stable first release target: `1.0.0`

During beta:

- Breaking API changes: bump minor (`0.x.0-beta.N` where `x` increases)
- Backward-compatible features: bump patch/minor as needed
- Fix-only beta iteration: increment beta counter first

## Changelog Rules

- Every release must update `CHANGELOG.md`.
- Keep `[Unreleased]` at top for pending changes.
- Move entries into versioned section at release cut.

## Release Checklist

1. Confirm `xmake test` passes on host test targets.
2. Confirm WASM test target (`umibench_wasm/*`) in CI.
3. Confirm Doxygen generation succeeds (`xmake doxygen -P . -o build/doxygen .`).
4. Update `VERSION`.
5. Update `Doxyfile` `PROJECT_NUMBER`.
6. Update `CHANGELOG.md` release section with date.
7. Tag release (`v0.9.0-beta.N` style during beta).

## CI Scope

- Required: host tests + compile-fail + wasm tests.
- Optional/manual: Renode smoke checks (environment/tooling dependent).
