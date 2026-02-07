# umitest

English | [日本語](docs/ja/README.md)

`umitest` is a zero-macro, header-only test framework for C++23.
It lets you write test functions as ordinary C++ code with automatic source location capture via `std::source_location`.

## Release Status

- Current version: `0.1.0`
- Stability: initial release
- Versioning policy: [`RELEASE.md`](RELEASE.md)
- Changelog: [`CHANGELOG.md`](CHANGELOG.md)

## Why umitest

- Zero macros — all assertions are regular function calls
- Header-only — `#include <umitest/test.hh>` and use
- Embedded-ready — no exceptions, no heap, no RTTI
- Two testing styles — structured (`TestContext`) and inline (`Suite::check_*`)
- Self-testing — umitest tests itself, so framework regressions are immediately visible

## Quick Start

```cpp
#include <umitest/test.hh>
using namespace umi::test;

bool test_add(TestContext& t) {
    t.assert_eq(1 + 1, 2);
    t.assert_true(true);
    return true;
}

int main() {
    Suite s("example");
    s.run("add", test_add);
    return s.summary();
}
```

## Build and Test

```bash
xmake build test_umitest
xmake test "test_umitest/*"
```

## Documentation

- Documentation index (recommended entry): [`docs/INDEX.md`](docs/INDEX.md)
- Getting started: [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md)
- Detailed usage: [`docs/USAGE.md`](docs/USAGE.md)
- Testing and quality gates: [`docs/TESTING.md`](docs/TESTING.md)
- Example guide: [`docs/EXAMPLES.md`](docs/EXAMPLES.md)
- Design note: [`docs/DESIGN.md`](docs/DESIGN.md)

Japanese versions are available under [`docs/ja/`](docs/ja/README.md).

Generate Doxygen HTML locally:

```bash
xmake doxygen -P . -o build/doxygen .
```

## License

MIT (`LICENSE`)
