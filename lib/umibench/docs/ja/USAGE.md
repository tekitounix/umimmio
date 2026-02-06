# 使い方

[ドキュメント一覧](INDEX.md) | [English](../USAGE.md)

## コアAPI

- `umi::bench::Runner<Timer>`
- `Runner::calibrate<N>()`
- `Runner::run<N>(func)`
- `Runner::run<N>(iterations, func)`
- `umi::bench::report<Platform>(name, stats)`
- `umi::bench::report_compact<Platform>(name, stats)`

## 推奨フロー

1. `Runner<Platform::Timer>` を作成
2. `calibrate<N>()` を一度実行
3. サンプル数/反復回数を決めて `run`
4. `report`（または `report_compact`）で出力

## 計測セマンティクス

- キャリブレーションは median ベースでベースラインを推定
- 補正後サンプルは `0` で下限クランプ
- `run<N>(iters, fn)` は `fn` を厳密に `N * iters` 回呼び出し

## 数値特性

- report の min/max/median は 64bit 安全
- 偶数サンプル中央値はオーバーフロー回避式を使用
- `calibrate<0>()` はコンパイル時に拒否

## 出力/タイマの差し替え

独自バックエンドを `Platform` で差し替え可能です。

必要契約:

- `TimerLike`: `enable()`, `now()`, `Counter`
- `OutputLike`: `init()`, `putc()`, `puts()`, `print_uint(uint64_t)`, `print_double(double)`

参照:

- `include/umibench/timer/concept.hh`
- `include/umibench/output/concept.hh`
