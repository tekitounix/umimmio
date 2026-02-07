# umimmio 設計

[ドキュメント一覧](INDEX.md) | [English](../DESIGN.md)

## 1. ビジョン

`umimmio` は C++23 向けの型安全、ゼロコストメモリマップド I/O ライブラリです。

1. レジスタマップはコンパイル時に定義 — 実行時のディスカバリやパースは不要
2. ビットフィールドアクセスは型安全: 書き込み専用レジスタの読み取り、またはその逆はコンパイルエラー
3. 同一のレジスタマップ記述が Direct MMIO、I2C、SPI トランスポートで動作する
4. トランスポート選択はテンプレートパラメータで行い、実行時ディスパッチではない（vtable なし）
5. エラーハンドリングはポリシーベース: assert、trap、ignore、またはカスタムハンドラ

---

## 2. 譲れない要件

### 2.1 コンパイル時レジスタマップ

全てのレジスタアドレス、フィールド幅、アクセスポリシー、リセット値は `constexpr` です。
実行時テーブル、初期化ステップ、アロケーションは不要です。

### 2.2 アクセスポリシー強制

書き込み専用レジスタの読み取り、または読み取り専用レジスタへの書き込みはコンパイルエラーです:

```cpp
static_assert(Reg::AccessType::can_read, "Cannot read WO register");
```

### 2.3 トランスポート抽象化

レジスタ操作はバスプロトコルから分離されています。
同一の `Device/Block/Register/Field` 階層が以下で動作します:

1. Direct volatile ポインタ（Cortex-M、RISC-V メモリマップドペリフェラル）
2. I2C バス（HAL ベースまたはビットバング）
3. SPI バス（HAL ベースまたはビットバング）

トランスポートはテンプレートパラメータであり、基底クラスポインタではありません。

### 2.4 レンジチェック

フィールド値は可能な場合コンパイル時にレンジチェックされます:

1. フィールド幅を超えるリテラルでの `value()` は `mmio_compile_time_error_value_out_of_range` を発火
2. 実行時の `DynamicValue` は `CheckPolicy` + `ErrorPolicy` で write/modify 時にチェック

### 2.5 依存境界

レイヤリングは厳格です:

1. `umimmio` は C++23 標準ライブラリヘッダにのみ依存
2. `umibench/platforms` は DWT/CoreSight レジスタアクセスに `umimmio` を利用
3. `tests/` はアサーションに `umitest` を利用

依存グラフ:

```text
umibench/platforms/* -> umimmio
umimmio/tests        -> umitest
```

---

## 3. 現在のレイアウト

```text
lib/umimmio/
├── README.md
├── xmake.lua
├── docs/
│   ├── INDEX.md
│   ├── DESIGN.md
│   ├── GETTING_STARTED.md
│   ├── USAGE.md
│   ├── EXAMPLES.md
│   ├── TESTING.md
│   └── ja/
├── examples/
│   ├── minimal.cc
│   ├── register_map.cc
│   └── transport_mock.cc
├── include/umimmio/
│   ├── mmio.hh              # アンブレラヘッダ
│   ├── register.hh          # コア: RegOps, ByteAdapter, BitRegion, Field, Value
│   └── transport/
│       ├── direct.hh        # DirectTransport (volatile ポインタ)
│       ├── i2c.hh           # I2cTransport (HAL ベース)
│       ├── spi.hh           # SpiTransport (HAL ベース)
│       ├── bitbang_i2c.hh   # BitBangI2cTransport (GPIO)
│       └── bitbang_spi.hh   # BitBangSpiTransport (GPIO)
└── tests/
    ├── test_main.cc
    ├── test_access_policy.cc
    ├── test_register_field.cc
    ├── test_transport.cc
    ├── compile_fail/
    │   ├── read_wo.cc
    │   └── write_ro.cc
    └── xmake.lua
```

---

## 4. 将来のレイアウト

```text
lib/umimmio/
├── include/umimmio/
│   ├── mmio.hh
│   ├── register.hh
│   └── transport/
│       ├── direct.hh
│       ├── i2c.hh
│       ├── spi.hh
│       ├── bitbang_i2c.hh
│       ├── bitbang_spi.hh
│       └── uart.hh           # 将来: UART レジスタトランスポート
├── examples/
│   ├── minimal.cc
│   ├── register_map.cc
│   ├── transport_mock.cc
│   └── multi_transport.cc    # 将来: 同一マップ、異なるトランスポート
└── tests/
    ├── test_main.cc
    ├── test_*.cc
    ├── compile_fail/
    │   ├── read_wo.cc
    │   └── write_ro.cc
    └── xmake.lua
```

注記:

1. 公開ヘッダは `include/umimmio/` 配下に置く
2. 新しいトランスポートは `transport/` 配下に別ヘッダとして追加
3. `register.hh` はコアであり安定を維持すべき
4. トランスポート固有のエラーポリシーはトランスポートごとに追加可能

---

## 5. プログラミングモデル

### 5.1 最小フロー

Direct MMIO の最小手順:

1. ベースアドレスとアクセスポリシーで `Device` を定義
2. デバイス内に `Register` を定義
3. レジスタ内に `Field` を定義
4. `DirectTransport` を構築
5. `transport.write(Field::Set{})` または `transport.read(Field{})` を呼ぶ

### 5.2 レジスタマップ構成

典型的なデバイスレジスタマップ構造:

```cpp
struct MyDevice : umi::mmio::Device<umi::mmio::RW> {
    static constexpr umi::mmio::Addr base_address = 0x4000'0000;
};

using CTRL = umi::mmio::Register<MyDevice, 0x00, 32>;
using EN   = umi::mmio::Field<CTRL, 0, 1>;   // ビット0の1ビットフィールド
using MODE = umi::mmio::Field<CTRL, 1, 2>;   // ビット1-2の2ビットフィールド
```

### 5.3 トランスポート選択

適切な型を構築してトランスポートを選択:

```cpp
umi::mmio::DirectTransport<> direct;                    // Volatile ポインタ
umi::mmio::I2cTransport<MyI2c> i2c(hal_i2c, 0x68);     // HAL I2C
umi::mmio::SpiTransport<MySpi> spi(hal_spi);            // HAL SPI
```

全トランスポートが同一の `write()`, `read()`, `modify()`, `is()`, `flip()` API を公開します。

### 5.4 応用

応用的な使い方:

1. 単一バストランザクションでのマルチフィールド書き込み
2. `modify()` によるリードモディファイライト
3. カスタムエラーポリシー（trap、ignore、コールバック）
4. I2C/SPI デバイス向け16ビットアドレス空間
5. 設定可能なアドレス・データのエンディアン

---

## 6. コア抽象化階層

### 6.1 BitRegion

レジスタとフィールドの両方の統一的なコンパイル時基盤:

- `Register` = `IsRegister=true` の `BitRegion`（フル幅、アドレスオフセットあり）
- `Field` = `IsRegister=false` の `BitRegion`（部分幅、ビットオフセットあり）

### 6.2 RegOps (CRTP 基底)

型安全な `write()`, `read()`, `modify()`, `is()`, `flip()` メソッドを提供します。
実際のバス I/O は `Derived::reg_read()` / `Derived::reg_write()` に委譲します。

### 6.3 ByteAdapter (CRTP ブリッジ)

RegOps の型付きレジスタ操作を `raw_read()` / `raw_write()` バイト操作に変換します。
ホスト CPU とワイヤフォーマット間のエンディアン変換を処理します。

### 6.4 Value と DynamicValue

- `Value<Field, EnumValue>`: シフトされた表現を持つコンパイル時定数
- `DynamicValue<Region, T>`: 遅延レンジチェック付き実行時値

---

## 7. エラーハンドリングモデル

### 7.1 コンパイル時エラー

1. アクセスポリシー違反 → `static_assert` 失敗
2. `consteval` コンテキストでの値範囲外 → `mmio_compile_time_error_value_out_of_range`
3. デバイスに許可されていないトランスポート → `static_assert` 失敗

### 7.2 実行時エラーポリシー

`ErrorPolicy` テンプレートパラメータによるポリシーベース:

| ポリシー | 動作 |
|---------|------|
| `AssertOnError` | `assert(false && msg)` (デフォルト) |
| `TrapOnError` | `__builtin_trap()` |
| `IgnoreError` | サイレント no-op |
| `CustomErrorHandler<fn>` | ユーザーコールバック |

---

## 8. テスト戦略

1. テストは責務ごとに分割: アクセスポリシー、レジスタ/フィールド、トランスポート
2. compile-fail テストが API 契約の強制を検証（`read_wo.cc`, `write_ro.cc`）
3. トランスポートテストは `raw_read` / `raw_write` を実装した RAM バックドモックを使用
4. ハードウェアレベルの MMIO テストは実機が必要で、ホストテストのスコープ外
5. CI はホストテストと compile-fail チェックを実行

---

## 9. サンプル戦略

サンプルは学習段階を表す:

1. `minimal`: コンパイル時チェック付きの基本レジスタ・フィールド定義
2. `register_map`: 現実的な SPI ペリフェラルレジスタマップレイアウト
3. `transport_mock`: ホスト側テスト用 RAM バックドモックトランスポート

---

## 10. 短期改善計画

1. UART トランスポートヘッダを追加
2. マルチトランスポートサンプルを追加（同一レジスタマップ、異なるバス）
3. ベンダー SVD/CMSIS ファイルからのレジスタマップ生成ワークフローを文書化
4. デバッグ用バッチレジスタダンプユーティリティを追加

---

## 11. 設計原則

1. ゼロコスト抽象化 — 全てのディスパッチはコンパイル時に解決
2. 型安全 — アクセス違反はコンパイルエラーであり、実行時バグではない
3. トランスポート非依存 — 同一レジスタマップ、任意のバス
4. ポリシーベース — エラーハンドリング、レンジチェック、エンディアンは設定可能
5. 組み込みファースト — ヒープ・例外・RTTI 不使用
