# サンプル

[ドキュメント一覧](INDEX.md) | [English](../EXAMPLES.md)

## サンプル一覧

- `examples/minimal.cc`: 最短の完全なテスト（1スイート、1テスト）。
- `examples/assertions.cc`: 全アサーションメソッドのデモ。
- `examples/check_style.cc`: セクションとインラインチェックスタイル。

## 学習順序の推奨

1. `minimal.cc`
2. `assertions.cc`
3. `check_style.cc`

## 運用ガイド

- 複数アサーションが必要な構造化テストには `run()` + `TestContext` を使用。
- 手軽なインラインチェックには `check_*()` を使用。
- 関連するチェックは `section()` でグループ化。
- 終了コードを正しくするため `main()` から `suite.summary()` を返す。
