# Testing

[Docs Home](INDEX.md) | [日本語](ja/TESTING.md)

## Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_access_policy.cc`: RW/RO/WO/W1C policy enforcement, WriteBehavior
- `tests/test_register_field.cc`: BitRegion, Register, Field, Value, mask/shift, RegisterReader
- `tests/test_transport.cc`: RAM-backed mock transport for read/write/modify/is/flip/clear/reset/read_variant
- `tests/test_spi_bitbang.cc`: SPI, I2C, BitBang transports, ByteAdapter endian tests
- `tests/test_protected.cc`: Protected<T, LockPolicy> with NoLockPolicy
- `tests/compile_fail/read_wo.cc`: compile-fail guard — reading a write-only register
- `tests/compile_fail/write_ro.cc`: compile-fail guard — writing a read-only register
- `tests/compile_fail/write_ro_value.cc`: compile-fail guard — writing a read-only register via value
- `tests/compile_fail/value_typesafe.cc`: compile-fail guard — `value()` on non-Numeric field
- `tests/compile_fail/value_signed.cc`: compile-fail guard — `value()` with signed integer
- `tests/compile_fail/modify_w1c.cc`: compile-fail guard — `modify()` on W1C field
- `tests/compile_fail/flip_w1c.cc`: compile-fail guard — `flip()` on W1C field
- `tests/compile_fail/field_overflow.cc`: compile-fail guard — BitRegion overflow (offset + width > reg width)

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

1. **Access policy enforcement** — `requires` clauses reject illegal access at compile time
2. **W1C safety** — `modify()` and `flip()` reject W1C fields; `clear()` is the only path
3. **Bit arithmetic** — mask, shift, reset values for registers and fields
4. **RegisterReader** — `bits()`, `get()`, `is()` fluent API for register reads
5. **Transport correctness** — RAM-backed mock verifies write/read/modify round-trips
6. **Protected access** — `Protected<T, NoLockPolicy>` RAII pattern verification
7. **Compile-fail guards** — illegal operations must not compile (8 test files)

Hardware-level MMIO tests require actual hardware or emulation and are out of scope for host tests.

## Quality Gates for Release

- All host tests pass (59 tests)
- All compile-fail contract tests pass (8 tests)
- Transport mock tests cover single and multi-field write, modify, is, flip, clear, reset, read_variant
- W1C safety: modify_w1c, flip_w1c compile-fail tests pass
- BitRegion overflow: field_overflow compile-fail test passes
- Signed value rejection: value_signed compile-fail test passes
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
3. For compile-fail tests, add under `tests/compile_fail/` and register in both `add_tests()` and `test_cases` table in xmake.lua
4. Run `xmake test "test_umimmio/*"`
