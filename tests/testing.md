# Testing

[README](../README.md) | [日本語](testing.ja.md)

## Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_access_policy.hh`: RW/RO/WO/W1C/W1S/W1T policy enforcement, WriteBehavior, Block hierarchy, register masks, NormalWrite concept
- `tests/test_register_field.hh`: BitRegion, Register, Field, Value, mask/shift, RegionValue, modify/write/flip workflows, RegisterArray, dispatch/dispatch_r, IndexedArray
- `tests/test_transport.hh`: RAM-backed mock transport for read/write/modify/is/flip/clear/reset/read_variant, W1C edge cases, DynamicValue boundary, error policy, 64-bit registers, non-zero base_address, RegionValue edge cases, Value::shifted_value, multi-transport device, CsrTransport (MockCsrAccessor)
- `tests/test_byte_transport.hh`: SPI, I2C transports, ByteAdapter endian, transport error policy, SPI custom command bits/big-endian/16-bit, I2C 8-bit/16-bit address width
- `tests/compile_fail/bits_non_numeric.cc`: compile-fail guard — `bits()` on non-Numeric field
- `tests/compile_fail/clear_non_w1c.cc`: compile-fail guard — `clear()` on non-W1C field
- `tests/compile_fail/cross_register_write.cc`: compile-fail guard — `write()` mixing values from different registers
- `tests/compile_fail/field_overflow.cc`: compile-fail guard — BitRegion overflow (offset + width > reg width)
- `tests/compile_fail/flip_atomic_direct.cc`: compile-fail guard — `flip()` on AtomicDirectTransport (no reg_read)
- `tests/compile_fail/flip_multi_bit.cc`: compile-fail guard — `flip()` on multi-bit field
- `tests/compile_fail/flip_ro.cc`: compile-fail guard — `flip()` on read-only field
- `tests/compile_fail/flip_w1c.cc`: compile-fail guard — `flip()` on W1C field
- `tests/compile_fail/flip_w1s.cc`: compile-fail guard — `flip()` on W1S field (NormalWrite rejection)
- `tests/compile_fail/flip_w1t.cc`: compile-fail guard — `flip()` on W1T field (NormalWrite rejection)
- `tests/compile_fail/flip_wo.cc`: compile-fail guard — `flip()` on write-only field
- `tests/compile_fail/get_wrong_field.cc`: compile-fail guard — `RegionValue::get()` with field from wrong register
- `tests/compile_fail/indexed_array_oob.cc`: compile-fail guard — `IndexedArray::Entry` out-of-range
- `tests/compile_fail/modify_atomic_direct.cc`: compile-fail guard — `modify()` on AtomicDirectTransport (no reg_read)
- `tests/compile_fail/modify_cross_register.cc`: compile-fail guard — `modify()` with fields from different registers
- `tests/compile_fail/modify_w1c.cc`: compile-fail guard — `modify()` on W1C field
- `tests/compile_fail/modify_w1s.cc`: compile-fail guard — `modify()` on W1S field (NormalWrite rejection)
- `tests/compile_fail/modify_w1t.cc`: compile-fail guard — `modify()` on W1T field (NormalWrite rejection)
- `tests/compile_fail/modify_wo.cc`: compile-fail guard — `modify()` on write-only register
- `tests/compile_fail/read_atomic_direct.cc`: compile-fail guard — `read()` on AtomicDirectTransport (no reg_read)
- `tests/compile_fail/read_field_eq_int.cc`: compile-fail guard — `RegionValue == integer` (use `.bits()` for raw)
- `tests/compile_fail/read_w1s.cc`: compile-fail guard — `read()` on W1S field (not Readable)
- `tests/compile_fail/read_w1t.cc`: compile-fail guard — `read()` on W1T field (not Readable)
- `tests/compile_fail/read_wo.cc`: compile-fail guard — reading a write-only register
- `tests/compile_fail/transport_tag_mismatch.cc`: compile-fail guard — I2C transport on Direct-only device
- `tests/compile_fail/value_signed.cc`: compile-fail guard — `value()` with signed integer
- `tests/compile_fail/value_typesafe.cc`: compile-fail guard — `value()` on non-Numeric field
- `tests/compile_fail/write_ro.cc`: compile-fail guard — writing a read-only register
- `tests/compile_fail/write_ro_csr.cc`: compile-fail guard — writing a read-only CSR register via CsrTransport
- `tests/compile_fail/write_ro_value.cc`: compile-fail guard — writing a read-only register via value
- `tests/compile_fail/write_zero_args.cc`: compile-fail guard — `write()` with zero arguments

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
3. **W1S/W1T safety** — `modify()`, `flip()`, `read()` reject W1S/W1T fields via NormalWrite concept
4. **W1C mask correctness** — `modify()`, `flip()`, `clear()` properly mask W1C bits in mixed registers
5. **Bit arithmetic** — mask, shift, reset values for registers and fields
6. **RegionValue** — `bits()`, `get()`, `is()` fluent API; `RegionValue<F>` blocks raw integer comparison
7. **Transport correctness** — RAM-backed mock verifies write/read/modify round-trips
8. **CsrTransport** — MockCsrAccessor-backed CSR transport, write/read/modify/flip/concept/tag tests
9. **AtomicDirectTransport** — write-only alias transport; read/modify/flip rejected via Readable concept (compile-fail)
10. **Edge cases** — boundary values, all-W1C registers, selective clear, reset_value preservation
11. **Error policy** — CustomErrorHandler, IgnoreError, out-of-range DynamicValue detection
12. **Compile-fail guards** — illegal operations must not compile (31 test files)
13. **Multi-width registers** — 8-bit, 16-bit, 32-bit, 64-bit register operations
14. **Non-zero base_address** — MMIO peripheral at real addresses, Block within MMIO
15. **Transport variants** — SPI custom command bits, big-endian data, I2C 16-bit address width, multi-transport device
16. **RegisterArray / dispatch** — compile-time array metadata, runtime-to-compile-time index bridge, OOB handling
17. **IndexedArray** — sub-register granularity arrays, write_entry/read_entry, custom stride, OOB error policy

Concurrency/locking is out of scope for umimmio and not tested here.

**The umimmio test suite is complete at the host level.** Runtime verification of DirectTransport is outside umimmio's responsibility — it is naturally covered by upper-layer (umiport driver) tests that use umimmio in practice. Hosting ARM integration tests inside umimmio would violate the layer boundary.

## Quality Gates for Release

- All host tests pass (131 cases)
- All compile-fail contract tests pass (31 tests)
- Transport mock tests cover single and multi-field write, modify, is, flip, clear, reset, read_variant
- W1C masking: flip/modify/clear on mixed W1C registers preserves non-W1C fields
- W1C paths: all-W1C direct write, mixed RMW, selective clear
- write() semantics: single field resets others to reset_value (not zero)
- DynamicValue: boundary values, zero, out-of-range detection
- ARM cross-build passes in CI (compile compatibility check)

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
