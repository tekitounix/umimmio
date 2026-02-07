# サンプル

[ドキュメント一覧](INDEX.md) | [English](../EXAMPLES.md)

## サンプル一覧

- `examples/minimal.cc`: モニターの初期化と書き込み。
- `examples/printf_demo.cc`: 全printfフォーマット指定子のデモ。
- `examples/print_demo.cc`: `{}` プレースホルダのprint/println。

## 学習順序の推奨

1. `minimal.cc`
2. `printf_demo.cc`
3. `print_demo.cc`

## 運用ガイド

- 書き込み操作の前に `rtm::init()` を1回呼び出す。
- バッファフォーマットには `rt::snprintf`、stdout出力には `rt::printf` を使用。
- `{}` スタイル出力には `rt::print` / `rt::println` を使用。
- 組み込みターゲットでは、モニターバッファはコントロールブロックIDを通じてホストデバッガが検出。
