# サンプル

[ドキュメント一覧](INDEX.md) | [English](../EXAMPLES.md)

## サンプル一覧

- `examples/minimal.cc`: 最短の完全なベンチマーク
- `examples/function_style.cc`: 既存関数シンボルの計測
- `examples/lambda_style.cc`: ラムダでの計測
- `examples/instruction_bench.cc`: 命令種別をまとめて比較
- `platforms/arm/cortex-m/stm32f4/examples/instruction_bench_cortexm.cc`: Cortex-M 特化版

## 学習順序の推奨

1. `minimal.cc`
2. `function_style.cc`
3. `lambda_style.cc`
4. `instruction_bench.cc`

## 運用ガイド

- 可能な限りターゲット非依存で書く
- ターゲット依存例は `platforms/<arch>/<board>/examples/` に分離する
- サンプルは API 利用パターン提示に集中し、テストアサーション用途にしない
