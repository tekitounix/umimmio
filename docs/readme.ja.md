# umimmio

[English](../README.md) | 日本語

C++23 向けの型安全、ゼロコストメモリマップド I/O ライブラリ。
レジスタマップをコンパイル時に定義し、同一の API で Direct MMIO、I2C、SPI トランスポート経由でアクセスできる。

## 特徴

- **デフォルトで安全** — フィールドは名前付き `Value<>` 型のみ受け付け、raw 数値アクセスは `Numeric` でオプトイン
- **ゼロコスト** — 全ディスパッチはコンパイル時に解決、vtable なし、ヒープなし
- **複数トランスポート** — 同一レジスタマップを `DirectTransport`、`I2cTransport`、`SpiTransport` で共有
- **ポリシーベースエラーハンドリング** — `AssertOnError`、`TrapOnError`、`IgnoreError`、`CustomErrorHandler`
- **C++23** — deducing this (CRTP 不要)、`if consteval`、`std::byteswap`

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

## クイックスタート

```cpp
#include <umimmio/mmio.hh>
using namespace umi::mmio;

struct MyDevice : Device<> {
    static constexpr Addr base_address = 0x4002'0000;
    struct CTRL : Register<MyDevice, 0x00, 32> {
        struct EN : Field<CTRL, 0, 1> {};
    };
};

DirectTransport<> io;
io.modify(MyDevice::CTRL::EN::Set{});       // 型安全なフィールド設定
bool on = io.is(MyDevice::CTRL::EN::Set{}); // 型安全な読み出し
```

## インストール

外部プロジェクト:

```lua
add_repositories("synthernet https://github.com/tekitounix/synthernet-xmake-repo.git main")
add_requires("umimmio")
add_packages("umimmio")
```

## ビルドとテスト

```bash
xmake test 'test_umimmio/*'
```

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
| `is(Value{})` | フィールド/レジスタを名前付き値と比較（単一バス読み出し） |
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

### Register::value() — 正しい場合と間違いの場合

`Register::value()` はデータレジスタとビットマップレジスタでは**正規の API** である:

- **データレジスタ** (SPI DR, USART DR, ADC DR) — レジスタ全体がフィールド構造を持たない単一数値。
- **ビットマップレジスタ** (NVIC ISER, GPIO BSRR, EXTI IMR/PR) — 各ビットが独立した制御対象（IRQ、ピン、割り込みライン）に対応する。ビット単位の Named Value 定義はコスト不釣り合い。

```cpp
// データレジスタ — Register::value() が通常の API
io.write(SPI::DR::value(tx_byte));

// ビットマップレジスタ — Register::value() が実用的
io.write(NVIC::ISER<irq_num / 32>::value(1U << (irq_num % 32)));

// レジスタインデックスのコンパイル時ディスパッチ
dispatch<8>(reg_idx, [&]<std::size_t I>() {
    io.write(NVIC::ISER<I>::value(1U << bit_pos));
});
```

**フィールドを持つレジスタ** (PLLCFGR, MODER, CR1) では `Register::value()` はフィールドレベルの型安全をバイパスするため避けるべき:

| アンチパターン | 型安全な代替 |
|---------------|------------|
| `PLLCFGR::value((m<<0)\|(n<<6))` | 個別フィールドの `write()` / `modify()` |
| `MODER::value(手動RMW)` | `modify()` でピンごとのフィールドを指定 |

> **参考**: Rust の svd2rust は同等の `w.bits(n)` を `unsafe` として扱う。
> umimmio の `Register::value()` は C++ の意味では `unsafe` ではない。
> データレジスタとビットマップレジスタでは正規 API である。
> フィールドを持つレジスタでは、フィールドレベルの型安全をバイパスするため、
> svd2rust の `unsafe` `.bits()` と同等の注意が必要。

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

## W1S / W1T (Write-1-to-Set / Write-1-to-Toggle) フィールド

W1S と W1T フィールドは read-modify-write なしでアトミックなビット操作を提供する。
書き込み専用 (`WO`) として定義される — ハードウェアが独立した set/toggle ポートを提供する。

```cpp
// RP2040 SIO — アトミック GPIO 操作用の個別 WO レジスタ
struct SIO : Device<> {
    static constexpr Addr base_address = 0xD000'0000;
    struct GPIO_OUT     : Register<SIO, 0x010, 32, RW>  {};
    struct GPIO_OUT_SET : Register<SIO, 0x014, 32, W1S> {};
    struct GPIO_OUT_CLR : Register<SIO, 0x018, 32, W1C> {};
    struct GPIO_OUT_XOR : Register<SIO, 0x01C, 32, W1T> {};
};

io.write(SIO::GPIO_OUT_SET::value(pin_mask));  // アトミックセット
io.write(SIO::GPIO_OUT_XOR::value(pin_mask));  // アトミックトグル
```

1 ビット W1S フィールドは `Set`/`Reset` エイリアスを得る（通常の RW と同じ — 「1 を書いてセット」が一致）。
1 ビット W1T フィールドは代わりに `Toggle` エイリアスを得る。

`modify()` と `flip()` は W1S/W1T フィールドではコンパイルエラー — 特殊な書き込みセマンティクスに対して read-modify-write は安全でないか意味がない。

## フィールド型サマリー

| フィールド種別 | `value()` | `bits()` | 名前付き `Value<>` | 自動エイリアス | `modify()` | `clear()` |
|-----------|:---------:|:--------:|:---------------:|:------------:|:----------:|:---------:|
| デフォルト（安全） | ブロック | ブロック | 必須 | — | Yes | — |
| `Numeric` | Yes（符号なし） | Yes | 任意 | — | Yes | — |
| 1 ビット RW | — | ブロック | 任意（カスタム別名可） | `Set` / `Reset` | Yes | — |
| 1 ビット W1C | — | ブロック | — | `Clear` | コンパイルエラー | Yes |
| 1 ビット W1S | — | ブロック | — | `Set` / `Reset` | コンパイルエラー | — |
| 1 ビット W1T | — | ブロック | — | `Toggle` | コンパイルエラー | — |

## レジスタ配列とランタイムディスパッチ

umimmio はインデックス付きレジスタバンク（NVIC ISER[0..7]、DMA ストリーム等）を扱うための 3 つのプリミティブを提供する:

### RegisterArray — コンパイル時配列メタデータ

```cpp
// テンプレートレジスタバンク: ISER<0>, ISER<1>, ..., ISER<7>
template <std::size_t N>
struct ISER : Register<NVIC, 0x100 + (N * 4), 32> {
    static_assert(N < 8);
};

using ISERArray = RegisterArray<ISER, 8>;
static_assert(ISERArray::size == 8);
// ISERArray::Element<3> は ISER<3>
```

### dispatch / dispatch_r — ランタイム→コンパイル時ブリッジ

fold expression によりランタイムインデックスをコンパイル時テンプレートパラメータに変換する:

```cpp
// void dispatch — 戻り値なし
std::size_t stream = get_active_stream();  // ランタイム
dispatch<8>(stream, [&]<std::size_t I>() {
    io.modify(DMA::Stream<I>::CR::EN::Set{});
});

// dispatch_r — 戻り値あり
auto count = dispatch_r<8, std::uint32_t>(stream, [&]<std::size_t I>() {
    return io.read(DMA::Stream<I>::NDTR{}).bits();
});
```

範囲外インデックスは `ErrorPolicy::on_range_error()` を呼び出す（デフォルト: assert）。

### IndexedArray — サブレジスタ粒度

各エントリがレジスタより小さいレジスタ配列（例: 32 ビットレジスタ内の 8 ビットエントリ）:

```cpp
// デバイスベースから offset 0x100 に 32 バイト、8 ビットエントリ、stride 1
using LUT = IndexedArray<MyDevice, 0x100, 32>;

// コンパイル時アクセス — Entry<N>
io.write(LUT::Entry<5>::value(0x42));

// ランタイムアクセス — write_entry/read_entry (Direct トランスポート専用)
LUT::write_entry(idx, 0x42);
auto val = LUT::read_entry(idx);

// カスタムエントリ幅とストライド
using WideArray = IndexedArray<MyDevice, 0x200, 16, bits16, 4>;  // 16 ビットエントリ、4 バイトストライド
```

`write_entry`/`read_entry` はデバイスの `AllowedTransports` に `Direct` が含まれることを要求する（`static_assert` でコンパイル時に強制）。

## トランスポート選択

トランスポートは適切な型を構築して選択する:

```cpp
umi::mmio::DirectTransport<> direct;                    // Volatile ポインタ (Cortex-M 等)
umi::mmio::I2cTransport<MyI2c> i2c(hal_i2c, 0x68);     // HAL I2C
umi::mmio::SpiTransport<MySpi> spi(hal_spi);            // HAL SPI
```

全トランスポートが同一の `write()`, `read()`, `modify()`, `is()`, `flip()`, `clear()`, `reset()` API を提供する。
Device の `AllowedTransports...` パラメータにより、許可されないトランスポートの使用はコンパイルエラーになる。

### CsrTransport — RISC-V CSR アクセス

CSR (Control and Status Register) アクセスは専用トランスポートにより同一の型安全 API で提供される:

```cpp
#include <umimmio/transport/csr.hh>

// デバイス定義 — CSR 番号をレジスタオフセットとして使用 (base_address = 0)
struct RiscvMachine : Device<RW, Csr> {
    static constexpr Addr base_address = 0;
    struct MSTATUS : Register<RiscvMachine, 0x300, 32> {
        struct MIE : Field<MSTATUS, 3, 1> {};
        struct MPP : Field<MSTATUS, 11, 2> {
            using MACHINE = Value<MPP, 3>;
        };
    };
};

// RISC-V ターゲットでは DefaultCsrAccessor (インライン asm csrr/csrw) を使用
// テストでは CsrAccessor concept を満たす MockCsrAccessor を注入
CsrTransport<MockCsrAccessor> csr;
csr.modify(RiscvMachine::MSTATUS::MIE::Set{});
```

`CsrAccessor` concept は `csr_read<CsrNum>()` と `csr_write<CsrNum>(value)` を要求する — CSR 番号はコンパイル時テンプレートパラメータ（12 ビット即値制約）。

### AtomicDirectTransport — 書き込み専用エイリアスレジスタ

アトミックレジスタエイリアスを持つ MCU（例：RP2040 SET/CLR/XOR）向けに、`AtomicDirectTransport` はすべての書き込みに固定オフセットを加算する：

```cpp
#include <umimmio/transport/atomic_direct.hh>

// RP2040: SET エイリアス = +0x2000、CLR エイリアス = +0x3000、XOR エイリアス = +0x1000
AtomicDirectTransport<0x2000> gpio_set;   // write() は reg + 0x2000 を対象
AtomicDirectTransport<0x3000> gpio_clr;   // write() は reg + 0x3000 を対象

gpio_set.write(GPIO::OUT::Pin5::Set{});   // エイリアス経由のアトミックビットセット
gpio_clr.write(GPIO::OUT::Pin5::Set{});   // エイリアス経由のアトミックビットクリア
```

これは**書き込み専用**トランスポートである — `read()`、`modify()`、`flip()`、`is()` は `reg_read()` が提供されないためコンパイルエラーとなる。`write()` と `reset()` は通常通り動作する。

## エラーハンドリング

### コンパイル時エラー

アクセスポリシー違反、値の範囲超過、型の不一致は全てコンパイルエラーになる。
ランタイムに到達するバグの種類を最小化する設計。

### ランタイムエラーポリシー

`ErrorPolicy` テンプレートパラメータで動作を選択:

| ポリシー | 動作 |
|--------|----------|
| `AssertOnError` | `assert(false && msg)` (デフォルト) |
| `TrapOnError` | `std::abort()` |
| `IgnoreError` | サイレント no-op |
| `CustomErrorHandler<fn>` | ユーザーコールバック |

2 つのエントリポイント:

| エントリポイント | トリガー |
|---|---|
| `on_range_error(msg)` | フィールド幅を超える値（プログラミングエラー） |
| `on_transport_error(msg)` | HAL が失敗を報告（バスエラー、NACK、タイムアウト） |

I2cTransport / SpiTransport は `if constexpr` で HAL の戻り値型を判定し、偽値が返された場合に `on_transport_error()` を呼び出す。

## 並行性

`modify()` は read-modify-write であり、**決してアトミックではない**。
ISR 安全なアクセスでは、呼び出し側がアクセスを外部で直列化する必要がある:

```cpp
{
    auto lock = enter_critical_section();  // プラットフォーム固有
    io.modify(ConfigEnable::Set{});        // ISR 安全な RMW
}   // lock 解放 (RAII)
```

umimmio はトランスポートレベルのライブラリであり、同期プリミティブは提供しない。
適切なロック機構の選択は呼び出し側の責務である。

## サンプル

- [`examples/minimal.cc`](../examples/minimal.cc) — レジスタマップ定義と static_assert による検証
- [`examples/register_map.cc`](../examples/register_map.cc) — SPI ペリフェラルレジスタマップ (STM32スタイル)
- [`examples/transport_mock.cc`](../examples/transport_mock.cc) — MockTransport による全公開 API 操作

## ライセンス

MIT — [LICENSE](../LICENSE) を参照
