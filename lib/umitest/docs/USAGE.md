# Usage

[Docs Home](INDEX.md)

## Core API

- `umi::test::Suite` — Test runner and statistics.
- `umi::test::TestContext` — Assertion context for structured tests.
- `umi::test::format_value()` — snprintf-based value formatter.

## Two Testing Styles

### Style 1: Structured Tests (run + TestContext)

```cpp
suite.run("test_name", [](umi::test::TestContext& ctx) {
    ctx.assert_eq(a, b);
    ctx.assert_true(cond, "message");
    ctx.assert_near(1.0, 1.001, 0.01);
    return true; // false = explicit failure
});
```

### Style 2: Inline Checks (no context needed)

```cpp
suite.check_eq(a, b);
suite.check_ne(a, b);
suite.check_lt(a, b);
suite.check(cond, "message");
suite.check_near(a, b, eps);
```

## Available Assertions

| Method | Checks |
|--------|--------|
| `assert_eq` / `check_eq` | `a == b` |
| `assert_ne` / `check_ne` | `a != b` |
| `assert_lt` / `check_lt` | `a < b` |
| `assert_le` / `check_le` | `a <= b` |
| `assert_gt` / `check_gt` | `a > b` |
| `assert_ge` / `check_ge` | `a >= b` |
| `assert_near` / `check_near` | `|a - b| < eps` |
| `assert_true` / `check` | boolean condition |

## Sections

```cpp
umi::test::Suite::section("Group Name");
```

Prints a visual section header to organize test output.

## Summary

```cpp
return suite.summary(); // 0 = all passed, 1 = failures
```

## Color Output

Disable ANSI colors by defining `UMI_TEST_NO_COLOR` before including the header.
