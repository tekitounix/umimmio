# テスト

[ドキュメント一覧](INDEX.md) | [English](../TESTING.md)

## テスト構成

- `tests/test_main.cc`: テストエントリポイント
- `tests/test_assertions.cc`: 全 assert_* メソッド (eq, ne, lt, le, gt, ge, near, true, false)
- `tests/test_format.cc`: 全対応型に対する format_value
- `tests/test_suite_workflow.cc`: Suite ライフサイクル、run()、check_*、summary()

## テスト実行

```bash
xmake test
```

`xmake test` は `add_tests()` で登録された全ターゲットを実行します。

部分実行:

```bash
xmake test 'test_umitest/*'
```

## リリース向け品質ゲート

- 全アサーションテストがホストで成功
- format_value テストが全対応型をカバー（整数、浮動小数点、bool、char、文字列、ポインタ、nullptr）
- Suite ワークフローテストが合格/不合格カウントと終了コードセマンティクスを検証
- セルフテスト: umitest は自身を使用 — フレームワークのリグレッションは即座に可視化

## CIカバレッジ

- 必須CIワークフロー: `.github/workflows/umitest-ci.yml`
- 必須ジョブ:
  - `host-tests`（ubuntu/macos でのホストテスト）
- `xmake test` は非対話実行でCI向きです。

## テストの追加方法

1. `tests/test_<feature>.cc` を作成
2. `tests/xmake.lua` の `add_files("test_*.cc")` に追加
3. `xmake test "test_umitest/*"` で実行
