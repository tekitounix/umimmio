# はじめに

[ドキュメント一覧](INDEX.md) | [English](../GETTING_STARTED.md)

## 前提条件

- C++23 コンパイラ（`clang++` または `g++`）
- `xmake`
- 任意（WASM）: Emscripten（`emcc`）
- 任意（組み込み）: Arm ツールチェーン（`clang-arm` または `gcc-arm`）、Renode

## 1. テスト実行

```bash
xmake test
```

実行対象:

- ホストテスト
- WASM テスト（`emcc` がある場合）
- compile-fail API 契約テスト

## 2. 組み込みターゲットをビルド（任意）

```bash
xmake build umibench_stm32f4_renode
xmake build umibench_stm32f4_renode_gcc
```

## 3. 最初のベンチマークを書く

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

## 4. 次に読むドキュメント

- 詳細な使い方: [`USAGE.md`](USAGE.md)
- プラットフォーム: [`PLATFORMS.md`](PLATFORMS.md)
- テスト方針: [`TESTING.md`](TESTING.md)
- サンプルガイド: [`EXAMPLES.md`](EXAMPLES.md)
