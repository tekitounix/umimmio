# umibench

[English](../../README.md)

C++ 向けの軽量なクロスターゲット・マイクロベンチマークライブラリです。
ホスト / WebAssembly / 組み込みで、同じベンチマーク記述スタイルを使えます。

## 特徴

- 単一スタイルのベンチマーク記述（`<umibench/bench.hh>` と `<umibench/platform.hh>`）
- median ベースのキャリブレーションとベースライン補正
- 軽量な抽象化（`TimerLike` / `OutputLike`）
- セマンティクス・数値境界・compile-fail を含むテスト

## クイックスタート

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

## ビルドとテスト

```bash
xmake test
xmake build umibench_stm32f4_renode
xmake build umibench_stm32f4_renode_gcc
```

## ドキュメント

- [設計 & API](../DESIGN.md)
- [共通ガイド](../../docs/INDEX.md)

## ライセンス

MIT — [LICENSE](../../../LICENSE) を参照
