# テスト

[ドキュメント一覧](INDEX.md) | [English](../TESTING.md)

## テスト構成

- `tests/test_main.cc`: テストエントリポイント
- `tests/test_access_policy.cc`: RW/RO/WO ポリシーの強制
- `tests/test_register_field.cc`: BitRegion, Register, Field, Value, マスク/シフトの正当性
- `tests/test_transport.cc`: read/write/modify/is/flip 用 RAM バックドモックトランスポート
- `tests/compile_fail/read_wo.cc`: compile-fail ガード — 書き込み専用レジスタの読み取り
- `tests/compile_fail/write_ro.cc`: compile-fail ガード — 読み取り専用レジスタへの書き込み

## テスト実行

```bash
xmake test
```

`xmake test` は `add_tests()` で登録された全ターゲットを実行します。

部分実行:

```bash
xmake test 'test_umimmio/*'
xmake test 'test_umimmio_compile_fail/*'
```

## テスト戦略

umimmio は主にコンパイル時抽象化ライブラリのため、テストは以下に焦点を当てています:

1. **アクセスポリシー強制** — 不正アクセスで static_assert が発火
2. **ビット演算** — レジスタとフィールドのマスク、シフト、リセット値
3. **トランスポート正当性** — RAM バックドモックで write/read/modify のラウンドトリップを検証
4. **compile-fail ガード** — 不正操作はコンパイルできてはならない

ハードウェアレベルの MMIO テストは実機またはエミュレーションが必要で、ホストテストのスコープ外です。

## リリース向け品質ゲート

- 全ホストテスト成功
- compile-fail 契約テスト成功（read_wo, write_ro）
- トランスポートモックテストが単一・マルチフィールド書き込み、modify、is、flip をカバー
- CI で組み込みクロスビルド成功（gcc-arm）

## CIカバレッジ

- 必須CIワークフロー: `.github/workflows/umimmio-ci.yml`
- 必須ジョブ:
  - `host-tests`（ubuntu/macos でのホスト + compile-fail）
  - `arm-build`（クロスビルド検証）
- `xmake test` は非対話実行でCI向きです。

## テストの追加方法

1. `tests/test_<feature>.cc` を作成
2. `tests/xmake.lua` の `add_files("test_*.cc")` に追加
3. compile-fail テストは `tests/compile_fail/` 配下に追加し、xmake.lua に登録
4. `xmake test "test_umimmio/*"` で実行
