# Testing

[Docs Home](INDEX.md) | [日本語](ja/TESTING.md)

## Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_assertions.cc`: all assert_* methods (eq, ne, lt, le, gt, ge, near, true, false)
- `tests/test_format.cc`: format_value for all supported types
- `tests/test_suite_workflow.cc`: Suite lifecycle, run(), check_*, summary()

## Run Tests

```bash
xmake test
```

`xmake test` runs all targets registered with `add_tests()`.

Useful subsets:

```bash
xmake test 'test_umitest/*'
```

## Quality Gates for Release

- All assertion tests pass on host
- Format value tests cover all supported types (integral, float, bool, char, string, pointer, nullptr)
- Suite workflow tests verify pass/fail counting and exit code semantics
- Self-testing: umitest uses itself — any framework regression is immediately visible

## CI Coverage

- Required CI workflow: `.github/workflows/umitest-ci.yml`
- Required jobs:
  - `host-tests` (host on ubuntu/macos)
- `xmake test` is CI-safe because it is non-interactive.

## Adding New Tests

1. Create `tests/test_<feature>.cc`
2. Add to `tests/xmake.lua` via `add_files("test_*.cc")`
3. Run `xmake test "test_umitest/*"`
