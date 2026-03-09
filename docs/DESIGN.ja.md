# umimmio 設計

[English](DESIGN.md)

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
2. I2C バス。
3. SPI バス。

トランスポートはテンプレートパラメータであり、基底クラスポインタではない。

### 2.4 範囲チェック

フィールド値は可能な限りコンパイル時に範囲チェックされる：

1. フィールド幅を超えるリテラルを持つ `value()` は `if consteval` で `detail::mmio_compile_time_error_value_out_of_range` を発生。
2. ランタイム `DynamicValue` は書き込み/変更時に `CheckPolicy` + `ErrorPolicy` でチェック。
3. `value()` は `std::unsigned_integral` を要求 — 符号付き値はコンパイルエラー。
4. `BitRegion` は 5 つの `static_assert` でオフセット、幅、レジスタ幅の整合性を検証。

### 2.5 依存関係

`umimmio` は C++23 標準ライブラリヘッダーのみに依存。
テストはアサーション用に `umitest` に依存。

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
├── examples/
│   ├── minimal.cc
│   ├── register_map.cc
│   └── transport_mock.cc
├── include/umimmio/
│   ├── mmio.hh              # アンブレラヘッダー
│   ├── policy.hh            # 基盤: AccessPolicy、トランスポートタグ、エラーポリシー
│   ├── region.hh            # データモデル: Device, Register, Field, Value, concepts
│   ├── ops.hh               # 操作: RegOps, ByteAdapter
│   └── transport/
│       ├── detail.hh        # アドレスエンコード共通ヘルパー
│       ├── direct.hh        # DirectTransport (volatile ポインタ)
│       ├── i2c.hh           # I2cTransport (HAL ベース)
│       └── spi.hh           # SpiTransport (HAL ベース)
└── tests/
    ├── test_main.cc
    ├── test_access_policy.cc
    ├── test_register_field.cc
    ├── test_transport.cc
    ├── test_byte_transport.cc
    ├── compile_fail/
    │   ├── clear_non_w1c.cc
    │   ├── cross_register_write.cc
    │   ├── field_overflow.cc
    │   ├── flip_ro.cc
    │   ├── flip_w1c.cc
    │   ├── flip_wo.cc
    │   ├── modify_w1c.cc
    │   ├── modify_wo.cc
    │   ├── read_field_eq_int.cc
    │   ├── read_wo.cc
    │   ├── value_signed.cc
    │   ├── value_typesafe.cc
    │   ├── write_ro.cc
    │   ├── write_ro_value.cc
    │   └── write_zero_args.cc
    └── xmake.lua
```

---

## 4. プログラミングモデル

### 4.0 API リファレンス

パブリックエントリポイント: `include/umimmio/mmio.hh`

コア型：

| 型 | 用途 |
|------|---------|
| `Device<Access, Transports...>` | アクセスポリシーと許可トランスポートを持つデバイスルート。MMIO デバイスは `base_address` をオーバーライド。 |
| `Block<Parent, BaseAddr, Access>` | Device 内のアドレスサブ領域（親のトランスポートを継承）。 |
| `Register<Device, Offset, Bits, Access, Reset, W1cMask>` | デバイス内のオフセットにあるレジスタ。`W1cMask` は W1C ビットを指定。 |
| `Field<Reg, BitOffset, BitWidth, ...Traits>` | レジスタ内のビットフィールド（可変長トレイト） |
| `Value<Field, val>` | Field の名前付き定数 |
| `DynamicValue<Field, T>` | Field のランタイム値 |
| `RegionValue<R>` | `read()` と `get()` の統一戻り値型 — `bits()` は常に利用可能。レジスタ: `get()`, `is()`。フィールド: 型付き `==` のみ |
| `UnknownValue<Reg>` | `read_variant()` で名前付き値にマッチしない場合のセンチネル型 |
| `Numeric` | トレイト: Field で raw `value()` を有効化 |
| `Inherit` | センチネル: Field が親 Register からアクセスポリシーを継承 |
| `WriteBehavior` | 列挙型: `NORMAL` または `ONE_TO_CLEAR` |

トランスポート型：

| トランスポート | 用途 |
|-----------|----------|
| `DirectTransport` | メモリマップド I/O (volatile ポインタアクセス) |
| `I2cTransport` | HAL 互換 I2C ペリフェラルドライバ |
| `SpiTransport` | HAL 互換 SPI ペリフェラルドライバ |

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
| `read(Reg{})` | レジスタ読み出し → `RegionValue<Reg>` | `Readable<Reg>` |
| `read(Field{})` | フィールド読み出し → `RegionValue<F>`（raw は `.bits()`） | `Readable<Field>` |
| `write(v1, v2, ...)` | 値の書き込み（リセット値ベース） | `WritableValue` |
| `modify(v1, v2, ...)` | Read-modify-write | `ModifiableValue`（W1C 除外） |
| `is(v)` | フィールド/レジスタ値の比較 | `ReadableValue` |
| `flip(F{})` | 1ビットフィールドのトグル（W1Cマスク適用） | `ReadWritable && NotW1C` |
| `clear(F{})` | W1C フィールドの Write-1-to-clear（混合レジスタはRMW） | `IsW1C<F>` |
| `reset(Reg{})` | `Reg::reset_value()` の書き込み | `Writable<Reg>` |
| `read_variant(F{}, V1{}, ..., VN{})` | フィールド値のパターンマッチ → `std::variant` | — |

Register/Field の静的メソッド：

| メソッド | 用途 | 利用可能条件 |
|--------|---------|-------------|
| `<Register>::value(T)` | 範囲チェック付き `DynamicValue` 生成 | Register（常に） |
| `<Field>::value(T)` | 範囲チェック付き `DynamicValue` 生成 | `Numeric` トレイト付き Field |
| `mask()` | コンパイル時ビットマスク | Register, Field |
| `reset_value()` | コンパイル時リセット値 | Register, Field（継承） |

並行性:

`modify()` はアトミックではない（read-modify-write）。ISR 安全またはマルチコンテキスト
アクセスでは、呼び出し側が外部でアクセスを直列化する必要がある（例: 割り込み禁止、
スコープ付きロック）。パターンは §9.4 を参照。

### 4.1 最小パス

Direct MMIO の最小フロー：

1. ベースアドレスとアクセスポリシーを持つ `Device` を定義。
2. デバイス内に `Register` を定義。
3. レジスタ内に `Field` を定義。
4. `DirectTransport` を構築。
5. `transport.write(Field::Set{})` または `transport.read(Field{})` を呼び出し。

### 4.2 レジスタマップの構成

推奨スタイルは**階層型ネスト**: Device が Register を含み、
Register が Field を含み、Field が名前付き Value を含む。
これはデバイスの物理構造をそのまま反映し、複数レジスタが同名フィールド（`EN`, `MODE` 等）を
持つ場合の名前衝突を防ぐ。

```cpp
namespace mm = umi::mmio;

struct MyDevice : mm::Device<mm::RW> {
    static constexpr mm::Addr base_address = 0x4000'0000;

    struct CTRL : mm::Register<MyDevice, 0x00, 32> {
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
    };

    // W1C ステータスレジスタ（W1cMask 指定）
    struct SR : mm::Register<MyDevice, 0x04, 32, mm::RW, 0, 0x0003> {
        // W1C フィールド — Clear エイリアスが自動生成（Set/Reset の代わり）
        struct OVR : mm::Field<SR, 0, 1, mm::W1C> {};
        struct EOC : mm::Field<SR, 1, 1, mm::W1C> {};
        struct READY : mm::Field<SR, 8, 1> {};  // 通常の RW フィールド
    };
};
```

> **Note:** フラットスタイル（レジスタやフィールドをデバイス外の独立した struct として
> 定義する）も有効な C++ であり正しくコンパイルされる。ただし階層型スタイルを推奨する。
> 名前衝突を自然に防ぎ、型パス（`MyDevice::CTRL::MODE::Output`）が
> 自己文書化されるため。

### 4.2.1 フィールド型安全モデル

フィールドは**デフォルトで安全**: 名前付き `Value<>` 型のみ受け付ける。
`Numeric` トレイトで raw `value()` アクセスをオプトイン。
読み出し側の raw 値取得には `RegionValue::bits()` を使用。

| フィールド種別 | `value()` | `Value<>` 型 |
|-----------|:---------:|:---------------:|
| デフォルト（安全） | ブロック | Yes |
| `Numeric` 付き | Yes（符号なしのみ） | Yes |
| 1ビット RW | — | `Set` / `Reset` 自動 |
| 1ビット W1C | — | `Clear` 自動 |

**`Field<Reg, BitOffset, BitWidth, ...Traits>`** — 可変長トレイトパターン：
- トレイトにはアクセスポリシー（`RW`, `RO`, `WO`, `W1C`）や `Numeric` を任意の順序で含められる。
- デフォルトアクセスは `Inherit`（親レジスタから継承）。
- 1ビット RW フィールドは `Set` と `Reset` 型エイリアスを自動提供。
- 1ビット W1C フィールドは `Clear` 型エイリアスを自動提供。

### 4.3 トランスポート選択

トランスポートは適切な型を構築して選択する：

```cpp
umi::mmio::DirectTransport<> direct;                    // Volatile ポインタ
umi::mmio::I2cTransport<MyI2c> i2c(hal_i2c, 0x68);     // HAL I2C
umi::mmio::SpiTransport<MySpi> spi(hal_spi);            // HAL SPI
```

すべてのトランスポートが同一の `write()`, `read()`, `modify()`, `is()`, `flip()`, `clear()`, `reset()` API を公開する。

### 4.4 上級パス

上級用途：

1. 単一バストランザクションでの複数フィールド書き込み、
2. `modify()` による read-modify-write、
3. カスタムエラーポリシー（trap、ignore、コールバック）、
4. I2C/SPI デバイス向け 16 ビットアドレス空間、
5. `std::endian` による設定可能なアドレスとデータのエンディアン、
6. `clear()` による W1C フィールドハンドリング、
7. `reset()` によるレジスタリセット、
8. `read_variant()` によるパターンマッチ付きフィールド読み出し、
9. トランスポート操作を呼び出し側が提供するクリティカルセクションでラップする ISR 安全アクセス。

---

## 5. コア抽象化階層

### 5.1 BitRegion

レジスタとフィールドの統一されたコンパイル時基底：

- `Register` = `IsRegister=true` の `BitRegion`（フル幅、アドレスオフセットを持つ）。
- `Field` = `IsRegister=false` の `BitRegion`（サブ幅、ビットオフセットを持つ）。
- 5 つの `static_assert` で検証：ビット幅 > 0、レジスタ幅 ≤ 64、
  オフセット + 幅 ≤ レジスタ幅、レジスタの BitOffset が 0、
  レジスタの BitWidth が RegBitWidth と等しい。

### 5.2 RegOps (deducing this)

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

### 5.3 ByteAdapter (deducing this)

RegOps の型付きレジスタ操作を `raw_read()` / `raw_write()` バイト操作に変換。
C++23 deducing this を使用 — CRTP パラメータなし。
`std::byteswap`（`<bit>`）を使用してホスト CPU とワイヤフォーマット間のエンディアン変換を処理。
エンディアンは `std::endian` で表現（カスタム `Endian` enum なし）。

### 5.4 Value と DynamicValue

- `Value<RegionT, EnumValue>`: シフト済み表現を持つコンパイル時定数。
  親の Register または Field 型を `RegionType` エイリアスで保持。
- `DynamicValue<RegionT, T>`: 遅延範囲チェック付きランタイム値。

### 5.5 フィールドトレイトシステム

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

### 5.6 RegionValue

`read(Register{})` は raw 値ではなく `RegionValue<Reg>` を返す。
`read(Field{})` は `RegionValue<F>` を返す。どちらも同一の `RegionValue<R>` テンプレートが
レジスタまたはフィールドに特殊化されたもの。
これによりフルエントなチェーンアクセスが可能：

```cpp
auto cfg = hw.read(ConfigReg{});
auto en  = cfg.get(ConfigEnable{});   // RegionValue<ConfigEnable>
bool is_fast = cfg.is(ModeFast{});    // 名前付き値とのマッチ
uint32_t raw = cfg.bits();           // raw レジスタ値
auto en_raw = en.bits();             // raw フィールド値（エスケープハッチ）
```

`RegionValue<R>` は raw 値を保持し、以下を提供：
- `bits()` — raw 値（常に利用可能）
- `operator==(RegionValue)` — 同一領域の等値比較（常に利用可能）
- `get(Field{})` — フィールド値の抽出（`RegionValue<F>` を返す、レジスタのみ）
- `is(ValueType{})` — 名前付き値とのマッチ（レジスタのみ）
- `operator==` with `Value`/`DynamicValue` — 型付き比較（フィールドのみ）

### 5.7 read_variant()

`read_variant()` はフィールドを読み出し、その値を名前付き `Value<>` 型のセットに対して
パターンマッチし、`std::variant` を返す。マッチしない場合は最後の代替型として
`UnknownValue<F>` が返される。

```cpp
auto result = hw.read_variant<CTRL::MODE,
                              CTRL::MODE::Normal,
                              CTRL::MODE::Fast,
                              CTRL::MODE::LowPwr>();

// result の型: std::variant<Normal, Fast, LowPwr, UnknownValue<MODE>>

std::visit([](auto v) {
    if constexpr (std::is_same_v<decltype(v), CTRL::MODE::Fast>) {
        // Fast モードの処理
    } else if constexpr (std::is_same_v<decltype(v), UnknownValue<CTRL::MODE>>) {
        // 予期しない値の処理 — v.value に raw ビット値を保持
    }
}, result);
```

フィールドが多くの名前付き値を持ち、呼び出し側が `std::visit` で網羅的に
処理したい場合に特に有用である。

---

## 6. エラーハンドリングモデル

### 6.1 コンパイル時エラー

1. アクセスポリシー違反 → concept 名を伴う `requires` 句の失敗。
2. `consteval` コンテキストでの範囲外値 → `detail::mmio_compile_time_error_value_out_of_range`。
3. 非 Numeric フィールドでの `value()` → concept 制約失敗（`requires(is_numeric)`）。
4. 符号付き型での `value()` → concept 制約失敗（`std::unsigned_integral`）。
5. デバイスに許可されていないトランスポート → `static_assert` 失敗。
6. `BitRegion` オーバーフロー（オフセット + 幅 > レジスタ幅） → `static_assert` 失敗。
7. `modify()` での W1C フィールド → `ModifiableValue` concept が W1C を拒否。
8. `flip()` での W1C フィールド → `NotW1C` concept が W1C を拒否。
9. `RegionValue == 整数` → `operator==` なし（raw アクセスは `.bits()` を使用）。

### 6.2 ランタイムエラーポリシー

`ErrorPolicy` テンプレートパラメータによるポリシーベース：

| ポリシー | 動作 |
|--------|----------|
| `AssertOnError` | `assert(false && msg)` (デフォルト) |
| `TrapOnError` | `std::abort()` |
| `IgnoreError` | サイレント no-op |
| `CustomErrorHandler<fn>` | ユーザーコールバック |

各ポリシーは 2 つのエントリポイントを提供：

| エントリポイント | トリガー |
|---|---|
| `on_range_error(msg)` | フィールド幅を超える値（プログラミングエラー） |
| `on_transport_error(msg)` | HAL ドライバが失敗を報告（バスエラー、NACK、タイムアウト） |

I2cTransport と SpiTransport は `if constexpr` で HAL ドライバの戻り値型が
`void` か `bool` 変換可能かを判定。HAL が偽値を返した場合、
`on_transport_error()` が呼び出される。void 戻りの HAL ではチェックをスキップ。

---

## 7. テスト戦略

[TESTING.md](TESTING.md) を参照。

---

## 8. 設計原則

1. ゼロコスト抽象化 — すべてのディスパッチはコンパイル時に解決。
2. 型安全 — アクセス違反はランタイムバグではなくコンパイルエラー。
3. トランスポート非依存 — 同一のレジスタマップ、任意のバス。
4. ポリシーベース — エラーハンドリング、範囲チェック、エンディアンが設定可能。
5. 組み込みファースト — ヒープなし、例外なし、RTTI なし。

---

## 9. write() / modify() セマンティクスガイド

### 9.1 セマンティクスの違い

| 操作 | ベース値 | セマンティクス | 安全性 |
|-----------|-----------|-----------|:------:|
| `write(v1, v2, ...)` | `reset_value()` | リセット状態からのレジスタ初期化 | ✅ |
| `modify(v1, v2, ...)` | 現在の値 (RMW) | 特定フィールドの変更、他を保持 | ✅ |
| `write(single_v)` | `reset_value()` | レジスタ初期化 — 他のフィールドはリセット | ⚠️ |

### 9.2 使用ルール

1. **初期化**: 関連するすべてのフィールドを指定して `write()` を使用。
2. **ランタイム変更**: 特定フィールドの変更には `modify()` を使用。
3. **単一フィールド書き込み**: `write(v)` は他のフィールドを `reset_value()` にリセットする。
   これは単一フィールドレジスタまたは完全リセット用。
   ランタイムでの単一フィールド変更には `modify()` を使用。

### 9.3 W1C フィールド

W1C フィールドは `clear()` を使用する：

```cpp
hw.clear(MyDevice::SR::OVR{});             // ✅ 正しい: OVR をクリア（RMWで非W1Cフィールドを保持）
hw.modify(MyDevice::SR::OVR::Clear{});     // ✗ コンパイルエラー: W1C は ModifiableValue でない
hw.flip(MyDevice::SR::OVR{});              // ✗ コンパイルエラー: W1C は NotW1C でない
```

`modify()` および `flip()` 中、親レジスタの W1C ビットは `Register::w1c_mask` により
ライトバック前に自動的に 0 にマスクされる。これにより、他のフィールドの
read-modify-write 操作中に W1C ステータスビットが意図せずクリアされることを防ぐ。

`clear()` は混合レジスタ（W1C と非W1C フィールドを両方含む）では
read-modify-write により非W1C フィールド値を保持する。
全ビットが W1C のレジスタでは、効率のため直接書き込みを使用する。

### 9.4 アトミック性

`modify()` は read-modify-write を実行し、**決してアトミックではない**。
ISR 安全なアクセスでは、呼び出し側がアクセスを外部で直列化する必要がある：

```cpp
// 例: トランスポート操作をプラットフォーム固有のクリティカルセクションで囲む。
// 具体的な方法（割り込み禁止、mutex 等）は呼び出し側が決定する。
{
    auto lock = enter_critical_section();  // プラットフォーム固有
    io.modify(ConfigEnable::Set{});        // ISR 安全な RMW
}   // lock 解放 (RAII)
```

umimmio はトランスポートレベルのライブラリであり、同期プリミティブは提供しない。
適切なロック機構の選択は呼び出し側の責務である。

### 9.5 reset()

`reset(Reg{})` はレジスタの `reset_value()` を直接書き込む。これは純粋な書き込み
（read-modify-write ではない）であり、ハードウェアを初期状態に戻すのに適している。
