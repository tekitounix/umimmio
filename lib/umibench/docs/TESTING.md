# Testing

[Docs Home](INDEX.md) | [日本語](ja/TESTING.md)

## Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_timer_measure.cc`: timer/measurement semantics
- `tests/test_stats_runner.cc`: statistics and runner behavior
- `tests/test_platform_output_report.cc`: platform/output/report checks
- `tests/test_integration.cc`: end-to-end benchmark flow
- `tests/compile_fail/calibrate_zero.cc`: compile-fail guard test

## Run Tests

```bash
xmake test
```

`xmake test` runs all targets registered with `add_tests()`.
In `umibench`, this includes host tests, compile-fail tests, and wasm tests (when `emcc` is available).

Useful subsets:

```bash
xmake test 'test_umibench/*'
xmake test 'test_umibench_compile_fail/*'
xmake test 'umibench_wasm/*'
```

## Quality Gates for Release

- Functional tests pass on host
- WASM tests pass (when `emcc` exists)
- Compile-fail contract test passes
- Embedded cross-build passes in CI (`gcc-arm`)
- Embedded `clang-arm` profile is validated locally before release

## CI Coverage

- Required CI workflow: `.github/workflows/umibench-ci.yml`
- Required jobs:
  - `host-tests` (host + compile-fail on ubuntu/macos)
  - `wasm-tests` (Emscripten + Node.js)
  - `arm-build` (STM32F4 GCC cross-build)
- Optional manual job:
  - `renode-smoke` (best-effort hardware-emulation smoke check)

`xmake test` itself is CI-safe because it is non-interactive.
Renode execution is possible in CI, but remains optional due emulator/toolchain environment variability.

## Do `tests/` and `examples/` Need Their Own Docs?

Yes, but keep them minimal.

- Keep one canonical document per topic under `docs/`.
- Avoid per-file mini-readmes unless behavior is target-specific or non-obvious.
- Rely on clear file names + comments for local details.

This keeps maintenance cost low and release docs clear.
