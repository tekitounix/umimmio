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

書き込み専用レジスタの読み出し、または読み出し専用レジスタへの書き込みはコンパイルエラーとなる。
強制は `read()`、`write()`、`modify()` 等に対する C++20 `requires` 句で行い、
`Readable`、`Writable`、`ReadWritable`、`ReadableValue`/`WritableValue`/`ModifiableValue` concept を使用する。

W1C (Write-1-to-Clear) フィールドは `WriteBehavior::ONE_TO_CLEAR` を持ち、`clear()` でのみ受け付けられる。
`IsW1C` concept がこれを識別し、`NotW1C` が `modify()` と `flip()` から除外する。

### 2.3 トランスポート抽象化

レジスタ操作はバスプロトコルから分離されている。
同一の `Device/Block/Register/Field` 階層が以下で動作する：

1. Direct volatile ポインタ（Cortex-M、RISC-V メモリマップドペリフェラル）。
2. I2C バス（HAL ベースまたはビットバング）。
3. SPI バス（HAL ベースまたはビットバング）。

トランスポートはテンプレートパラメータであり、基底クラスポインタではない。

### 2.4 範囲チェック

フィールド値は可能な限りコンパイル時に範囲チェックされる：

1. フィールド幅を超えるリテラルを持つ `value()` は `if consteval` で `mmio_compile_time_error_value_out_of_range` を発生。
2. ランタイム `DynamicValue` は書き込み/変更時に `CheckPolicy` + `ErrorPolicy` でチェック。
3. `value()` は `std::unsigned_integral` を要求 — 符号付き値はコンパイルエラー。
4. `BitRegion` は 5 つの `static_assert` でオフセット、幅、レジスタ幅の整合性を検証。

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
│   ├── ja/
│   └── plans/
├── examples/
│   ├── minimal.cc
│   ├── register_map.cc
│   └── transport_mock.cc
├── include/umimmio/
│   ├── mmio.hh              # アンブレラヘッダー
│   ├── register.hh          # コア: RegOps, ByteAdapter, BitRegion, Field, Value, concepts
│   ├── protected.hh         # Protected<T, LockPolicy>, Guard, ロックポリシー
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
    ├── test_spi_bitbang.cc
    ├── test_protected.cc
    ├── compile_fail/
    │   ├── read_wo.cc
    │   ├── write_ro.cc
    │   ├── write_ro_value.cc
    │   ├── value_typesafe.cc
    │   ├── value_signed.cc
    │   ├── modify_w1c.cc
    │   ├── flip_w1c.cc
    │   └── field_overflow.cc
    └── xmake.lua
```

---

## 4. 成長レイアウト

```text
lib/umimmio/
├── include/umimmio/
│   ├── mmio.hh
│   ├── register.hh
│   ├── protected.hh
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
    │   └── *.cc
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
| `Device<Access, Transports...>` | アクセスポリシーと許可トランスポートを持つデバイスルート。MMIO デバイスは `base_address` をオーバーライド。 |
| `Register<Device, Offset, Bits, Access, Reset, W1cMask>` | デバイス内のオフセットにあるレジスタ。`W1cMask` は W1C ビットを指定。 |
| `Field<Reg, BitOffset, BitWidth, ...Traits>` | レジスタ内のビットフィールド（可変長トレイト） |
| `Value<Field, val>` | Field の名前付き定数 |
| `DynamicValue<Field, T>` | Field のランタイム値 |
| `RegisterReader<Reg>` | `read(Register{})` の戻り値型 — `bits()`、`get()`、`is()` を提供 |
| `UnknownValue<Reg>` | `read_variant()` で名前付き値にマッチしない場合のセンチネル型 |
| `Numeric` | トレイト: Field で raw `value()` を有効化 |
| `raw<Field>(val)` | エスケープハッチ: 任意の Field への raw 値 |
| `WriteBehavior` | 列挙型: `NORMAL` または `ONE_TO_CLEAR` |

トランスポート型：

| トランスポート | 用途 |
|-----------|----------|
| `DirectTransport` | メモリマップド I/O (volatile ポインタアクセス) |
| `I2cTransport` | HAL 互換 I2C ペリフェラルドライバ |
| `SpiTransport` | HAL 互換 SPI ペリフェラルドライバ |
| `BitBangI2cTransport` | GPIO によるソフトウェア I2C |
| `BitBangSpiTransport` | GPIO によるソフトウェア SPI |

アクセスポリシー：

| ポリシー | `read()` | `write()` | `modify()` | `clear()` | `WriteBehavior` |
|--------|:--------:|:---------:|:----------:|:---------:|:---------------:|
| `RW` | Yes | Yes | Yes | — | `NORMAL` |
| `RO` | Yes | No | No | — | `NORMAL` |
| `WO` | No | Yes | No | — | `NORMAL` |
| `W1C` | Yes | — | No | Yes | `ONE_TO_CLEAR` |

操作一覧：

| 操作 | 用途 | 制約 |
|-----------|---------|------------|
| `read(Reg{})` | レジスタ読み出し → `RegisterReader<Reg>` | `Readable<Reg>` |
| `read(Field{})` | フィールド読み出し → 抽出値 | `Readable<Field>` |
| `write(v1, v2, ...)` | 値の書き込み（リセット値ベース） | `WritableValue` |
| `modify(v1, v2, ...)` | Read-modify-write | `ModifiableValue`（W1C 除外） |
| `is(v)` | フィールド/レジスタ値の比較 | `ReadableValue` |
| `flip(F{})` | 1ビットフィールドのトグル | `ReadWritable && NotW1C` |
| `clear(F{})` | W1C フィールドの Write-1-to-clear | `IsW1C<F>` |
| `reset(Reg{})` | `Reg::reset_value()` の書き込み | `Writable<Reg>` |
| `read_variant(F{}, V1{}, ..., VN{})` | フィールド値のパターンマッチ → `std::variant` | — |

並行性型：

| 型 | 用途 |
|------|---------|
| `Protected<T, LockPolicy>` | T をラップし、`lock()` → `Guard` 経由でのみアクセス可能 |
| `Guard<T, LockPolicy>` | Protected の内部値への RAII スコープ付きアクセス |
| `MutexPolicy<MutexT>` | RTOS ミューテックスラッパー |
| `NoLockPolicy` | シングルスレッドまたはテスト用の No-op ロック |

`CriticalSectionPolicy`（ARM Cortex-M `cpsid`/`cpsie`）は `umiport` が提供 — `<umiport/platform/embedded/critical_section.hh>` を参照。

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

// W1C ステータスレジスタ（W1cMask 指定）
using SR = mm::Register<MyDevice, 0x04, 32, mm::RW, 0, 0x0003>;

// W1C フィールド — Clear エイリアスが自動生成（Set/Reset の代わり）
struct OVR : mm::Field<SR, 0, 1, mm::W1C> {};
struct EOC : mm::Field<SR, 1, 1, mm::W1C> {};
struct READY : mm::Field<SR, 8, 1> {};  // 通常の RW フィールド
```

### 5.2.1 フィールド型安全モデル

フィールドは**デフォルトで安全**: 名前付き `Value<>` 型と `raw<>()` エスケープハッチのみ受け付ける。
`Numeric` トレイトで raw `value()` アクセスをオプトイン。

| フィールド種別 | `value()` | `Value<>` 型 | `raw<>()`  |
|-----------|:---------:|:---------------:|:----------:|
| デフォルト（安全） | ブロック | Yes | Yes |
| `Numeric` 付き | Yes（符号なしのみ） | Yes | Yes |
| 1ビット RW | — | `Set` / `Reset` 自動 | Yes |
| 1ビット W1C | — | `Clear` 自動 | Yes |

**`Field<Reg, BitOffset, BitWidth, ...Traits>`** — 可変長トレイトパターン：
- トレイトにはアクセスポリシー（`RW`, `RO`, `WO`, `W1C`）や `Numeric` を任意の順序で含められる。
- デフォルトアクセスは `Inherit`（親レジスタから継承）。
- 1ビット RW フィールドは `Set` と `Reset` 型エイリアスを自動提供。
- 1ビット W1C フィールドは `Clear` 型エイリアスを自動提供。

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

すべてのトランスポートが同一の `write()`, `read()`, `modify()`, `is()`, `flip()`, `clear()`, `reset()` API を公開する。

### 5.4 上級パス

上級用途：

1. 単一バストランザクションでの複数フィールド書き込み、
2. `modify()` による read-modify-write、
3. カスタムエラーポリシー（trap、ignore、コールバック）、
4. I2C/SPI デバイス向け 16 ビットアドレス空間、
5. `std::endian` による設定可能なアドレスとデータのエンディアン、
6. `clear()` による W1C フィールドハンドリング、
7. `reset()` によるレジスタリセット、
8. `read_variant()` によるパターンマッチ付きフィールド読み出し、
9. `Protected<Transport, LockPolicy>` による ISR 安全アクセス（プラットフォーム固有のロックポリシーを DI で注入）。

---

## 6. コア抽象化階層

### 6.1 BitRegion

レジスタとフィールドの統一されたコンパイル時基底：

- `Register` = `IsRegister=true` の `BitRegion`（フル幅、アドレスオフセットを持つ）。
- `Field` = `IsRegister=false` の `BitRegion`（サブ幅、ビットオフセットを持つ）。
- 5 つの `static_assert` で検証：ビット幅 > 0、オフセット + 幅 ≤ レジスタ幅、
  レジスタ幅は 2 の累乗、レジスタ幅 ≥ 8、ゼロ幅レジスタなし。

### 6.2 RegOps (deducing this)

型安全な `write()`, `read()`, `modify()`, `is()`, `flip()`, `clear()`, `reset()`, `read_variant()` メソッドを提供。
C++23 deducing this (P0847R7) を使用 — CRTP の `Derived` パラメータなし。
実際のバス I/O は `self.reg_read()` / `self.reg_write()` に委譲。

Concept 制約：

| Concept | 適用先 | 説明 |
|---------|-----------|-------------|
| `Readable<T>` | Register/Field | `can_read == true` |
| `Writable<T>` | Register/Field | `can_write == true` |
| `ReadWritable<T>` | Register/Field | 読み書き両方可能 |
| `IsRegister<T>` | Register | `is_register == true` |
| `IsField<T>` | Field | `is_register == false` |
| `IsW1C<T>` | Field | `write_behavior == ONE_TO_CLEAR` |
| `NotW1C<T>` | Field | W1C でない |
| `ReadableValue<V>` | Value/DynamicValue | 親領域が読み出し可能 |
| `WritableValue<V>` | Value/DynamicValue | 親領域が書き込み可能 |
| `ModifiableValue<V>` | Value/DynamicValue | 書き込み可能かつ親が W1C でない |

### 6.3 ByteAdapter (deducing this)

RegOps の型付きレジスタ操作を `raw_read()` / `raw_write()` バイト操作に変換。
C++23 deducing this を使用 — CRTP パラメータなし。
`std::byteswap`（`<bit>`）を使用してホスト CPU とワイヤフォーマット間のエンディアン変換を処理。
エンディアンは `std::endian` で表現（カスタム `Endian` enum なし）。

### 6.4 Value と DynamicValue

- `Value<RegionT, EnumValue>`: シフト済み表現を持つコンパイル時定数。
  主型参照として `RegionType` を使用（`FieldType` ではない）。
- `DynamicValue<RegionT, T>`: 遅延範囲チェック付きランタイム値。

### 6.5 フィールドトレイトシステム

`Field<Reg, BitOffset, BitWidth, ...Traits>` は可変長パラメータパックでトレイトを受け取る：

- **アクセスポリシー抽出**: `detail::ExtractAccess_t<Traits...>` がパック内の `RW`/`RO`/`WO`/`W1C` を検出（デフォルト: `Inherit`）。
  `detail::IsAccessPolicy<T>` concept で確実な検出を実現。
- **Numeric 検出**: `detail::contains_v<Numeric, Traits...>` が `value()` を有効化。
- **OneBitAliases**: 1ビット RW フィールドは `detail::OneBitBase` で `Set`/`Reset` を継承。
- **OneBitW1CAliases**: 1ビット W1C フィールドは `detail::OneBitBase` で `Clear` を継承。

トレイトは任意の順序で記述可能：
```cpp
// アクセスに関してはすべて等価:
struct A : mm::Field<REG, 0, 8, mm::RO, mm::Numeric> {};   // RO + Numeric
struct B : mm::Field<REG, 0, 8, mm::Numeric, mm::RO> {};   // 同じ
struct C : mm::Field<REG, 0, 8, mm::Numeric> {};           // Inherit + Numeric
struct D : mm::Field<REG, 0, 8> {};                        // Inherit, 安全
struct E : mm::Field<SR, 0, 1, mm::W1C> {};                // W1C: Clear エイリアス
```

### 6.6 RegisterReader

`read(Register{})` は raw 値ではなく `RegisterReader<Reg>` を返す。
これによりフルエントなチェーンアクセスが可能：

```cpp
auto cfg = hw.read(ConfigReg{});
auto en  = cfg.get(ConfigEnable{});   // フィールド値の抽出
bool is_fast = cfg.is(ModeFast{});    // 名前付き値とのマッチ
uint32_t raw = cfg.bits();           // raw レジスタ値
```

`RegisterReader` は raw 値を保持し、以下を提供：
- `bits()` — raw レジスタ値
- `get(Field{})` — フィールド値の抽出
- `is(ValueType{})` — 名前付き値とのマッチ
- `RegValueType` への暗黙変換（後方互換性のため）

---

## 7. エラーハンドリングモデル

### 7.1 コンパイル時エラー

1. アクセスポリシー違反 → concept 名を伴う `requires` 句の失敗。
2. `consteval` コンテキストでの範囲外値 → `mmio_compile_time_error_value_out_of_range`。
3. 非 Numeric フィールドでの `value()` → concept 制約失敗（`requires(is_numeric)`）。
4. 符号付き型での `value()` → concept 制約失敗（`std::unsigned_integral`）。
5. デバイスに許可されていないトランスポート → `static_assert` 失敗。
6. `BitRegion` オーバーフロー（オフセット + 幅 > レジスタ幅） → `static_assert` 失敗。
7. `modify()` での W1C フィールド → `ModifiableValue` concept が W1C を拒否。
8. `flip()` での W1C フィールド → `NotW1C` concept が W1C を拒否。

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

1. テストは関心事ごとに分割：アクセスポリシー、レジスタ/フィールド、トランスポート、保護付きアクセス。
2. compile-fail テストが API 契約の強制を検証（8 テストファイル）。
3. トランスポートテストは `reg_read` / `reg_write` を実装した RAM バックドモックを使用。
4. ハードウェアレベル MMIO テストは実ハードウェアが必要であり、ホストテストの対象外。
5. CI はホストテストと compile-fail チェックを実行。

### 8.1 テストレイアウト

- `tests/test_main.cc`: テストエントリポイント
- `tests/test_access_policy.cc`: RW/RO/WO/W1C ポリシーの強制、WriteBehavior
- `tests/test_register_field.cc`: BitRegion, Register, Field, Value, マスク/シフト、RegisterReader
- `tests/test_transport.cc`: read/write/modify/is/flip/clear/reset/read_variant 用 RAM バックドモックトランスポート
- `tests/test_spi_bitbang.cc`: SPI, I2C, BitBang トランスポート、ByteAdapter エンディアンテスト
- `tests/test_protected.cc`: NoLockPolicy による Protected<T, LockPolicy>
- `tests/compile_fail/read_wo.cc`: 書き込み専用レジスタの読み出し
- `tests/compile_fail/write_ro.cc`: 読み出し専用レジスタへの書き込み
- `tests/compile_fail/write_ro_value.cc`: value 経由での読み出し専用レジスタへの書き込み
- `tests/compile_fail/value_typesafe.cc`: 非 Numeric フィールドでの `value()`
- `tests/compile_fail/value_signed.cc`: 符号付き整数での `value()`
- `tests/compile_fail/modify_w1c.cc`: W1C フィールドでの `modify()`
- `tests/compile_fail/flip_w1c.cc`: W1C フィールドでの `flip()`
- `tests/compile_fail/field_overflow.cc`: BitRegion オーバーフロー（オフセット + 幅 > レジスタ幅）

### 8.2 テスト実行

```bash
xmake test                              # 全ターゲット
xmake test 'test_umimmio/*'             # ホストのみ
xmake test 'test_umimmio_compile_fail/*'  # compile-fail のみ
```

### 8.3 品質ゲート

- 全ホストテストパス（59 テスト）
- 全 compile-fail 契約テストパス（8 テスト）
- トランスポートモックテストが単一および複数フィールドの write, modify, is, flip, clear, reset, read_variant をカバー
- W1C 安全性: modify_w1c, flip_w1c compile-fail テストパス
- BitRegion オーバーフロー: field_overflow compile-fail テストパス
- 符号付き値拒否: value_signed compile-fail テストパス
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
5. **バイトトランスポートのテンプレートパラメータを HAL concept で制約。**
6. **`umi::hal` と `umi::mmio` の Transport 型の命名衝突を解決。**

---

## 11. 設計原則

1. ゼロコスト抽象化 — すべてのディスパッチはコンパイル時に解決。
2. 型安全 — アクセス違反はランタイムバグではなくコンパイルエラー。
3. トランスポート非依存 — 同一のレジスタマップ、任意のバス。
4. ポリシーベース — エラーハンドリング、範囲チェック、エンディアンが設定可能。
5. 組み込みファースト — ヒープなし、例外なし、RTTI なし。

---

## 12. write() / modify() セマンティクスガイド

### 12.1 セマンティクスの違い

| 操作 | ベース値 | セマンティクス | 安全性 |
|-----------|-----------|-----------|:------:|
| `write(v1, v2, ...)` | `reset_value()` | リセット状態からのレジスタ初期化 | ✅ |
| `modify(v1, v2, ...)` | 現在の値 (RMW) | 特定フィールドの変更、他を保持 | ✅ |
| `write(single_v)` | `reset_value()` | レジスタ初期化 — 他のフィールドはリセット | ⚠️ |

### 12.2 使用ルール

1. **初期化**: 関連するすべてのフィールドを指定して `write()` を使用。
2. **ランタイム変更**: 特定フィールドの変更には `modify()` を使用。
3. **単一フィールド書き込み**: `write(v)` は他のフィールドを `reset_value()` にリセットする。
   これは単一フィールドレジスタまたは完全リセット用。
   ランタイムでの単一フィールド変更には `modify()` を使用。

### 12.3 W1C フィールド

W1C フィールドは `clear()` を使用する：

```cpp
hw.clear(OVR{});             // ✅ 正しい: OVR のみに 1 を書き込み
hw.modify(OVR::Clear{});     // ✗ コンパイルエラー: W1C は ModifiableValue でない
hw.flip(OVR{});              // ✗ コンパイルエラー: W1C は NotW1C でない
```

`modify()` 中、親レジスタの W1C ビットは `Register::w1c_mask` により
ライトバック前に自動的に 0 にマスクされる。これにより、他のフィールドの
read-modify-write 操作中に W1C ステータスビットが意図せずクリアされることを防ぐ。

### 12.4 アトミック性

`modify()` は read-modify-write を実行し、**決してアトミックではない**。
ISR 安全なアクセスにはプラットフォーム固有のポリシーを注入した `Protected<Transport, LockPolicy>` を使用：

```cpp
// ARM Cortex-M: #include <umiport/platform/embedded/critical_section.hh>
using umi::port::platform::CriticalSectionPolicy;
Protected<DirectTransport<>, CriticalSectionPolicy> protected_hw;

auto guard = protected_hw.lock();   // __disable_irq()
guard->modify(ConfigEnable::Set{}); // ISR 安全な RMW
// ~Guard() → __enable_irq() (RAII)
```

非 ARM プラットフォームでは `MutexPolicy<MutexT>` または `NoLockPolicy` を適宜使用。

### 12.5 reset()

`reset(Reg{})` はレジスタの `reset_value()` を直接書き込む。これは純粋な書き込み
（read-modify-write ではない）であり、ハードウェアを初期状態に戻すのに適している。
