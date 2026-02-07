# Getting Started

[Docs Home](INDEX.md)

## Prerequisites

- C++23 compiler (`clang++` or `g++`)
- `xmake`

## 1. Include the Header

```cpp
#include <umitest/test.hh>
```

## 2. Write Your First Test

```cpp
#include <umitest/test.hh>

int main() {
    umi::test::Suite suite("my_first_test");

    suite.run("addition works", [](umi::test::TestContext& ctx) {
        ctx.assert_eq(1 + 1, 2);
        return true;
    });

    return suite.summary();
}
```

## 3. Run

```bash
xmake test
```

## 4. Read More

- Usage details: [`USAGE.md`](USAGE.md)
- Example files: [`EXAMPLES.md`](EXAMPLES.md)
- Design: [`DESIGN.md`](DESIGN.md)
- Testing strategy: [`TESTING.md`](TESTING.md)
