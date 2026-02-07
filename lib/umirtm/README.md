# umirtm

English | [日本語](docs/ja/README.md)

`umirtm` is a header-only Real-Time Monitor library for C++23.
It provides SEGGER RTT compatible ring buffers, an embedded printf, and `{}` placeholder print — all with zero heap allocation.

## Release Status

- Current version: `0.1.0`
- Stability: initial release
- Versioning policy: [`RELEASE.md`](RELEASE.md)
- Changelog: [`CHANGELOG.md`](CHANGELOG.md)

## Why umirtm

- RTT compatible — works with existing RTT viewers (J-Link, pyOCD, OpenOCD)
- Three output layers — raw ring buffer, printf, and `{}` format print
- Lightweight printf — no heap, configurable feature set for code size control
- Header-only — zero build dependencies
- Host testable — includes host-side bridge for unit testing and shared memory export

## Quick Start

```cpp
#include <umirtm/rtm.hh>
#include <umirtm/print.hh>

int main() {
    rtm::init("MY_RTM");
    rtm::log<0>("hello\n");
    rt::println("value = {}", 42);
    return 0;
}
```

## Public Headers

- `umirtm/rtm.hh` — RTT monitor core (Monitor, Mode, terminal colors)
- `umirtm/printf.hh` — Lightweight printf/snprintf (PrintConfig, format engine)
- `umirtm/print.hh` — `{}` format print/println helper
- `umirtm/rtm_host.hh` — Host-side bridge (stdout, shared memory, TCP)

## Build and Test

```bash
xmake build test_umirtm
xmake test "test_umirtm/*"
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
