# Examples

[Docs Home](INDEX.md)

## Example Files

- `examples/minimal.cc`: Shortest complete test (1 suite, 1 test).
- `examples/assertions.cc`: All assertion methods demonstrated.
- `examples/check_style.cc`: Sections and inline check style.

## Recommended Learning Order

1. `minimal.cc`
2. `assertions.cc`
3. `check_style.cc`

## Guidance

- Use `run()` with `TestContext` for structured tests that need multiple assertions.
- Use `check_*()` for quick inline checks.
- Group related checks with `section()`.
- Return `suite.summary()` from `main()` for proper exit codes.
