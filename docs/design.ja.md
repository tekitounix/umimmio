# umimmio 設計

[English](design.md) | 日本語

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
W1S (Write-1-to-Set) と W1T (Write-1-to-Toggle) フィールドはアトミックビット操作を提供する WO ポートとして定義される。
`IsW1C`/`IsW1S`/`IsW1T` concept がこれらを識別し、`NormalWrite` concept が `modify()` と `flip()` を NORMAL 以外の全 WriteBehavior から除外する。

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
2. ランタイム `DynamicValue` は `value()` 生成時には検証されない。検証は操作側（`write()`/`modify()`/`is()`）で `CheckPolicy` + `ErrorPolicy` に基づいて行われる。
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
│   ├── design.md / design.ja.md
│   └── readme.ja.md
├── examples/
│   ├── minimal.cc
│   ├── register_map.cc
│   └── transport_mock.cc
├── include/umimmio/
│   ├── mmio.hh              # アンブレラヘッダー（csr.hh は含まない）
│   ├── policy.hh            # 基盤: AccessPolicy、トランスポートタグ、エラーポリシー
│   ├── region.hh            # データモデル: Device, Register, Field, Value, concepts,
│   │                        #   RegisterArray, dispatch, IndexedArray
│   ├── ops.hh               # 操作: RegOps, ByteAdapter
│   └── transport/
│       ├── atomic_direct.hh # AtomicDirectTransport (書き込み専用エイリアス、明示的 include)
│       ├── csr.hh           # CsrTransport (RISC-V CSR、明示的 include)
│       ├── detail.hh        # アドレスエンコード共通ヘルパー
│       ├── direct.hh        # DirectTransport (volatile ポインタ)
│       ├── i2c.hh           # I2cTransport (HAL ベース)
│       └── spi.hh           # SpiTransport (HAL ベース)
└── tests/
    ├── testing.md / testing.ja.md   # テストドキュメント
    ├── test_main.cc
    ├── test_mock.hh             # MockTransport と共有デバイス定義
    ├── test_access_policy.hh    # W1C/W1S/W1T アクセスポリシーテスト
    ├── test_register_field.hh   # Field, RegisterArray, dispatch, IndexedArray テスト
    ├── test_transport.hh        # Transport テスト (Direct, I2C, CSR)
    ├── test_byte_transport.hh   # ByteAdapter テスト
    ├── smoke/
    │   └── standalone.cc
    ├── compile_fail/            # 31 個のネガティブコンパイルテスト（glob 収集）
    │   ├── bits_non_numeric.cc
    │   ├── clear_non_w1c.cc
    │   ├── cross_register_write.cc
    │   ├── field_overflow.cc
    │   ├── flip_atomic_direct.cc  # AtomicDirectTransport の flip 拒否 (reg_read なし)
    │   ├── flip_multi_bit.cc
    │   ├── flip_ro.cc
    │   ├── flip_w1c.cc
    │   ├── flip_w1s.cc          # W1S は NormalWrite で拒否
    │   ├── flip_w1t.cc          # W1T は NormalWrite で拒否
    │   ├── flip_wo.cc
    │   ├── get_wrong_field.cc
    │   ├── indexed_array_oob.cc # IndexedArray::Entry 範囲外
    │   ├── modify_atomic_direct.cc # AtomicDirectTransport の modify 拒否 (reg_read なし)
    │   ├── modify_cross_register.cc
    │   ├── modify_w1c.cc
    │   ├── modify_w1s.cc        # W1S は NormalWrite で拒否
    │   ├── modify_w1t.cc        # W1T は NormalWrite で拒否
    │   ├── modify_wo.cc
    │   ├── read_atomic_direct.cc  # AtomicDirectTransport の read 拒否 (reg_read なし)
    │   ├── read_field_eq_int.cc
    │   ├── read_w1s.cc          # W1S は Readable でない
    │   ├── read_w1t.cc          # W1T は Readable でない
    │   ├── read_wo.cc
    │   ├── transport_tag_mismatch.cc
    │   ├── value_signed.cc
    │   ├── value_typesafe.cc
    │   ├── write_ro.cc
    │   ├── write_ro_csr.cc      # RO CSR への書き込みを CsrTransport で拒否
    │   ├── write_ro_value.cc
    │   └── write_zero_args.cc
    └── xmake.lua
```

---

## 4. 使い方

型階層、操作、トランスポート選択、エラーハンドリング、並行性については
[README](../README.md)（[日本語](readme.ja.md)）を参照。

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
| `IsW1S<T>` | Field | `write_behavior == ONE_TO_SET` |
| `IsW1T<T>` | Field | `write_behavior == ONE_TO_TOGGLE` |
| `NormalWrite<T>` | Register/Field | `write_behavior == NORMAL`（RMW 安全） |
| `ReadableValue<V>` | Value/DynamicValue | 親領域が読み出し可能 |
| `WritableValue<V>` | Value/DynamicValue | 親領域が書き込み可能 |
| `ModifiableValue<V>` | Value/DynamicValue | 書き込み可能かつ NormalWrite |

### 5.3 ByteAdapter (deducing this)

RegOps の型付きレジスタ操作を `raw_read()` / `raw_write()` バイト操作に変換。
C++23 deducing this を使用 — CRTP パラメータなし。
`std::byteswap`（`<bit>`）を使用してホスト CPU とワイヤフォーマット間のエンディアン変換を処理。
エンディアンは `std::endian` で表現（カスタム `Endian` enum なし）。

### 5.4 Value と DynamicValue

- `Value<RegionT, EnumValue>`: シフト済み表現を持つコンパイル時定数。
  親の Register または Field 型を `RegionType` エイリアスで保持。
- `DynamicValue<RegionT, T>`: ランタイム値。`value()` 生成時には範囲チェックを行わず、操作時（`write()`/`modify()`/`is()`）に `CheckPolicy` + `ErrorPolicy` で検証される。

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

### 5.7 レジスタ配列とランタイムディスパッチ

#### RegisterArray

`RegisterArray<Template, N>` はテンプレートレジスタバンク（例: NVIC ISER[0..7]）のコンパイル時メタデータを保持する：

- `size` — 要素数
- `Element<I>` — I 番目のレジスタ型へのアクセス

データメンバなし、ランタイムコストなし — 純粋な型レベルメタデータ。

#### dispatch / dispatch_r

ランタイムインデックスをコンパイル時テンプレートパラメータに変換する。`std::index_sequence` に対する fold expression を使用：

```cpp
dispatch<N>(idx, [&]<std::size_t I>() { ... });           // void
auto val = dispatch_r<N, R>(idx, [&]<std::size_t I>() { return ...; });  // 戻り値あり
```

範囲外インデックスは `ErrorPolicy::on_range_error()` を呼び出す。

#### IndexedArray

`IndexedArray<Parent, BaseOffset, Count, EntryWidth, Stride>` はサブレジスタ粒度の配列（例: ルックアップテーブル、FIFO 配列）をモデル化する：

- `Entry<N>` — `Register` 型としてのコンパイル時アクセス。`static_assert` で N ≥ Count を拒否。
- `write_entry(index, value)` / `read_entry(index)` — volatile ポインタによるランタイムアクセス。`AllowedTransportsType` に `Direct` が必要（`static_assert`）。範囲外インデックスは `ErrorPolicy::on_range_error()` を呼び出す。
- `EntryWidth` — エントリあたりのビット幅（デフォルト: `bits8`）。`UintFit<EntryWidth>` で `EntryType` を決定（例: `bits8` → `uint8_t`、`bits16` → `uint16_t`）。
- `Stride` — 連続エントリ間のバイト間隔（デフォルト: `EntryWidth / 8`、すなわちギャップなしの密パッキング）。アライメントパディングのあるハードウェアではオーバーライド（例: 16 ビットエントリを 4 バイト境界に配置: `Stride = 4`）。エントリ N のアドレス = `base_address + BaseOffset + N * Stride`。

### 5.8 CsrTransport

`CsrTransport<Accessor>` は umimmio のトランスポートモデルを RISC-V CSR レジスタに拡張する。

**主要な設計判断**:

- CSR 番号は `Register::address` にマッピング（Device の `base_address = 0`）。
- `CsrAccessor` concept がカスタマイズポイント — `csr_read<CsrNum>()` / `csr_write<CsrNum>(value)` で `CsrNum` はコンパイル時定数（RISC-V の 12 ビット即値エンコーディングの要件）。
- `DefaultCsrAccessor`（RISC-V のみ、`#if __riscv`）は Phase 1 CSR（mstatus, misa, mie, mtvec, mscratch, mepc, mcause, mtval, mip）のインライン asm を提供。
- ホストテストでは `CsrAccessor` を満たす任意の型（例: RAM バック付きモック）を注入可能。
- アンブレラヘッダー（`mmio.hh`）には含めない — ユーザーが明示的に `<umimmio/transport/csr.hh>` を include する。

### 5.9 AtomicDirectTransport

`AtomicDirectTransport<AliasOffset>` は、すべてのレジスタ書き込みに固定バイトオフセットを加算する書き込み専用トランスポートである。

**主要な設計判断**：

- 書き込みは `(Reg::address + AliasOffset)` を volatile ポインタ経由で実行。`reg_read()` なし — `read()`、`modify()`、`flip()`、`is()` は `Readable` concept 非充足によりコンパイルエラー。
- `write()` と `reset()` は通常通り動作（どちらも書き込みのみの操作）。
- 主要ユースケース：RP2040 のアトミックレジスタエイリアス（SET: +0x2000、CLR: +0x3000、XOR: +0x1000）。
- 汎用的 — 書き込みエイリアスレジスタを持つ任意の MCU で使用可能。
- アンブレラヘッダー（`mmio.hh`）には含めない — ユーザーが明示的に `<umimmio/transport/atomic_direct.hh>` を include する。
- 3 つの compile_fail テストが `read()`、`modify()`、`flip()` の拒否を検証。

### 5.10 read_variant()

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
8. `modify()`/`flip()` での W1S/W1T フィールド → `NormalWrite` concept が非 NORMAL WriteBehavior を拒否。
9. `RegionValue == 整数` → `operator==` なし（raw アクセスは `.bits()` を使用）。

ランタイムエラーポリシーの使い方については [README](../README.md)（[日本語](readme.ja.md)）を参照。

---

## 7. テスト戦略

[testing.md](../tests/testing.md) を参照。

---

## 8. 設計原則

1. ゼロコスト抽象化 — すべてのディスパッチはコンパイル時に解決。
2. 型安全 — アクセス違反はランタイムバグではなくコンパイルエラー。
3. トランスポート非依存 — 同一のレジスタマップ、任意のバス。
4. ポリシーベース — エラーハンドリング、範囲チェック、エンディアンが設定可能。
5. 組み込みファースト — ヒープなし、例外なし、RTTI なし。

---

write()/modify() のセマンティクス、W1C フィールド、並行性については
[README](../README.md)（[日本語](readme.ja.md)）を参照。
