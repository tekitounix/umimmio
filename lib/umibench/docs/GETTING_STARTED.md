# Getting Started

[Docs Home](INDEX.md) | [日本語](ja/GETTING_STARTED.md)

## Prerequisites

- C++23 compiler (`clang++` or `g++`)
- `xmake`
- Optional (WASM): Emscripten (`emcc`)
- Optional (embedded): Arm toolchain (`clang-arm` or `gcc-arm`), Renode

## 1. Run Tests

```bash
xmake test
```

This runs:

- Host tests
- WASM tests (if Emscripten is available)
- Compile-fail API contract tests

## 2. Build Embedded Targets (Optional)

```bash
xmake build umibench_stm32f4_renode
xmake build umibench_stm32f4_renode_gcc
```

## 3. Write Your First Benchmark

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

    umi::bench::report<Platform>("first", stats);
    Platform::halt();
    return 0;
}
```

## 4. Read More

- Usage details: [`USAGE.md`](USAGE.md)
- Platform details: [`PLATFORMS.md`](PLATFORMS.md)
- Testing policy: [`TESTING.md`](TESTING.md)
- Example map: [`EXAMPLES.md`](EXAMPLES.md)
