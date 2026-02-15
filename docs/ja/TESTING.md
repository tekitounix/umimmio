# テスト

[Docs Home](INDEX.md) | [English](../TESTING.md)

## テストレイアウト

- `tests/test_main.cc`: テストエントリポイント
- `tests/test_access_policy.cc`: RW/RO/WO ポリシーの強制
- `tests/test_register_field.cc`: BitRegion, Register, Field, Value, マスク/シフトの正しさ
- `tests/test_transport.cc`: read/write/modify/is/flip 用 RAM バックドモックトランスポート
- `tests/compile_fail/read_wo.cc`: compile-fail ガード — 書き込み専用レジスタの読み出し
- `tests/compile_fail/write_ro.cc`: compile-fail ガード — 読み出し専用レジスタへの書き込み

## テスト実行

```bash
xmake test
```

`xmake test` は `add_tests()` で登録されたすべてのターゲットを実行します。

サブセット指定:

```bash
xmake test 'test_umimmio/*'
xmake test 'test_umimmio_compile_fail/*'
```

## テスト戦略

umimmio は主にコンパイル時抽象化ライブラリであるため、テストは以下に重点を置きます：

1. **アクセスポリシーの強制** — 不正アクセスに対して static_assert が発火
2. **ビット演算** — レジスタとフィールドのマスク、シフト、リセット値
3. **トランスポートの正しさ** — RAM バックドモックが write/read/modify のラウンドトリップを検証
4. **compile-fail ガード** — 不正操作がコンパイルされないことを確認

ハードウェアレベル MMIO テストは実ハードウェアまたはエミュレーションが必要であり、ホストテストの対象外です。

## リリース品質ゲート

- 全ホストテストパス
- compile-fail 契約テストパス (read_wo, write_ro)
- トランスポートモックテストが単一および複数フィールドの write, modify, is, flip をカバー
- 組み込みクロスビルドが CI でパス (gcc-arm)

## CI カバレッジ

- 必須 CI ワークフロー: `.github/workflows/umimmio-ci.yml`
- 必須ジョブ:
  - `host-tests` (ubuntu/macos でのホスト + compile-fail)
  - `arm-build` (クロスビルド検証)
- `xmake test` は非対話型であるため CI セーフ。

## 新テスト追加方法

1. `tests/test_<feature>.cc` を作成
2. `tests/xmake.lua` の `add_files("test_*.cc")` に追加
3. compile-fail テストの場合、`tests/compile_fail/` 配下に追加し xmake.lua に登録
4. `xmake test "test_umimmio/*"` で実行
