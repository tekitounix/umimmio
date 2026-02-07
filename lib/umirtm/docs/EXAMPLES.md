# Examples

[Docs Home](INDEX.md)

## Example Files

- `examples/minimal.cc`: Monitor init + write.
- `examples/printf_demo.cc`: All printf format specifiers demonstrated.
- `examples/print_demo.cc`: `{}` placeholder print/println.

## Recommended Learning Order

1. `minimal.cc`
2. `printf_demo.cc`
3. `print_demo.cc`

## Guidance

- Call `rtm::init()` once before any write operations.
- Use `rt::snprintf` for buffer formatting; use `rt::printf` for stdout.
- Use `rt::print`/`rt::println` for `{}` style output.
- On embedded targets, the monitor buffers are discovered by the host debugger via the control block ID.
