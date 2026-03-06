# Testing

[Docs Home](INDEX.md) | [日本語](ja/TESTING.md)

## Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_access_policy.cc`: RW/RO/WO/W1C policy enforcement, WriteBehavior, Block hierarchy, register masks
- `tests/test_register_field.cc`: BitRegion, Register, Field, Value, mask/shift, RegionValue, modify/write/flip workflows
- `tests/test_transport.cc`: RAM-backed mock transport for read/write/modify/is/flip/clear/reset/read_variant, W1C edge cases, DynamicValue boundary, error policy, 64-bit registers, non-zero base_address, RegionValue edge cases, Value::shifted_value, multi-transport device
- `tests/test_byte_transport.cc`: SPI, I2C transports, ByteAdapter endian, transport error policy, SPI custom command bits/big-endian/16-bit, I2C 8-bit/16-bit address width
- `tests/compile_fail/read_wo.cc`: compile-fail guard — reading a write-only register
- `tests/compile_fail/write_ro.cc`: compile-fail guard — writing a read-only register
- `tests/compile_fail/write_ro_value.cc`: compile-fail guard — writing a read-only register via value
- `tests/compile_fail/value_typesafe.cc`: compile-fail guard — `value()` on non-Numeric field
- `tests/compile_fail/value_signed.cc`: compile-fail guard — `value()` with signed integer
- `tests/compile_fail/modify_w1c.cc`: compile-fail guard — `modify()` on W1C field
- `tests/compile_fail/modify_wo.cc`: compile-fail guard — `modify()` on write-only register
- `tests/compile_fail/flip_w1c.cc`: compile-fail guard — `flip()` on W1C field
- `tests/compile_fail/flip_ro.cc`: compile-fail guard — `flip()` on read-only field
- `tests/compile_fail/flip_wo.cc`: compile-fail guard — `flip()` on write-only field
- `tests/compile_fail/field_overflow.cc`: compile-fail guard — BitRegion overflow (offset + width > reg width)
- `tests/compile_fail/clear_non_w1c.cc`: compile-fail guard — `clear()` on non-W1C field
- `tests/compile_fail/cross_register_write.cc`: compile-fail guard — `write()` mixing values from different registers
- `tests/compile_fail/read_field_eq_int.cc`: compile-fail guard — `RegionValue == integer` (use `.bits()` for raw)
- `tests/compile_fail/write_zero_args.cc`: compile-fail guard — `write()` with zero arguments
- `tests/compile_fail/transport_tag_mismatch.cc`: compile-fail guard — I2C transport on Direct-only device
- `tests/compile_fail/modify_cross_register.cc`: compile-fail guard — `modify()` with fields from different registers
- `tests/compile_fail/flip_multi_bit.cc`: compile-fail guard — `flip()` on multi-bit field
- `tests/compile_fail/get_wrong_field.cc`: compile-fail guard — `RegionValue::get()` with field from wrong register

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
3. **W1C mask correctness** — `modify()`, `flip()`, `clear()` properly mask W1C bits in mixed registers
4. **Bit arithmetic** — mask, shift, reset values for registers and fields
5. **RegionValue** — `bits()`, `get()`, `is()` fluent API; `RegionValue<F>` blocks raw integer comparison
6. **Transport correctness** — RAM-backed mock verifies write/read/modify round-trips
7. **Edge cases** — boundary values, all-W1C registers, selective clear, reset_value preservation
8. **Error policy** — CustomErrorHandler, IgnoreError, out-of-range DynamicValue detection
9. **Compile-fail guards** — illegal operations must not compile (19 test files)
10. **Multi-width registers** — 8-bit, 16-bit, 32-bit, 64-bit register operations
11. **Non-zero base_address** — MMIO peripheral at real addresses, Block within MMIO
12. **Transport variants** — SPI custom command bits, big-endian data, I2C 16-bit address width, multi-transport device

Concurrency/locking is out of scope for umimmio and not tested here.

Hardware-level MMIO tests require actual hardware or emulation and are out of scope for host tests.

## Quality Gates for Release

- All host tests pass (93 tests)
- All compile-fail contract tests pass (19 tests)
- Transport mock tests cover single and multi-field write, modify, is, flip, clear, reset, read_variant
- W1C masking: flip/modify/clear on mixed W1C registers preserves non-W1C fields
- W1C paths: all-W1C direct write, mixed RMW, selective clear
- write() semantics: single field resets others to reset_value (not zero)
- DynamicValue: boundary values, zero, out-of-range detection
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
