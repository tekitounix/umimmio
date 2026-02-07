# Usage

[Docs Home](INDEX.md)

## Monitor API (`rtm.hh`)

`rtm` is a type alias for `rt::Monitor<>` with default parameters.

| Method | Description |
|--------|-------------|
| `rtm::init(id)` | Initialise with control block ID string. |
| `rtm::write(str)` | Write string to up buffer. Returns bytes written. |
| `rtm::log(str)` | Write string, discard return value. |
| `rtm::read(span)` | Read from down buffer. Returns bytes read. |
| `rtm::read_byte()` | Read one byte (-1 if empty). |
| `rtm::read_line(buf, len)` | Read line from down buffer. |
| `rtm::get_available()` | Bytes pending in up buffer. |
| `rtm::get_free_space()` | Free bytes in up buffer. |

### Custom Configuration

```cpp
using MyMonitor = rt::Monitor<
    2,    // UpBuffers
    1,    // DownBuffers
    4096, // UpBufferSize
    64,   // DownBufferSize
    rt::Mode::NoBlockTrim  // overflow mode
>;
```

### Buffer Modes

| Mode | Behaviour |
|------|-----------|
| `NoBlockSkip` | Drop entire write if buffer is full. |
| `NoBlockTrim` | Write as much as fits, discard excess. |
| `BlockIfFifoFull` | Block until space is available. |

## Printf API (`printf.hh`)

| Function | Description |
|----------|-------------|
| `rt::snprintf(buf, sz, fmt, ...)` | Format to buffer. |
| `rt::vsnprintf(buf, sz, fmt, va)` | va_list variant. |
| `rt::printf(fmt, ...)` | Format to stdout. |

### Supported Format Specifiers

`%d`, `%u`, `%x`, `%X`, `%o`, `%c`, `%s`, `%p`, `%f`, `%e`, `%g`, `%%`

Flags: `0`, `-`, `+`, ` `, `#`, width, precision.

### Configuration

```cpp
rt::snprintf<rt::MinimalConfig>(buf, sz, "%d", 42); // smallest footprint
rt::snprintf<rt::FullConfig>(buf, sz, "%lld", 42LL); // full C99 support
```

## Print API (`print.hh`)

| Function | Description |
|----------|-------------|
| `rt::print(fmt, args...)` | `{}` placeholder output to stdout. |
| `rt::println(fmt, args...)` | Same + newline. |
| `rt::println()` | Bare newline. |

### Placeholder Syntax

- `{}` — auto-detect type specifier.
- `{0:x}` — explicit format override.
- `{{` / `}}` — literal braces.

## Terminal Colors (`rtm.hh`)

```cpp
rt::terminal::text::red      // "\x1B[2;31m"
rt::terminal::text::green    // "\x1B[2;32m"
rt::terminal::reset          // "\x1B[0m"
```
