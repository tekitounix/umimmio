# Testing

[Docs Home](INDEX.md) | [日本語](ja/TESTING.md)

## Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_monitor.cc`: Monitor write/read, capacity, buffer wrapping, overflow modes
- `tests/test_printf.cc`: printf/snprintf format specifiers and edge cases
- `tests/test_print.cc`: `{}` placeholder conversion and output

## Run Tests

```bash
xmake test
```

`xmake test` runs all targets registered with `add_tests()`.

Useful subsets:

```bash
xmake test 'test_umirtm/*'
```

## Test Strategy

1. **Monitor tests** — write/read semantics, capacity, wrapping, empty/full conditions
2. **Printf tests** — format specifiers (`%d`, `%x`, `%f`, `%s`, etc.), precision, field width, edge cases (null string, zero, negative)
3. **Print tests** — `{}` placeholder conversion, type deduction, mixed format strings

All tests run on host. Embedded verification relies on ring buffer protocol compatibility with SEGGER RTT tools.

## Quality Gates for Release

- All host tests pass
- Monitor tests verify buffer integrity under boundary conditions
- Printf tests cover all enabled format specifiers per DefaultConfig
- Print tests verify `{}` → `%` conversion correctness

## CI Coverage

- Required CI workflow: `.github/workflows/umirtm-ci.yml`
- Required jobs:
  - `host-tests` (host on ubuntu/macos)
- `xmake test` is CI-safe because it is non-interactive.

## Adding New Tests

1. Create `tests/test_<feature>.cc`
2. Add to `tests/xmake.lua` via `add_files("test_*.cc")`
3. Run `xmake test "test_umirtm/*"`
