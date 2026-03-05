# umimmio 設計

[English](../DESIGN.md)

## 1. ビジョン

`umimmio` は C++23 向けの型安全、ゼロコストメモリマップド I/O ライブラリです：

1. レジスタマップはコンパイル時に定義 — ランタイムでの探索やパースは不要。
2. ビットフィールドアクセスは型安全：書き込み専用レジスタの読み出し（およびその逆）はコンパイルエラー。
3. 同一のレジスタマップ記述が Direct MMIO、I2C、SPI トランスポートで動作。
4. トランスポート選択はテンプレートパラメータであり、ランタイムディスパッチではない（vtable なし）。
5. エラーハンドリングはポリシーベース：assert、trap、ignore、カスタムハンドラ。

---

## 2. 絶対要件

### 2.1 コンパイル時レジスタマップ

すべてのレジスタアドレス、フィールド幅、アクセスポリシー、リセット値は `constexpr`。
ランタイムテーブルなし、初期化ステップなし、アロケーションなし。

### 2.2 アクセスポリシーの強制

書き込み専用レジスタの読み出し、または読み出し専用レジスタへの書き込みはコンパイルエラーとなる：

```cpp
static_assert(Reg::AccessType::can_read, "Cannot read WO register");
```

### 2.3 トランスポート抽象化

レジスタ操作はバスプロトコルから分離されている。
同一の `Device/Block/Register/Field` 階層が以下で動作する：

1. Direct volatile ポインタ（Cortex-M、RISC-V メモリマップドペリフェラル）。
2. I2C バス（HAL ベースまたはビットバング）。
3. SPI バス（HAL ベースまたはビットバング）。

トランスポートはテンプレートパラメータであり、基底クラスポインタではない。

### 2.4 範囲チェック

フィールド値は可能な限りコンパイル時に範囲チェックされる：

1. フィールド幅を超えるリテラルを持つ `value()` は `mmio_compile_time_error_value_out_of_range` を発生。
2. ランタイム `DynamicValue` は書き込み/変更時に `CheckPolicy` + `ErrorPolicy` でチェック。

### 2.5 依存関係の境界

レイヤリングは厳格：

1. `umimmio` は C++23 標準ライブラリヘッダーのみに依存。
2. `umibench/platforms` は DWT/CoreSight レジスタアクセスのために `umimmio` に依存。
3. `tests/` はアサーション用に `umitest` に依存。

依存グラフ：

```text
umibench/platforms/* -> umimmio
umimmio/tests        -> umitest
```

---

## 3. 現行レイアウト

```text
lib/umimmio/
├── README.md
├── xmake.lua
├── docs/
│   ├── INDEX.md
│   ├── DESIGN.md
│   ├── TESTING.md
│   └── ja/
├── examples/
│   ├── minimal.cc
│   ├── register_map.cc
│   └── transport_mock.cc
├── include/umimmio/
│   ├── mmio.hh              # アンブレラヘッダー
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

## 4. 成長レイアウト

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

注記：

1. パブリックヘッダーは `include/umimmio/` 配下に配置。
2. 新しいトランスポートは `transport/` 配下に個別ヘッダーとして追加。
3. `register.hh` はコアであり、安定を維持すべき。
4. トランスポート固有のエラーポリシーをトランスポートごとに追加する可能性がある。

---

## 5. プログラミングモデル

### 5.0 API リファレンス

パブリックエントリポイント: `include/umimmio/mmio.hh`

コア型：

| 型 | 用途 |
|------|---------|
| `Device<Access, Transports...>` | ベースアドレス、アクセスポリシー、許可トランスポートを持つデバイスルート |
| `Register<Device, Offset, Bits, Access, Reset>` | デバイス内のオフセットにあるレジスタ |
| `Field<Reg, BitOffset, BitWidth, ...Traits>` | レジスタ内のビットフィールド（可変長トレイト） |
| `Value<Field, val>` | Field の名前付き定数 |
| `DynamicValue<Field, T>` | Field のランタイム値 |
| `Numeric` | トレイト: Field で raw `value()` を有効化 |
| `raw<Field>(val)` | エスケープハッチ: 任意の Field への raw 値 |

トランスポート型：

| トランスポート | 用途 |
|-----------|----------|
| `DirectTransport` | メモリマップド I/O (volatile ポインタアクセス) |
| `I2cTransport` | HAL 互換 I2C ペリフェラルドライバ |
| `SpiTransport` | HAL 互換 SPI ペリフェラルドライバ |
| `BitBangI2cTransport` | GPIO によるソフトウェア I2C |
| `BitBangSpiTransport` | GPIO によるソフトウェア SPI |

アクセスポリシー：

| ポリシー | `read()` | `write()` | `modify()` |
|--------|:--------:|:---------:|:----------:|
| `RW` | Yes | Yes | Yes |
| `RO` | Yes | No | No |
| `WO` | No | Yes | No |

### 5.1 最小パス

Direct MMIO の最小フロー：

1. ベースアドレスとアクセスポリシーを持つ `Device` を定義。
2. デバイス内に `Register` を定義。
3. レジスタ内に `Field` を定義。
4. `DirectTransport` を構築。
5. `transport.write(Field::Set{})` または `transport.read(Field{})` を呼び出し。

### 5.2 レジスタマップの構成

典型的なデバイスレジスタマップの構造：

```cpp
namespace mm = umi::mmio;

struct MyDevice : mm::Device<mm::RW> {
    static constexpr mm::Addr base_address = 0x4000'0000;
};

using CTRL = mm::Register<MyDevice, 0x00, 32>;

// 1ビットフィールド — Set/Reset 自動生成
struct EN : mm::Field<CTRL, 0, 1> {};

// 2ビットフィールド（名前付き値） — デフォルトで安全（raw value() 不可）
struct MODE : mm::Field<CTRL, 1, 2> {
    using Output  = mm::Value<MODE, 0b01>;
    using AltFunc = mm::Value<MODE, 0b10>;
};

// 9ビット数値フィールド — raw value() 有効
struct PLLN : mm::Field<CTRL, 6, 9, mm::Numeric> {};

// 読み出し専用 + 数値
struct DR : mm::Field<CTRL, 0, 16, mm::RO, mm::Numeric> {};
```

### 5.2.1 フィールド型安全モデル

フィールドは**デフォルトで安全**: 名前付き `Value<>` 型と `raw<>()` エスケープハッチのみ受け付ける。
`Numeric` トレイトで raw `value()` アクセスをオプトイン。

| フィールド種別 | `value()` | `Value<>` 型 | `raw<>()`  |
|-----------|:---------:|:---------------:|:----------:|
| デフォルト（安全） | ブロック | Yes | Yes |
| `Numeric` 付き | Yes | Yes | Yes |
| 1ビット | — | `Set` / `Reset` 自動 | Yes |

**`Field<Reg, BitOffset, BitWidth, ...Traits>`** — 可変長トレイトパターン：
- トレイトにはアクセスポリシー（`RW`, `RO`, `WO`）や `Numeric` を任意の順序で含められる。
- デフォルトアクセスは `Inherit`（親レジスタから継承）。
- 1ビットフィールドは CRTP で `Set` と `Reset` 型エイリアスを自動提供。

**`raw<Field>(val)`** — エスケープハッチ：
- 任意のフィールドに対して型安全をバイパスし `DynamicValue` を生成。
- `const_cast` に類似 — 名前が意図的なバイパスを示す。
- `val` がリテラルの場合、コンパイル時に範囲チェック。

### 5.3 トランスポート選択

トランスポートは適切な型を構築して選択する：

```cpp
umi::mmio::DirectTransport<> direct;                    // Volatile ポインタ
umi::mmio::I2cTransport<MyI2c> i2c(hal_i2c, 0x68);     // HAL I2C
umi::mmio::SpiTransport<MySpi> spi(hal_spi);            // HAL SPI
```

すべてのトランスポートが同一の `write()`, `read()`, `modify()`, `is()`, `flip()` API を公開する。

### 5.4 上級パス

上級用途：

1. 単一バストランザクションでの複数フィールド書き込み、
2. `modify()` による read-modify-write、
3. カスタムエラーポリシー（trap、ignore、コールバック）、
4. I2C/SPI デバイス向け 16 ビットアドレス空間、
5. 設定可能なアドレスとデータのエンディアン。

---

## 6. コア抽象化階層

### 6.1 BitRegion

レジスタとフィールドの統一されたコンパイル時基底：

- `Register` = `IsRegister=true` の `BitRegion`（フル幅、アドレスオフセットを持つ）。
- `Field` = `IsRegister=false` の `BitRegion`（サブ幅、ビットオフセットを持つ）。

### 6.2 RegOps (CRTP 基底)

型安全な `write()`, `read()`, `modify()`, `is()`, `flip()` メソッドを提供。
実際のバス I/O は `Derived::reg_read()` / `Derived::reg_write()` に委譲。

### 6.3 ByteAdapter (CRTP ブリッジ)

RegOps の型付きレジスタ操作を `raw_read()` / `raw_write()` バイト操作に変換。
ホスト CPU とワイヤフォーマット間のエンディアン変換を処理。

### 6.4 Value と DynamicValue

- `Value<Field, EnumValue>`: シフト済み表現を持つコンパイル時定数。
- `DynamicValue<Field, T>`: 遅延範囲チェック付きランタイム値。

### 6.5 フィールドトレイトシステム

`Field<Reg, BitOffset, BitWidth, ...Traits>` は可変長パラメータパックでトレイトを受け取る：

- **アクセスポリシー抽出**: `detail::ExtractAccess_t<Traits...>` がパック内の `RW`/`RO`/`WO` を検出（デフォルト: `Inherit`）。
- **Numeric 検出**: `detail::contains_v<Numeric, Traits...>` が `value()` を有効化。
- **OneBitAliases**: 1ビットフィールドは `detail::OneBitBase<Field, Width>` (CRTP) で `Set`/`Reset` を継承。

トレイトは任意の順序で記述可能：
```cpp
// アクセスに関してはすべて等価:
struct A : mm::Field<REG, 0, 8, mm::RO, mm::Numeric> {};   // RO + Numeric
struct B : mm::Field<REG, 0, 8, mm::Numeric, mm::RO> {};   // 同じ
struct C : mm::Field<REG, 0, 8, mm::Numeric> {};           // Inherit + Numeric
struct D : mm::Field<REG, 0, 8> {};                        // Inherit, 安全
```

---

## 7. エラーハンドリングモデル

### 7.1 コンパイル時エラー

1. アクセスポリシー違反 → `static_assert` 失敗。
2. `consteval` コンテキストでの範囲外値 → `mmio_compile_time_error_value_out_of_range`。
3. 非 Numeric フィールドでの `value()` → concept 制約失敗（`requires(is_numeric)`）。
4. デバイスに許可されていないトランスポート → `static_assert` 失敗。

### 7.2 ランタイムエラーポリシー

`ErrorPolicy` テンプレートパラメータによるポリシーベース：

| ポリシー | 動作 |
|--------|----------|
| `AssertOnError` | `assert(false && msg)` (デフォルト) |
| `TrapOnError` | `__builtin_trap()` |
| `IgnoreError` | サイレント no-op |
| `CustomErrorHandler<fn>` | ユーザーコールバック |

---

## 8. テスト戦略

1. テストは関心事ごとに分割：アクセスポリシー、レジスタ/フィールド、トランスポート。
2. compile-fail テストが API 契約の強制を検証（`read_wo.cc`, `write_ro.cc`）。
3. トランスポートテストは `raw_read` / `raw_write` を実装した RAM バックドモックを使用。
4. ハードウェアレベル MMIO テストは実ハードウェアが必要であり、ホストテストの対象外。
5. CI はホストテストと compile-fail チェックを実行。

### 8.1 テストレイアウト

- `tests/test_main.cc`: テストエントリポイント
- `tests/test_access_policy.cc`: RW/RO/WO ポリシーの強制
- `tests/test_register_field.cc`: BitRegion, Register, Field, Value, マスク/シフトの正しさ
- `tests/test_transport.cc`: read/write/modify/is/flip 用 RAM バックドモックトランスポート
- `tests/compile_fail/read_wo.cc`: compile-fail ガード — 書き込み専用レジスタの読み出し
- `tests/compile_fail/write_ro.cc`: compile-fail ガード — 読み出し専用レジスタへの書き込み

### 8.2 テスト実行

```bash
xmake test                              # 全ターゲット
xmake test 'test_umimmio/*'             # ホストのみ
xmake test 'test_umimmio_compile_fail/*'  # compile-fail のみ
```

### 8.3 品質ゲート

- 全ホストテストパス
- compile-fail 契約テストパス (read_wo, write_ro)
- トランスポートモックテストが単一および複数フィールドの write, modify, is, flip をカバー
- 組み込みクロスビルドが CI でパス (gcc-arm)

---

## 9. サンプル戦略

サンプルは学習段階を表す：

1. `minimal`: 基本的なレジスタとフィールド定義、コンパイル時チェック付き。
2. `register_map`: 現実的な SPI ペリフェラルレジスタマップレイアウト。
3. `transport_mock`: ホスト側テスト用 RAM バックドモックトランスポート。

---

## 10. 短期改善計画

1. UART トランスポートヘッダーを追加。
2. マルチトランスポートサンプル（同一レジスタマップ、異なるバス）を追加。
3. ベンダー SVD/CMSIS ファイルからのレジスタマップ生成ワークフローを文書化。
4. デバッグ用バッチレジスタダンプユーティリティを追加。
5. **バイトトランスポートのテンプレートパラメータを HAL コンセプトで制約。**
   現在、`I2cTransport<I2C>` と `SpiTransport<SpiDevice>` は HAL ドライバパラメータに
   任意の型を受け入れる（ダックタイピング）。明示的な `requires` 句を追加すると、
   深いテンプレートエラーの代わりに明確なコンセプト不一致エラーが出力される：

   ```cpp
   // 現在: 制約なし — エラーは raw_read/raw_write の奥深くで発生
   template <typename I2C, ...>
   class I2cTransport : public ByteAdapter<...> { ... };

   // 改善後: コンセプト制約 — インスタンス化時に早期かつ明確なエラー
   template <umi::hal::I2cTransport I2C, ...>
   class I2cTransport : public ByteAdapter<...> { ... };
   ```

   **前提条件:** `umi::hal::I2cTransport` コンセプトのシグネチャ
   (`write(addr, reg, tx)`) は `mmio::I2cTransport` が実際に呼び出す
   (`write(addr, payload)` — 個別 `reg` なし) と一致しない。
   まず HAL コンセプトを整合させる必要がある（`03_ARCHITECTURE.md` §10.3 参照）。

6. **`umi::hal` と `umi::mmio` の Transport 型の命名衝突を解決。**
   両名前空間が `I2cTransport` と `SpiTransport` を定義しているが、
   抽象化レベルが異なる：

   | 名前 | 名前空間 | 種別 | 抽象化 |
   |------|-----------|------|-------------|
   | `I2cTransport` | `umi::hal` | concept | バスレベル: バイト送受信 |
   | `I2cTransport` | `umi::mmio` | class | レジスタレベル: アドレスフレーミング + エンディアン変換 |

   mmio クラスを `I2cRegisterBus` または `I2cRegisterTransport` にリネームして
   曖昧さを排除することを検討する。mmio Transport は HAL コンセプトを *消費* する
   （HAL 準拠型をテンプレートパラメータとして受け取る）ため、競合する抽象化ではなく
   相補的な関係にある。

---

## 11. 設計原則

1. ゼロコスト抽象化 — すべてのディスパッチはコンパイル時に解決。
2. 型安全 — アクセス違反はランタイムバグではなくコンパイルエラー。
3. トランスポート非依存 — 同一のレジスタマップ、任意のバス。
4. ポリシーベース — エラーハンドリング、範囲チェック、エンディアンが設定可能。
5. 組み込みファースト — ヒープなし、例外なし、RTTI なし。
