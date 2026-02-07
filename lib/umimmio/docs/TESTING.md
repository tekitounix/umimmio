# Testing

[Docs Home](INDEX.md) | [日本語](ja/TESTING.md)

## Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_access_policy.cc`: RW/RO/WO policy enforcement
- `tests/test_register_field.cc`: BitRegion, Register, Field, Value, mask/shift correctness
- `tests/test_transport.cc`: RAM-backed mock transport for read/write/modify/is/flip
- `tests/compile_fail/read_wo.cc`: compile-fail guard — reading a write-only register
- `tests/compile_fail/write_ro.cc`: compile-fail guard — writing a read-only register

## Run Tests

```bash
xmake test
```

`xmake test` runs all targets registered with `add_tests()`.

Useful subsets:

```bash
xmake test 'test_umimmio/*'
xmake test 'test_umimmio_compile_fail/*'
```

## Test Strategy

Since umimmio is primarily a compile-time abstraction library, tests focus on:

1. **Access policy enforcement** — static_assert fires on illegal access
2. **Bit arithmetic** — mask, shift, reset values for registers and fields
3. **Transport correctness** — RAM-backed mock verifies write/read/modify round-trips
4. **Compile-fail guards** — illegal operations must not compile

Hardware-level MMIO tests require actual hardware or emulation and are out of scope for host tests.

## Quality Gates for Release

- All host tests pass
- Compile-fail contract tests pass (read_wo, write_ro)
- Transport mock tests cover single and multi-field write, modify, is, flip
- Embedded cross-build passes in CI (gcc-arm)

## CI Coverage

- Required CI workflow: `.github/workflows/umimmio-ci.yml`
- Required jobs:
  - `host-tests` (host + compile-fail on ubuntu/macos)
  - `arm-build` (cross-build verification)
- `xmake test` is CI-safe because it is non-interactive.

## Adding New Tests

1. Create `tests/test_<feature>.cc`
2. Add to `tests/xmake.lua` via `add_files("test_*.cc")`
3. For compile-fail tests, add under `tests/compile_fail/` and register in xmake.lua
4. Run `xmake test "test_umimmio/*"`
