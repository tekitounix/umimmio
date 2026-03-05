# テスト

[ドキュメントホーム](INDEX.md) | [English](../TESTING.md)

## テストレイアウト

- `tests/test_main.cc`: テストエントリポイント
- `tests/test_access_policy.cc`: RW/RO/WO/W1C ポリシーの強制、WriteBehavior
- `tests/test_register_field.cc`: BitRegion, Register, Field, Value, マスク/シフト、RegisterReader
- `tests/test_transport.cc`: read/write/modify/is/flip/clear/reset/read_variant 用 RAM バックドモックトランスポート
- `tests/test_spi_bitbang.cc`: SPI, I2C, BitBang トランスポート、ByteAdapter エンディアンテスト
- `tests/test_protected.cc`: NoLockPolicy および MutexPolicy による Protected<T, LockPolicy>
- `tests/compile_fail/read_wo.cc`: compile-fail ガード — 書き込み専用レジスタの読み出し
- `tests/compile_fail/write_ro.cc`: compile-fail ガード — 読み出し専用レジスタへの書き込み
- `tests/compile_fail/write_ro_value.cc`: compile-fail ガード — value 経由での読み出し専用レジスタへの書き込み
- `tests/compile_fail/value_typesafe.cc`: compile-fail ガード — 非 Numeric フィールドでの `value()`
- `tests/compile_fail/value_signed.cc`: compile-fail ガード — 符号付き整数での `value()`
- `tests/compile_fail/modify_w1c.cc`: compile-fail ガード — W1C フィールドでの `modify()`
- `tests/compile_fail/flip_w1c.cc`: compile-fail ガード — W1C フィールドでの `flip()`
- `tests/compile_fail/field_overflow.cc`: compile-fail ガード — BitRegion オーバーフロー（オフセット + 幅 > レジスタ幅）
- `tests/compile_fail/read_field_eq_int.cc`: compile-fail ガード — `FieldValue == 整数`（raw アクセスは `.bits()` を使用）

## テスト実行

```bash
xmake test
```

`xmake test` は `add_tests()` で登録されたすべてのターゲットを実行する。

有用なサブセット：

```bash
xmake test 'test_umimmio/*'
xmake test 'test_umimmio_compile_fail/*'
```

## テスト戦略

umimmio は主にコンパイル時抽象化ライブラリであるため、テストは以下に焦点を当てる：

1. **アクセスポリシー強制** — `requires` 句がコンパイル時に不正アクセスを拒否
2. **W1C 安全性** — `modify()` と `flip()` が W1C フィールドを拒否、`clear()` が唯一のパス
3. **ビット算術** — レジスタとフィールドのマスク、シフト、リセット値
4. **RegisterReader と FieldValue** — `bits()`、`get()`、`is()` フルエント API、`FieldValue<F>` が raw 整数比較をブロック
5. **トランスポート正確性** — RAM バックドモックが write/read/modify ラウンドトリップを検証
6. **保護付きアクセス** — `Protected<T, LockPolicy>` NoLockPolicy および MutexPolicy の RAII パターン検証
7. **Compile-fail ガード** — 不正操作がコンパイルされないことを確認（9 テストファイル）

ハードウェアレベル MMIO テストは実ハードウェアまたはエミュレーションが必要であり、ホストテストの対象外。

## リリースの品質ゲート

- 全ホストテストパス（68 テスト）
- 全 compile-fail 契約テストパス（9 テスト）
- トランスポートモックテストが単一および複数フィールドの write, modify, is, flip, clear, reset, read_variant をカバー
- W1C 安全性: modify_w1c, flip_w1c compile-fail テストパス
- BitRegion オーバーフロー: field_overflow compile-fail テストパス
- 符号付き値拒否: value_signed compile-fail テストパス
- 組み込みクロスビルドが CI でパス (gcc-arm)

## CI カバレッジ

- 必須 CI ワークフロー: `.github/workflows/umimmio-ci.yml`
- 必須ジョブ:
  - `host-tests` (ubuntu/macos でのホスト + compile-fail)
  - `arm-build` (クロスビルド検証)
- `xmake test` は非対話的であるため CI 安全。

## 新規テストの追加

1. `tests/test_<feature>.cc` を作成
2. `tests/xmake.lua` の `add_files("test_*.cc")` に追加
3. compile-fail テストの場合、`tests/compile_fail/` 配下に追加し、xmake.lua の `add_tests()` と `test_cases` テーブルの両方に登録
4. `xmake test "test_umimmio/*"` を実行
