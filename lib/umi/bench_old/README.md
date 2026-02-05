# bench - CPUサイクル計測フレームワーク

組み込み/ホスト両対応の軽量ベンチマークライブラリ。

## 依存関係

なし（自己完結型）

## 主要API

- `Runner<Timer>` — ベンチマーク実行
- `Stats` — 統計結果（min/max/mean/median）
- `concept TimerLike` — タイマー抽象化
- `concept OutputLike` — 出力抽象化

## クイックスタート

```cpp
#include <bench/bench.hh>
#include <bench/platform/host.hh>

using Platform = umi::bench::Host;

int main() {
    Platform::init();
    umi::bench::Runner<Platform::Timer> runner;
    runner.calibrate<64>();

    auto stats = runner.run<64>(100, [&] { /* 計測対象 */ });
    // stats.min, stats.mean, stats.median, stats.max
}
```

## ビルド・テスト

```bash
xmake build test_bench
xmake run test_bench
```
