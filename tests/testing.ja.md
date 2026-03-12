# テスト

[README](../README.md) | [English](testing.md) | 日本語

## テストレイアウト

- `tests/test_main.cc`: テストエントリポイント
- `tests/test_access_policy.hh`: RW/RO/WO/W1C/W1S/W1T ポリシーの強制、WriteBehavior、Block 階層、レジスタマスク、NormalWrite concept
- `tests/test_register_field.hh`: BitRegion, Register, Field, Value, マスク/シフト、RegionValue、modify/write/flip ワークフロー、RegisterArray, dispatch/dispatch_r, IndexedArray
- `tests/test_transport.hh`: RAM バックドモックトランスポート — read/write/modify/is/flip/clear/reset/read_variant、W1C エッジケース、DynamicValue 境界値、エラーポリシー、64-bit レジスタ、非ゼロ base_address、RegionValue エッジケース、Value::shifted_value、マルチトランスポートデバイス、CsrTransport (MockCsrAccessor)
- `tests/test_byte_transport.hh`: SPI, I2C トランスポート、ByteAdapter エンディアン、トランスポートエラーポリシー、SPI カスタムコマンドビット/ビッグエンディアン/16-bit、I2C 8-bit/16-bit アドレス幅
- `tests/compile_fail/bits_non_numeric.cc`: compile-fail ガード — 非 Numeric フィールドでの `bits()`
- `tests/compile_fail/clear_non_w1c.cc`: compile-fail ガード — 非 W1C フィールドでの `clear()`
- `tests/compile_fail/cross_register_write.cc`: compile-fail ガード — 異なるレジスタの値を混ぜた `write()`
- `tests/compile_fail/field_overflow.cc`: compile-fail ガード — BitRegion オーバーフロー（オフセット + 幅 > レジスタ幅）
- `tests/compile_fail/flip_atomic_direct.cc`: compile-fail ガード — AtomicDirectTransport での `flip()` (reg_read なし)
- `tests/compile_fail/flip_multi_bit.cc`: compile-fail ガード — 複数ビットフィールドでの `flip()`
- `tests/compile_fail/flip_ro.cc`: compile-fail ガード — 読み出し専用フィールドでの `flip()`
- `tests/compile_fail/flip_w1c.cc`: compile-fail ガード — W1C フィールドでの `flip()`
- `tests/compile_fail/flip_w1s.cc`: compile-fail ガード — W1S フィールドでの `flip()` (NormalWrite 拒否)
- `tests/compile_fail/flip_w1t.cc`: compile-fail ガード — W1T フィールドでの `flip()` (NormalWrite 拒否)
- `tests/compile_fail/flip_wo.cc`: compile-fail ガード — 書き込み専用フィールドでの `flip()`
- `tests/compile_fail/get_wrong_field.cc`: compile-fail ガード — 別レジスタのフィールドでの `RegionValue::get()`
- `tests/compile_fail/indexed_array_oob.cc`: compile-fail ガード — `IndexedArray::Entry` 範囲外
- `tests/compile_fail/modify_atomic_direct.cc`: compile-fail ガード — AtomicDirectTransport での `modify()` (reg_read なし)
- `tests/compile_fail/modify_cross_register.cc`: compile-fail ガード — 異なるレジスタのフィールドを混ぜた `modify()`
- `tests/compile_fail/modify_w1c.cc`: compile-fail ガード — W1C フィールドでの `modify()`
- `tests/compile_fail/modify_w1s.cc`: compile-fail ガード — W1S フィールドでの `modify()` (NormalWrite 拒否)
- `tests/compile_fail/modify_w1t.cc`: compile-fail ガード — W1T フィールドでの `modify()` (NormalWrite 拒否)
- `tests/compile_fail/modify_wo.cc`: compile-fail ガード — 書き込み専用レジスタでの `modify()`
- `tests/compile_fail/read_atomic_direct.cc`: compile-fail ガード — AtomicDirectTransport での `read()` (reg_read なし)
- `tests/compile_fail/read_field_eq_int.cc`: compile-fail ガード — `RegionValue == 整数`（raw アクセスは `.bits()` を使用）
- `tests/compile_fail/read_w1s.cc`: compile-fail ガード — W1S フィールドの `read()` (Readable でない)
- `tests/compile_fail/read_w1t.cc`: compile-fail ガード — W1T フィールドの `read()` (Readable でない)
- `tests/compile_fail/read_wo.cc`: compile-fail ガード — 書き込み専用レジスタの読み出し
- `tests/compile_fail/transport_tag_mismatch.cc`: compile-fail ガード — Direct 専用デバイスでの I2C トランスポート使用
- `tests/compile_fail/value_signed.cc`: compile-fail ガード — 符号付き整数での `value()`
- `tests/compile_fail/value_typesafe.cc`: compile-fail ガード — 非 Numeric フィールドでの `value()`
- `tests/compile_fail/write_ro.cc`: compile-fail ガード — 読み出し専用レジスタへの書き込み
- `tests/compile_fail/write_ro_csr.cc`: compile-fail ガード — CsrTransport 経由での RO CSR レジスタへの書き込み
- `tests/compile_fail/write_ro_value.cc`: compile-fail ガード — value 経由での読み出し専用レジスタへの書き込み
- `tests/compile_fail/write_zero_args.cc`: compile-fail ガード — 引数なしの `write()`

## テスト実行

```bash
xmake test
```

`xmake test` は `add_tests()` で登録されたすべてのターゲットを実行する。

有用なサブセット：

```bash
xmake test 'test_umimmio/*'
xmake test 'test_umimmio/fail_*'
```

## テスト戦略

umimmio は主にコンパイル時抽象化ライブラリであるため、テストは以下に焦点を当てる：

1. **アクセスポリシー強制** — `requires` 句がコンパイル時に不正アクセスを拒否
2. **W1C 安全性** — `modify()` と `flip()` が W1C フィールドを拒否、`clear()` が唯一のパス
3. **W1S/W1T 安全性** — `modify()`、`flip()`、`read()` が W1S/W1T フィールドを NormalWrite concept で拒否
4. **W1C マスク正確性** — `modify()`、`flip()`、`clear()` が混合レジスタで W1C ビットを適切にマスク
5. **ビット算術** — レジスタとフィールドのマスク、シフト、リセット値
6. **RegionValue** — `bits()`、`get()`、`is()` フルエント API、`RegionValue<F>` が raw 整数比較をブロック
7. **トランスポート正確性** — RAM バックドモックが write/read/modify ラウンドトリップを検証
8. **CsrTransport** — MockCsrAccessor ベースの CSR トランスポート、write/read/modify/flip/concept/tag テスト
9. **AtomicDirectTransport** — 書き込み専用エイリアストランスポート、read/modify/flip が Readable concept で拒否（compile-fail）
10. **エッジケース** — 境界値、全 W1C レジスタ、選択的クリア、reset_value 保持
11. **エラーポリシー** — CustomErrorHandler、IgnoreError、範囲外 DynamicValue 検出
12. **Compile-fail ガード** — 不正操作がコンパイルされないことを確認（31 テストファイル）
13. **複数幅レジスタ** — 8-bit、16-bit、32-bit、64-bit レジスタ操作
14. **非ゼロ base_address** — 実アドレスの MMIO ペリフェラル、MMIO 内の Block
15. **トランスポートバリアント** — SPI カスタムコマンドビット、ビッグエンディアン、I2C 16-bit アドレス幅、マルチトランスポートデバイス
16. **RegisterArray / dispatch** — コンパイル時配列メタデータ、ランタイム→コンパイル時インデックスブリッジ、OOB ハンドリング
17. **IndexedArray** — サブレジスタ粒度配列、write_entry/read_entry、カスタム stride、OOB エラーポリシー

並行性/ロックは umimmio のスコープ外であり、ここではテストしない。

**umimmio のテストはこのホストスイートで完結する。** DirectTransport の実行時検証は umimmio の責任範囲外であり、umimmio を使用する上位層（umiport ドライバ）のテストで自然に担保される。ARM 統合テストを umimmio 内に持つことはレイヤー構造の違反になるため行わない。

## リリースの品質ゲート

- 全ホストテストパス（131 ケース）
- 全 compile-fail 契約テストパス（31 テスト）
- トランスポートモックテストが単一および複数フィールドの write, modify, is, flip, clear, reset, read_variant をカバー
- W1C マスキング: 混合 W1C レジスタでの flip/modify/clear が非 W1C フィールドを保持
- W1C パス: 全 W1C 直接書き込み、混合 RMW、選択的クリア
- write() セマンティクス: 単一フィールド書き込みで他のフィールドが reset_value になる（ゼロではない）
- DynamicValue: 境界値、ゼロ、範囲外検出
- ARM クロスビルドが CI でパス（コンパイル互換性の確認）

## CI カバレッジ

umimmio のテストはプロジェクト全体の CI パイプライン（`.github/workflows/ci.yml`）の一部として実行される。
`xmake test` は非対話的であるため CI 安全。

## 新規テストの追加

1. `tests/test_<feature>.hh` を作成 — `test_main.cc` から include し `main()` で呼び出し
2. compile-fail: `tests/compile_fail/` に `.cc` ファイルを追加 — glob で自動検出
3. smoke: `tests/smoke/` に `.cc` ファイルを追加 — glob で自動検出
4. `xmake test "test_umimmio/*"` を実行

xmake.lua の編集は不要。
