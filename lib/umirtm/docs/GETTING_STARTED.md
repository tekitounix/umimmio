# Getting Started

[Docs Home](INDEX.md)

## Prerequisites

- C++23 compiler (`clang++` or `g++`)
- `xmake`

## 1. Include Headers

```cpp
#include <umirtm/rtm.hh>       // Monitor (ring buffers)
#include <umirtm/printf.hh>    // rt::printf / rt::snprintf
#include <umirtm/print.hh>     // rt::print / rt::println ({} format)
```

## 2. Minimal Usage

```cpp
#include <umirtm/rtm.hh>

int main() {
    rtm::init("MY_APP");          // initialise default monitor
    rtm::log("Hello, world!\n");  // write to up buffer 0
    return 0;
}
```

## 3. Printf

```cpp
#include <umirtm/printf.hh>

char buf[64];
rt::snprintf(buf, sizeof(buf), "x = %d, y = %.2f\n", 42, 3.14);
```

## 4. Print (`{}` style)

```cpp
#include <umirtm/print.hh>

rt::println("value: {}, hex: {0:x}", 255);
```

## 5. Read More

- Usage details: [`USAGE.md`](USAGE.md)
- Example files: [`EXAMPLES.md`](EXAMPLES.md)
- Design: [`DESIGN.md`](DESIGN.md)
- Testing: [`TESTING.md`](TESTING.md)
