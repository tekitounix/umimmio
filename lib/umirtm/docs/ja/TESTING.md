# テスト

[ドキュメント一覧](INDEX.md) | [English](../TESTING.md)

## テスト構成

- `tests/test_main.cc`: テストエントリポイント
- `tests/test_monitor.cc`: Monitor の write/read、容量、バッファラッピング、オーバーフローモード
- `tests/test_printf.cc`: printf/snprintf フォーマット指定子とエッジケース
- `tests/test_print.cc`: `{}` プレースホルダ変換と出力

## テスト実行

```bash
xmake test
```

`xmake test` は `add_tests()` で登録された全ターゲットを実行します。

部分実行:

```bash
xmake test 'test_umirtm/*'
```

## テスト戦略

1. **Monitor テスト** — write/read セマンティクス、容量、ラッピング、空/満杯条件
2. **Printf テスト** — フォーマット指定子 (`%d`, `%x`, `%f`, `%s` 等)、精度、フィールド幅、エッジケース（null 文字列、ゼロ、負数）
3. **Print テスト** — `{}` プレースホルダ変換、型推論、混合フォーマット文字列

全テストはホストで実行されます。組み込み検証は SEGGER RTT ツールとのリングバッファプロトコル互換性に依存します。

## リリース向け品質ゲート

- 全ホストテスト成功
- Monitor テストが境界条件下でバッファ整合性を検証
- Printf テストが DefaultConfig に基づく全有効フォーマット指定子をカバー
- Print テストが `{}` → `%` 変換の正当性を検証

## CIカバレッジ

- 必須CIワークフロー: `.github/workflows/umirtm-ci.yml`
- 必須ジョブ:
  - `host-tests`（ubuntu/macos でのホストテスト）
- `xmake test` は非対話実行でCI向きです。

## テストの追加方法

1. `tests/test_<feature>.cc` を作成
2. `tests/xmake.lua` の `add_files("test_*.cc")` に追加
3. `xmake test "test_umirtm/*"` で実行
