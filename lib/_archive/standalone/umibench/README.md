# umibench

[日本語](docs/ja/README.md)

A small cross-target micro-benchmark library for C++.
Write one benchmark program style and run it on host, WebAssembly, and embedded targets.

## Why umibench

- Single benchmark style across targets (`#include <umibench/bench.hh>`, `#include <umibench/platform.hh>`)
- Baseline-corrected measurement with robust median-based calibration
- Lightweight output abstraction (`OutputLike`) and timer abstraction (`TimerLike`)
- Test suite covering semantics, numeric edge cases, and compile-fail API guards

## Quick Start

```cpp
#include <umibench/bench.hh>
#include <umibench/platform.hh>

int main() {
    using Platform = umi::bench::Platform;

    umi::bench::Runner<Platform::Timer> runner;
    runner.calibrate<64>();

    auto stats = runner.run<64>(100, [] {
        volatile int x = 0;
        x += 1;
        (void)x;
    });

    umi::bench::report<Platform>("sample", stats);
    Platform::halt();
    return 0;
}
```

## Build and Test

```bash
xmake test
xmake build umibench_stm32f4_renode
xmake build umibench_stm32f4_renode_gcc
```

## Public API

- Entrypoint: `include/umibench/bench.hh`
- Core: `Runner<Timer>`, `calibrate<N>()`, `run<N>(iters, fn)`, `report<Platform>(name, stats)`
- Concepts: `TimerLike`, `OutputLike`

## Examples

- [`examples/minimal.cc`](examples/minimal.cc) — shortest complete benchmark
- [`examples/function_style.cc`](examples/function_style.cc) — benchmark existing function symbols
- [`examples/lambda_style.cc`](examples/lambda_style.cc) — benchmark inline lambdas
- [`examples/instruction_bench.cc`](examples/instruction_bench.cc) — grouped instruction-style micro-benchmarks

## Documentation

- [Design & API](docs/DESIGN.md)
- [Common Guides](../docs/INDEX.md)
- API docs: `doxygen Doxyfile` → `build/doxygen/html/index.html`

## License

MIT — See [LICENSE](../../LICENSE)
