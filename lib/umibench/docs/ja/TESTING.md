# テスト

[ドキュメント一覧](INDEX.md) | [English](../TESTING.md)

## テスト構成

- `tests/test_main.cc`: エントリポイント
- `tests/test_timer_measure.cc`: timer/measure のセマンティクス
- `tests/test_stats_runner.cc`: stats/runner の挙動
- `tests/test_platform_output_report.cc`: platform/output/report 検証
- `tests/test_integration.cc`: エンドツーエンド検証
- `tests/compile_fail/calibrate_zero.cc`: compile-fail ガード検証

## テスト実行

```bash
xmake test
```

`xmake test` は `add_tests()` で登録された全ターゲットを実行します。
`umibench` では host テスト、compile-fail テスト、WASM テスト（`emcc` 利用可能時）が対象です。

部分実行:

```bash
xmake test 'test_umibench/*'
xmake test 'test_umibench_compile_fail/*'
xmake test 'umibench_wasm/*'
```

## リリース向け品質ゲート

- ホスト機能テスト成功
- WASM テスト成功（`emcc` がある場合）
- compile-fail 契約テスト成功
- CIで組み込みクロスビルド成功（`gcc-arm`）
- リリース前に `clang-arm` プロファイルをローカルで検証

## CIカバレッジ

- 必須CIワークフロー: `.github/workflows/umibench-ci.yml`
- 必須ジョブ:
  - `host-tests`（ubuntu/macos の host + compile-fail）
  - `wasm-tests`（Emscripten + Node.js）
  - `arm-build`（STM32F4 の GCC クロスビルド）
- 手動オプション:
  - `renode-smoke`（エミュレータ依存のため best-effort）

`xmake test` 自体は非対話実行でCI向きです。
RenodeはCIで実行可能ですが、ツールチェーン/実行環境依存があるため任意扱いです。

## tests/examples にドキュメントは必要か

必要です。ただし最小限に留めるべきです。

- 正本は `docs/` 配下に集約
- fileごとのREADMEは、ターゲット固有の非自明な場合のみ追加
- 細部はファイル名とコメントで補完

この方針で、保守コストを抑えつつリリース文書の明瞭性を維持できます。
