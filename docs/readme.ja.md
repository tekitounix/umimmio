# umimmio

[English](../../README.md)

C++23 向けの型安全、ゼロコストメモリマップド I/O ライブラリ。
レジスタマップをコンパイル時に定義し、同一の API で Direct MMIO、I2C、SPI トランスポート経由でアクセスできる。

## 課題

従来の C/C++ ベンダーヘッダー（CMSIS、ESP-IDF、Pico SDK 等）はレジスタを `uint32_t` ポインタとビットマスクマクロで公開している。以下のようなバグがコンパイルを通過する:

```c
USART1->SR |= USART_CR1_UE;        // 間違ったレジスタ — コンパイル通過
GPIOA->ODR |= USART_CR1_UE;        // 間違ったペリフェラル — コンパイル通過
USART1->SR = 0;                     // RO ビットへの書き込み — コンパイル通過
```

umimmio はこれらのバグをコンパイル時の型検査で排除する:

| 安全チェック | ベンダー CMSIS | umimmio |
|-------------|:----------:|:-------:|
| クロスレジスタのビットマスク誤用 | ❌ | ✅ コンパイルエラー |
| クロスペリフェラルのビットマスク誤用 | ❌ | ✅ コンパイルエラー |
| 読み出し専用レジスタへの書き込み | ❌ | ✅ コンパイルエラー |
| 書き込み専用レジスタからの読み出し | ❌ | ✅ コンパイルエラー |
| フィールド幅のレンジチェック | ❌ | ✅ `if consteval` + ランタイムポリシー |
| 名前付き値の型安全性 | ❌ (マクロ) | ✅ NTTP `Value<F, V>` |
| W1C (Write-1-to-Clear) 安全性 | ❌ | ✅ `modify()` でコンパイルエラー |

## 型階層

umimmio はハードウェアを4階層のネスト構造体で表現する。
ネストにより、値は特定のフィールドに、フィールドは特定のレジスタに所属する。クロスレジスタの誤用はコンパイルエラーになる。

```
Device > Register > Field > Value
```

```cpp
#include <umimmio/mmio.hh>
using namespace umi::mmio;

struct MyDevice : Device<RW> {
    static constexpr Addr base_address = 0x4000'0000;

    struct CTRL : Register<MyDevice, 0x00, 32> {
        // 1 ビットフィールド — Set/Reset 自動生成
        struct EN : Field<CTRL, 0, 1> {};

        // 1 ビットフィールド（ドメイン固有の別名）— Set/Reset も利用可
        struct DIR : Field<CTRL, 3, 1> {
            using Transmit = Value<DIR, 1>;
            using Receive  = Value<DIR, 0>;
        };

        // 多ビットフィールド（名前付き値）— デフォルトで安全
        struct MODE : Field<CTRL, 1, 2> {
            using Output  = Value<MODE, 0b01>;
            using AltFunc = Value<MODE, 0b10>;
        };

        // Numeric フィールド — raw value()/bits() のオプトイン。数値そのものが
        // データの場合のみ使用（カウンタ、分周器、アドレス等）。
        // 大半のフィールドは名前付き Value<> を使うべき。
        struct DIV : Field<CTRL, 6, 9, Numeric> {};
    };

    // 書き込み専用レジスタ（アンロックキーは名前付き Value で定義）
    struct KEYR : Register<MyDevice, 0x04, 32, WO> {
        using Key1 = Value<KEYR, 0x4567'0123U>;
        using Key2 = Value<KEYR, 0xCDEF'89ABU>;
    };
};
```

### テンプレートパラメータ

**Device**`<Access = RW, AllowedTransports... = Direct>` — トップレベルのペリフェラル。`static constexpr Addr base_address` が必要。

**Register**`<Parent, Offset, Bits, Access = RW, Reset = 0, W1cMask = 0>` — 親からのバイトオフセットに位置するビット領域。

**Field**`<Reg, BitOffset, BitWidth, Traits...>` — レジスタ内のビット範囲。Traits: アクセスポリシー (`RO`, `WO`, `W1C`) および/または `Numeric`、順序不問。`Numeric` は raw `value()`/`bits()` を有効にする — カウンタ、分周器、アドレス等のデータ値フィールドにのみ適切。モードセレクタ、設定ビット等の有限な識別子には名前付き `Value<>` を使うこと。

**Value**`<RegionT, EnumValue>` — コンパイル時名前付き定数。Field または Register 内に `using` エイリアスとして定義する。

## 操作

全操作はトランスポートインスタンス上で呼び出す。

### write() vs modify() — 決定的な違い

```cpp
DirectTransport<> io;
using CTRL = MyDevice::CTRL;

// write() — reset_value をベースに、指定フィールドを適用してレジスタに書き込む。
//           未指定フィールドは reset_value に戻る。初期化用。
io.write(CTRL::EN::Set{}, CTRL::MODE::Output{});

// modify() — 現在値を読み出し、変更を適用して書き戻す。
//            未指定フィールドは現在値を保持する。ランタイム変更用。
io.modify(CTRL::EN::Set{});           // EN のみ変更、MODE と DIV は保持
io.modify(CTRL::DIV::value(336));     // Numeric フィールド
```

**よくある間違い**: ランタイムで `io.write(EN::Set{})` を使うと、MODE や DIV が reset_value に戻る。`modify()` を使うこと。

### 読み出し

```cpp
auto val = io.read(CTRL::EN{});             // → RegionValue<EN>
bool is_out = io.is(CTRL::MODE::Output{});  // 直接比較

// RegionValue — 1 回のバスアクセスで複数フィールドを取得
auto cfg = io.read(CTRL{});                 // → RegionValue<CTRL>
auto en  = cfg.get(CTRL::EN{});             // 追加バスアクセスなし
bool fast = cfg.is(CTRL::MODE::AltFunc{});
auto raw  = cfg.bits();                     // レジスタレベル: 常に利用可
```

`RegionValue` は `Value` および `DynamicValue` との `==` のみサポート — 整数との直接比較はコンパイルエラー。`bits()` はフィールドでは `Numeric` が必要（書き込み側の `value()` と対称）、レジスタでは常に利用可。

### その他の操作

| 操作 | 説明 |
|------|------|
| `flip(Field{})` | 1 ビット RW フィールドのトグル (read-modify-write) |
| `clear(Field{})` | W1C フィールドのクリア（該当ビットに 1 を書き込み） |
| `reset(Reg{})` | レジスタにコンパイル時リセット値を書き込み |
| `read_variant<F, V1, V2, ...>(F{})` | `std::variant` + `std::visit` によるパターンマッチ |

## よくある間違い

| 間違い | 何が起こるか | 正しい API |
|--------|------------|-----------|
| ランタイムで `io.write(EN::Set{})` | 未指定フィールドが reset_value に戻る — 他の設定を暗黙に破壊 | `io.modify(EN::Set{})` — 他フィールドを保持 |
| モード/セレクタに `Field<R, 0, 4, Numeric>` | `value(N)` / `bits()` が有効に — 名前付き値の型安全をバイパス | `Numeric` を省略し `Value<F, N>` エイリアスを定義 |
| `io.modify(W1C_Flag::Clear{})` | コンパイルエラー — `modify()` で W1C フィールドはクリアできない | `io.clear(W1C_Flag{})` |
| `io.read(ModeField{}).bits()` | コンパイルエラー — 非 Numeric フィールドでは `bits()` はブロック | `io.is(ModeField::Expected{})` または `io.read_variant(...)` |

**原則**: `write()` は初期化、`modify()` はランタイム変更。識別子には名前付き `Value<>`、数値そのものがデータの場合のみ `Numeric`。

## 設計意図: value() と Numeric

### 型安全性の階層

```
              書き込み                                 読み出し
              ──────                                 ──────
最も安全      名前付き Value<>                         is() / read_variant()
              io.modify(MODE::Output{})               io.is(MODE::Output{})

              1 ビット Set/Reset                      （同上）
              io.modify(EN::Set{})                    io.is(EN::Set{})

              Field::value() [Numeric]                .bits() [Numeric フィールド]
              io.modify(DIV::value(336))              io.read(DIV{}).bits()

              Register::value() [データレジスタ]       .bits() [データレジスタ]
              io.write(DR::value(tx_byte))            io.read(DR{}).bits()
```

`Register::value()` には2つの異なる役割がある:

- **データレジスタ** (SPI DR, USART DR) — レジスタ全体がフィールド構造を持たない単一の数値。`Register::value()` はここでは通常の API であり、エスケープハッチではない。
- **フィールドを持つレジスタ** (PLLCFGR, MODER, CR1) — `Register::value()` はフィールドレベルの型安全をバイパスする。全てのケースに型安全な代替がある（後述）。

### 名前付き Value — デフォルト

ほとんどのレジスタフィールドは、特定のハードウェア動作に対応する有限個の値を持つ。これらはマジック定数を含め、**必ず**名前付き `Value<>` 型にすべき:

```cpp
// アンロックキー — 仕様で定められた定数。任意の数値ではない
struct KEYR : Register<FLASH, 0x04, 32, WO> {
    using Key1 = Value<KEYR, 0x4567'0123U>;
    using Key2 = Value<KEYR, 0xCDEF'89ABU>;
};
io.write(FLASH::KEYR::Key1{});

// モードセレクタ — ハードウェア動作の有限集合
struct MODER0 : Field<MODER, 0, 2> {
    using Input     = Value<MODER0, 0>;
    using Output    = Value<MODER0, 1>;
    using Alternate = Value<MODER0, 2>;
    using Analog    = Value<MODER0, 3>;
};
io.modify(MODER0::Output{});  // 自己文書化、型安全

// GPIO 代替機能 — MCU 固有のペリフェラルマッピング
struct AFR0 : Field<AFRL, 0, 4> {
    using System = Value<AFR0, 0>;   // AF0
    using I2C    = Value<AFR0, 4>;   // AF4: I2C1..I2C3
    using USART  = Value<AFR0, 7>;   // AF7: USART1..USART3
};
io.modify(AFR0::USART{});  // 意図が明確
```

### Field::value() と Numeric — データ用、識別子ではない

`Numeric` は `Field::value()` を有効にする**明示的なオプトイン**。適切なのは以下の場合**のみ**:

- 数値**そのもの**がデータである — カウンタリロード値、ボーレート分周器、DMA アドレス、転送カウント
- 値域が広く、個々の値に固有のセマンティクスがない

```cpp
struct RELOAD : Field<LOAD, 0, 24, Numeric> {};   // 24 ビットカウンタ値
struct BRR    : Field<USART, 0, 16, Numeric> {};   // ボーレート分周器
struct MAR    : Field<DMA_S0, 0, 32, Numeric> {};  // メモリアドレス
struct NDT    : Field<DMA_S0, 0, 16, Numeric> {};  // 転送カウント
```

**Numeric は間違い**（値が有限の識別子の場合）:

```cpp
// ❌ AF 番号はペリフェラルマッピングであり、任意の数値ではない
struct AFR0 : Field<AFRL, 0, 4, Numeric> {};
io.modify(AFR0::value(7U));  // 7 とは何か？不透明で誤りやすい
```

### フィールドを持つレジスタでの Register::value() — アンチパターン

フィールドが定義されたレジスタに対して `Register::value()` を使うと、フィールドレベルの型安全を全てバイパスする。全てのケースに型安全な代替がある:

| アンチパターン | 型安全な代替 |
|---------------|------------|
| `KEYR::value(0x4567'0123U)` | `Value<KEYR, 0x4567'0123U>` — 名前付き定数 |
| `PLLCFGR::value((m<<0)\|(n<<6))` | 個別フィールドの `write()` / `modify()` |
| `ISER::value(1U << irq_num)` | 型付き IRQ enum + テンプレート or switch |
| `MODER::value(手動RMW)` | `modify()` でピンごとのフィールドを指定 |

ランタイム→コンパイル時ディスパッチには明示的なパターンマッチを使う:

```cpp
// ❌ 型安全をバイパス
io.write(NVIC::ISER<0>::value(1U << bit_pos));

// ✅ 型安全なディスパッチ
switch (bit_pos) {
case 0:  io.write(NVIC::ISER<0>::IRQ0::Set{});  break;
case 1:  io.write(NVIC::ISER<0>::IRQ1::Set{});  break;
// ...
}

// ✅ 最善 — コンパイル時インデックス
template <std::uint32_t IrqNum>
void enable_irq() {
    io.write(NVIC::ISER<IrqNum / 32>::IRQ<IrqNum % 32>::Set{});
}
```

> **参考**: Rust の svd2rust は同等の `w.bits(n)` を `unsafe` として扱う。
> umimmio の `Register::value()` は C++ の意味では `unsafe` ではないが、同等の注意を払うべき — 段階的移行のために存在するのであり、推奨 API ではない。

## W1C (Write-1-to-Clear) フィールド

W1C フィールドはステータス/割り込みレジスタで一般的。3ステップで定義する:

```cpp
struct SR : Register<MyDevice, 0x08, 32, RW, 0, /*W1cMask=*/0x03> {
    struct OVR : Field<SR, 0, 1, W1C> {};    // Clear 自動生成
    struct EOC : Field<SR, 1, 1, W1C> {};    // Clear 自動生成
    struct EN  : Field<SR, 8, 1> {};          // 通常の RW
};

io.clear(SR::OVR{});             // EOC や EN に影響せず OVR をクリア
io.modify(SR::EN::Set{});        // 安全: W1C ビットは自動的に 0 にマスク
// io.modify(SR::OVR::Clear{});  // コンパイルエラー — W1C には clear() を使う
```

`W1cMask` により、`modify()` と `flip()` 時に W1C ビット位置が自動的にゼロにされ、フラグの意図しないクリアを防止する。

## フィールド型サマリー

| フィールド種別 | `value()` | `bits()` | 名前付き `Value<>` | 自動エイリアス | `modify()` | `clear()` |
|-----------|:---------:|:--------:|:---------------:|:------------:|:----------:|:---------:|
| デフォルト（安全） | ブロック | ブロック | 必須 | — | Yes | — |
| `Numeric` | Yes（符号なし） | Yes | 任意 | — | Yes | — |
| 1 ビット RW | — | ブロック | 任意（カスタム別名可） | `Set` / `Reset` | Yes | — |
| 1 ビット W1C | — | ブロック | — | `Clear` | コンパイルエラー | Yes |

## 特徴

- **デフォルトで安全** — フィールドは名前付き `Value<>` 型のみ受け付け、raw 数値アクセスは `Numeric` でオプトイン
- **ゼロコスト** — 全ディスパッチはコンパイル時に解決、vtable なし、ヒープなし
- **複数トランスポート** — 同一レジスタマップを `DirectTransport`、`I2cTransport`、`SpiTransport` で共有
- **ポリシーベースエラーハンドリング** — `AssertOnError`、`TrapOnError`、`IgnoreError`、`CustomErrorHandler`
- **C++23** — deducing this (CRTP 不要)、`if consteval`、`std::byteswap`

## ビルドとテスト

```bash
xmake test 'test_umimmio/*'
```

## ドキュメント

- [設計 & API](design.md)
- [テスト](../tests/testing.md)

## ライセンス

MIT — [LICENSE](../../LICENSE) を参照
