# PAL (Peripheral Access Layer) アーキテクチャ提案

> **命名変更**: RAL → PAL。元ファイル: `../06_RAL_ARCHITECTURE.md`

**ステータス:** 設計中
**関連文書:**
- [04_ANALYSIS.md](04_ANALYSIS.md) — 既存 PAL アプローチの横断分析
- [02_CATEGORY_INDEX.md](02_CATEGORY_INDEX.md) — PAL カテゴリ一覧
- umimmio/docs/DESIGN.md — 現行 umimmio 設計

---

## 1. 本ドキュメントの目的

umimmio（MMIO フレームワーク）の上に MCU 固有のレジスタ定義を**どう構成・生成・配置するか**を提案する。

---

## 2. 前提の整理

### 2.1 umimmio と PAL の責務分離

```
umimmio (MMIO フレームワーク)          PAL (MCU 固有レジスタ定義)
─────────────────────────          ──────────────────────────
Device, Block, Register, Field      「GPIOA は 0x40020000」
RegOps (read/write/modify)          「MODER は offset 0x00, 32bit」
ByteAdapter (I2C/SPI ブリッジ)       「MODER13 は bit 26-27」
Transport concepts                  「0b01 = 出力モード」
Access policies (RW/RO/WO)          「AHB1ENR は RW, offset 0x30」
Error policies                      リセット値、列挙値

汎用・MCU 非依存                     MCU 固有・型番依存
```

PAL は umimmio の型テンプレートに **具体的な MCU データを流し込んでインスタンス化する** 層である。

### 2.2 umimmio 型シグネチャ（現行実装）

PAL のコード例を読む前に、umimmio が提供する型テンプレートの正確なシグネチャを確認する:

```cpp
// Device — デバイスルート (Access デフォルト RW、Transport 未指定時は DirectTransportTag)
template <class Access = RW, typename... AllowedTransports>
struct Device;

// Block — デバイス内サブリージョン (Access デフォルト Inherit)
template <class Parent, Addr BaseAddr, class Access = Inherit>
struct Block;

// Register — 5 パラメータ (Access デフォルト RW、Reset デフォルト 0)
template <class Parent, Addr Offset, std::size_t Bits, class Access = RW, std::uint64_t Reset = 0>
using Register = BitRegion<Parent, Offset, Bits, 0, Bits, Access, Reset, true>;

// Field — 4 パラメータ (Access デフォルト Inherit)
template <class Reg, std::size_t BitOffset, std::size_t BitWidth, class Access = Inherit>
struct Field : BitRegion<Reg, 0, Reg::reg_bits, BitOffset, BitWidth, Access, 0, false> {};

// 1-bit Field 特殊化 — Set/Reset が自動生成される
template <class Reg, std::size_t BitOffset, class Access>
struct Field<Reg, BitOffset, 1, Access> : BitRegion<...> {
    using Set = Value<Field, 1>;    // ビットセット値
    using Reset = Value<Field, 0>;  // ビットクリア値
};

// Value — フィールドの列挙値定数
template <class FieldT, auto EnumValue>
struct Value;

// DynamicValue — 実行時に決まる値 (Field::value(n) で生成)
template <class RegionT, typename T>
struct DynamicValue;
```

### 2.3 PAL が定義すべき情報

各ペリフェラルについて:

1. **ベースアドレス** — `0x40020000`
2. **レジスタオフセットとビット幅** — `MODER @ +0x00, 32bit`
3. **フィールドのビット位置と幅** — `MODER13: bit[27:26]`
4. **アクセスポリシー** — `RW`, `RO`, `WO`（デフォルト `RW` はレジスタレベル、`Inherit` はフィールドレベル）
5. **リセット値** — `0xA8000000`（省略時 0）
6. **列挙値（可能な範囲で）** — `Value<Field, 0b01>` または `enum class`
7. **Transport 制約** — 内蔵ペリフェラルは `DirectTransportTag` のみ

---

## 3. 設計判断

### 3.1 判断 1: 段階的アプローチ（手書き先行 → 生成移行）

**根拠:** 過去の umi_mmio プロジェクトで svd2ral v1/v2 が実装済みであり、SVD パーサー・バリデータ・コード生成器は動作実績がある。ただし旧 mmio.hh API を前提としているため、現行 umimmio API に適合させるリファクタリングが必要。

**方針:**

| フェーズ | 内容 | 成果物 |
|---------|------|--------|
| **Phase 1** (現在) | 使用するペリフェラルのみ手書きで PAL を定義 | GPIO, RCC, USART, I2C, SPI, Timer の最小セット |
| **Phase 2** | 既存 svd2ral を umimmio API 向けにリファクタリング | パッチ済み SVD → umimmio 型ベースの C++ ヘッダ生成 |
| **Phase 3** | 全ペリフェラルの自動生成 + 手書きとの共存 | MCU 追加が低コストに |

Phase 1 の手書き定義は Phase 2-3 の生成コードと **同じ型・同じ構造** を使う。生成に移行しても利用側コードの変更は不要。

**既存資産（`/Users/tekitou/work/umi_mmio/.archive/tools/`）:**

Phase 2 で再利用可能なコンポーネント:
- SVD パーサー（derivedFrom 解決、レジスタ配列展開）
- バリデータ（アドレス重複、フィールド重複、列挙値範囲チェック）
- ペリフェラルグループ自動検出 → テンプレート化（v2）
- 配列レジスタ・フィールド配列の自動検出
- バックエンド自動選択（アドレス範囲によるDirect/I2C/SPI判定）
- YAML 設定スキーマ（出力モード、フィルタ、バックエンドマッピング）
- 命名規則変換・C++ 予約語エスケープ

### 3.2 判断 2: 配置場所 — umiport 内の ral/ サブディレクトリ

**根拠:** PAL の消費者は umiport 内のペリフェラルドライバであり、アプリケーションが直接参照することは稀。独立パッケージにするメリットよりも、umiport との近接性の方が重要。

```
lib/umiport/
├── include/umiport/
│   ├── mcu/
│   │   └── stm32f4/
│   │       ├── pal/                    ← PAL 定義
│   │       │   ├── gpio.hh             ← GPIO ペリフェラル定義
│   │       │   ├── rcc.hh              ← RCC ペリフェラル定義
│   │       │   ├── usart.hh            ← USART ペリフェラル定義
│   │       │   ├── i2c.hh              ← I2C ペリフェラル定義
│   │       │   ├── spi.hh              ← SPI ペリフェラル定義
│   │       │   ├── timer.hh            ← Timer ペリフェラル定義
│   │       │   └── common.hh           ← MCU 共通定義（ベースアドレスマップ等）
│   │       ├── gpio_driver.hh          ← Peripheral Driver (HAL 実装)
│   │       ├── uart_output.hh
│   │       └── ...
│   └── ...
```

将来 MCU ファミリが増えた場合:

```
lib/umiport/include/umiport/mcu/
├── stm32f4/pal/
├── stm32h7/pal/
├── nrf52/pal/
└── rp2040/pal/
```

### 3.3 判断 3: SVD パッチは stm32-rs 互換フォーマットを採用

**根拠:** stm32-rs コミュニティが数百のパッチを維持しており、このデータを再利用できる。独自パッチフォーマットを作る理由がない。

**パイプライン（Phase 2 以降）:**

```
ベンダー SVD
    │
    ├── stm32-rs YAML パッチ（転用 or 参照）
    │
    ↓
svdtools apply
    │
    ↓
パッチ済み SVD
    │
    ↓
umi-svd2cpp (独自ツール)
    │
    ↓
umimmio 型を使った C++ ヘッダ
```

### 3.4 判断 4: コード生成ツールは Python + Jinja2

**根拠:** modm (lbuild) と同じスタック。過去の svd2ral v1 で Jinja2 テンプレートベースの生成が実装・検証済み。SVD パースには既存の独自パーサー（`cmsis-svd` Python ライブラリも `.ref/` に保持）が使え、テンプレートベースの生成は保守性が高い。xmake のビルドフックから呼び出し可能。

---

## 4. PAL 定義の具体的な形式

### 4.1 ペリフェラル定義の例: GPIO

テンプレート化された Device でベースアドレスをパラメータ化し、`using` で各ポートをインスタンス化する。
これは現行コードベース（[gpio.hh](../../../../umi/port/mcu/stm32h7/mcu/gpio.hh) 等）で実証されたパターンである。

```cpp
// umiport/include/umiport/mcu/stm32f4/pal/gpio.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::mcu::stm32f4 {

namespace mm = umi::mmio;

/// @brief GPIO ペリフェラル定義 (テンプレート化)
/// @note STM32F4 Reference Manual RM0090 Section 8
template <mm::Addr BaseAddr>
struct GPIOx : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    /// @brief Mode Register — 各ピンの I/O 方向を設定 (2bit × 16pin)
    struct MODER : mm::Register<GPIOx, 0x00, 32, mm::RW, 0xA800'0000> {
        struct MODER0  : mm::Field<MODER, 0,  2> {};
        struct MODER1  : mm::Field<MODER, 2,  2> {};
        // ... MODER2-MODER12 ...
        struct MODER13 : mm::Field<MODER, 26, 2> {};
        struct MODER14 : mm::Field<MODER, 28, 2> {};
        struct MODER15 : mm::Field<MODER, 30, 2> {};
    };

    /// @brief Output Type Register (1bit × 16pin)
    struct OTYPER : mm::Register<GPIOx, 0x04, 32> {
        struct OT0  : mm::Field<OTYPER, 0,  1> {};  // 1-bit → Set/Reset 自動生成
        // ...
        struct OT15 : mm::Field<OTYPER, 15, 1> {};
    };

    /// @brief Output Speed Register (2bit × 16pin)
    struct OSPEEDR : mm::Register<GPIOx, 0x08, 32> {};

    /// @brief Pull-up/Pull-down Register (2bit × 16pin)
    struct PUPDR : mm::Register<GPIOx, 0x0C, 32, mm::RW, 0x6400'0000> {};

    /// @brief Input Data Register
    struct IDR : mm::Register<GPIOx, 0x10, 32, mm::RO> {};

    /// @brief Output Data Register
    struct ODR : mm::Register<GPIOx, 0x14, 32> {};

    /// @brief Bit Set/Reset Register
    struct BSRR : mm::Register<GPIOx, 0x18, 32, mm::WO> {};

    /// @brief Alternate Function Low Register (pin 0-7, 4bit × 8pin)
    struct AFRL : mm::Register<GPIOx, 0x20, 32> {};

    /// @brief Alternate Function High Register (pin 8-15, 4bit × 8pin)
    struct AFRH : mm::Register<GPIOx, 0x24, 32> {};
};

/// @brief GPIO ポートインスタンス
using GPIOA = GPIOx<0x4002'0000>;
using GPIOB = GPIOx<0x4002'0400>;
using GPIOC = GPIOx<0x4002'0800>;
using GPIOD = GPIOx<0x4002'0C00>;
using GPIOE = GPIOx<0x4002'1000>;
using GPIOF = GPIOx<0x4002'1400>;
using GPIOG = GPIOx<0x4002'1800>;
using GPIOH = GPIOx<0x4002'1C00>;
using GPIOI = GPIOx<0x4002'2000>;

/// @brief GPIO Mode 列挙値
namespace gpio_mode {
constexpr std::uint32_t INPUT     = 0b00;
constexpr std::uint32_t OUTPUT    = 0b01;
constexpr std::uint32_t ALTERNATE = 0b10;
constexpr std::uint32_t ANALOG    = 0b11;
} // namespace gpio_mode

} // namespace umi::mcu::stm32f4
```

**設計ポイント:**

| 項目 | 旧ドキュメント | 現行実装に準拠 |
|------|-------------|-------------|
| Register パラメータ | 7 個 (BitRegion 直接) | **5 個**: `<Parent, Offset, Bits, Access, Reset>` |
| Field パラメータ | 5 個 | **4 個**: `<Reg, BitOffset, BitWidth, Access>` |
| Access デフォルト | 明示的に `mm::Inherit` | 省略可（Register は `RW`、Field は `Inherit`）|
| Reset デフォルト | 明示的に `0` | 省略可（デフォルト `0`）|
| インスタンス化 | `Block<Gpio, Addr>` | **テンプレート + `using`** |
| 列挙値 | `enum class` | **namespace 内 `constexpr`** |

### 4.2 利用例

```cpp
#include <umiport/mcu/stm32f4/pal/gpio.hh>
#include <umimmio/transport/direct.hh>

using namespace umi::mcu::stm32f4;

mm::DirectTransport<> transport;

// PD13 を出力モードに設定 (modify = read-modify-write)
transport.modify(GPIOD::MODER::MODER13::value(gpio_mode::OUTPUT));

// PD13 を High に設定 (BSRR は write-only、write で全体書き込み)
transport.write(GPIOD::BSRR::value(1U << 13));

// PA0 の入力値を読み取り
auto idr = transport.read(GPIOA::IDR{});

// RCC クロックイネーブル — 1-bit Field は Set/Reset が自動生成される
transport.modify(RCC::AHB1ENR::GPIODEN::Set{});

// USART TXE フラグのポーリング
while (!transport.is(USART1::SR::TXE::Set{})) {}

// 複数フィールドの同時書き込み (単一バストランザクション)
transport.write(
    USART1::CR1::UE::Set{},
    USART1::CR1::TE::Set{},
    USART1::CR1::RE::Set{}
);
```

### 4.3 ペリフェラル定義とインスタンスの分離

上記の例では `GPIOx<BaseAddr>` がペリフェラル構造（レジスタ配置）をテンプレートで定義し、`GPIOA`〜`GPIOI` が `using` で具体的なベースアドレスを持つインスタンスとなる。この分離により:

- 同一 IP ブロックを持つペリフェラル間でレジスタ定義を共有（コードゼロ重複）
- SVD の `derivedFrom` に自然に対応
- svd2rust の「コード重複」問題を回避
- ベースアドレスの違いだけでインスタンスを追加可能

---

## 5. 列挙値の扱い

### 5.1 方針: 3段階の値表現

現行コードベースで実証された3つの値表現パターン:

**パターン 1: 1-bit Field の自動 Set/Reset**

最も頻繁に使用される。1-bit Field は `Set` と `Reset` が自動生成される。

```cpp
struct CR1 : mm::Register<USART1, 0x00, 32> {
    struct UE : mm::Field<CR1, 0, 1> {};  // → UE::Set{}, UE::Reset{} が自動生成
    struct TE : mm::Field<CR1, 3, 1> {};  // → TE::Set{}, TE::Reset{} が自動生成
};

// 使用
transport.modify(USART1::CR1::UE::Set{}, USART1::CR1::TE::Set{});
```

**パターン 2: namespace 定数 + `Field::value()`**

マルチビットフィールドの列挙値。namespace 内の `constexpr` 定数と `DynamicValue` を組み合わせる。

```cpp
namespace gpio_mode {
constexpr std::uint32_t INPUT     = 0b00;
constexpr std::uint32_t OUTPUT    = 0b01;
constexpr std::uint32_t ALTERNATE = 0b10;
constexpr std::uint32_t ANALOG    = 0b11;
} // namespace gpio_mode

// 使用: value() が DynamicValue を生成
transport.modify(GPIOD::MODER::MODER13::value(gpio_mode::OUTPUT));
```

**パターン 3: `mm::Value` による Register レベルの定数**

レジスタ全体に特定のマジックバリューを書く場合（パワーシーケンスなど）。

```cpp
struct POWER_CTL1 : mm::Register<CS43L22, 0x02, 8> {
    using PowerDown = mm::Value<POWER_CTL1, 0x01>;
    using PowerUp   = mm::Value<POWER_CTL1, 0x9E>;
};

// 使用
transport.write(CS43L22::POWER_CTL1::PowerUp{});
```

### 5.2 SVD に列挙値がない場合

SVD に enumeratedValues が定義されていないフィールドは、コード生成時に:

1. `Field::value(n)` による `DynamicValue` での任意値書き込みを許可
2. ユーザーが後から namespace 定数を手書き追加できるよう、拡張ポイントを設ける
3. stm32-rs パッチで列挙値を補完した SVD を使用（推奨）

---

## 6. ペリフェラルドライバからの利用パターン

### 6.1 現行の散在パターン（Before）

```cpp
// uart_output.hh 内にペリフェラル定義が埋め込まれている（最小限の定義）
struct RCC : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x40023800;
    struct APB2ENR : mm::Register<RCC, 0x44, 32> {
        struct USART1EN : mm::Field<APB2ENR, 4, 1> {};
    };
};
```

### 6.2 PAL 分離後のパターン（After）

```cpp
// ドライバは PAL をインクルードするだけ
#include <umiport/mcu/stm32f4/pal/rcc.hh>
#include <umiport/mcu/stm32f4/pal/gpio.hh>
#include <umiport/mcu/stm32f4/pal/usart.hh>

class Stm32f4UartOutput {
    mm::DirectTransport<> transport;

    void init() {
        // PAL 定義を直接参照 — 1-bit Field は Set{} で型安全に操作
        transport.modify(RCC::AHB1ENR::GPIODEN::Set{});
        transport.modify(RCC::APB2ENR::USART1EN::Set{});
        transport.modify(USART1::CR1::UE::Set{}, USART1::CR1::TE::Set{});
    }

    void putc(char c) {
        while (!transport.is(USART1::SR::TXE::Set{})) {}
        transport.write(USART1::DR::value(static_cast<std::uint32_t>(c)));
    }
};
```

---

## 7. SVD 生成パイプライン（Phase 2 設計）

### 7.1 ツール構成

既存の svd2ral v1/v2（`/Users/tekitou/work/umi_mmio/.archive/tools/`）を umimmio API 向けにリファクタリングして構築する。

```
tools/umi-svd2cpp/
├── svd2cpp.py              ← メインスクリプト（既存 cli.py ベース）
├── parser.py               ← SVD パーサー（既存 parser.py リファクタリング）
├── models.py               ← 中間表現（既存 models.py ベース）
├── validator.py            ← バリデータ（既存 validator.py 流用）
├── codegen/
│   ├── generator.py        ← umimmio 型向けコード生成（既存 generator.py 再設計）
│   ├── naming.py           ← 命名規則変換（既存 naming.py 流用）
│   └── backends.py         ← バックエンド選択（既存 backends.py 流用）
├── templates/
│   ├── peripheral.hh.j2    ← ペリフェラルヘッダテンプレート（umimmio API 向け新規）
│   ├── register.hh.j2      ← レジスタ定義テンプレート
│   └── instance.hh.j2      ← インスタンス（ベースアドレス）テンプレート
├── config/
│   ├── schema.py           ← 設定スキーマ（既存 schema.py ベース）
│   └── loader.py           ← 設定ローダー（既存 loader.py 流用）
├── patches/
│   └── stm32f4/            ← stm32-rs 互換パッチ
│       ├── stm32f407.yaml
│       └── ...
└── requirements.txt        ← svdtools, jinja2
```

### 7.2 生成フロー

```
vendor/STM32F407.svd
    │
    ↓ svdtools patch (stm32-rs 互換 YAML)
    │
patches/stm32f4/stm32f407.yaml
    │
    ↓ umi-svd2cpp.py --target stm32f407 --output lib/umiport/include/umiport/mcu/stm32f4/pal/
    │
    ↓ Jinja2 テンプレート処理
    │
生成物:
  ├── gpio.hh       ← Gpio struct + GPIOA..GPIOI instances
  ├── rcc.hh        ← Rcc struct + RCC instance
  ├── usart.hh      ← Usart struct + USART1..USART6 instances
  └── ...
```

### 7.3 テンプレート例

```jinja2
{# peripheral.hh.j2 #}
// Auto-generated from {{ svd_file }} — DO NOT EDIT
#pragma once
#include <umimmio/register.hh>

namespace umi::mcu::{{ family }} {

namespace mm = umi::mmio;

/// @brief {{ peripheral.description }}
template <mm::Addr BaseAddr>
struct {{ peripheral.name }}x : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;
{% for reg in peripheral.registers %}

    /// @brief {{ reg.description }}
{% if reg.access != "RW" or reg.reset != 0 %}
    struct {{ reg.name }} : mm::Register<{{ peripheral.name }}x, {{ "0x%04X" | format(reg.offset) }}, {{ reg.size }}{{ ", mm::" + reg.access if reg.access != "RW" else "" }}{{ ", 0x%08X" | format(reg.reset) if reg.reset != 0 else "" }}> {
{% else %}
    struct {{ reg.name }} : mm::Register<{{ peripheral.name }}x, {{ "0x%04X" | format(reg.offset) }}, {{ reg.size }}> {
{% endif %}
{% for field in reg.fields %}
        struct {{ field.name }} : mm::Field<{{ reg.name }}, {{ field.offset }}, {{ field.width }}> {};
{% endfor %}
    };
{% endfor %}
};

{% for instance in peripheral.instances %}
using {{ instance.name }} = {{ peripheral.name }}x<{{ "0x%08X" | format(instance.base) }}>;
{% endfor %}

} // namespace umi::mcu::{{ family }}
```

### 7.4 xmake 統合

```lua
-- xmake.lua 内
rule("umiport.ral-generate")
    before_build(function (target)
        local svd = target:values("umiport.svd")
        local patches = target:values("umiport.svd_patches")
        if svd then
            os.execv("python3", {
                "tools/umi-svd2cpp/svd2cpp.py",
                "--svd", svd,
                "--patches", patches,
                "--output", target:autogendir() .. "/pal/"
            })
        end
    end)
```

---

## 8. 外部デバイスの PAL

### 8.1 MCU 内蔵ペリフェラルとの違い

外部デバイス（I2C/SPI 接続のセンサ、DAC、CODEC 等）の PAL は:

- Transport が `I2CTransportTag` / `SPITransportTag`
- ベースアドレスはデバイスアドレス（I2C: 7-bit addr、SPI: CS ピン）
- レジスタは SVD ではなく**デバイスのデータシート**から手書き
- `umidevice/` に配置

### 8.2 例: CS43L22 (I2C Audio DAC)

実際のコードベース ([cs43l22_regs.hh](../../../../umidevice/include/umidevice/audio/cs43l22/cs43l22_regs.hh)) と同一パターン:

```cpp
// umidevice/include/umidevice/audio/cs43l22/cs43l22_regs.hh
namespace umi::device {

namespace mm = umi::mmio;

struct CS43L22 : mm::Device<mm::RW, mm::I2CTransportTag> {
    static constexpr std::uint8_t i2c_address = 0x4A;

    struct ID : mm::Register<CS43L22, 0x01, 8, mm::RO> {
        struct REVID  : mm::Field<ID, 0, 3> {};
        struct CHIPID : mm::Field<ID, 3, 5> {};
    };

    struct POWER_CTL1 : mm::Register<CS43L22, 0x02, 8> {
        using PowerDown = mm::Value<POWER_CTL1, 0x01>;
        using PowerUp   = mm::Value<POWER_CTL1, 0x9E>;
    };

    struct INTERFACE_CTL1 : mm::Register<CS43L22, 0x06, 8> {
        struct SLAVE    : mm::Field<INTERFACE_CTL1, 6, 1> {};
        struct DAC_IF   : mm::Field<INTERFACE_CTL1, 2, 2> {};
        struct AWL      : mm::Field<INTERFACE_CTL1, 0, 2> {};
        using I2s16Bit = mm::Value<INTERFACE_CTL1, 0x04>;
        using I2s24Bit = mm::Value<INTERFACE_CTL1, 0x06>;
    };
    // ...
};

} // namespace umi::device
```

使用例:

```cpp
I2cTransport<MockI2C> transport(i2c, CS43L22::i2c_address);
transport.write(CS43L22::POWER_CTL1::PowerUp{});
transport.write(CS43L22::INTERFACE_CTL1::I2s24Bit{});
```

この構造は MCU 内蔵ペリフェラルの PAL と **完全に同じ型システム** を使う。Transport の違いは `Device` テンプレートの `AllowedTransports` パラメータで表現される。

---

## 9. ファミリ間共有と derivedFrom 対応

### 9.1 同一 IP ブロックの共有

STM32F4 と STM32F7 は多くのペリフェラル IP を共有する。PAL 定義の重複を避けるため:

```
lib/umiport/include/umiport/mcu/
├── stm32_common/pal/
│   ├── gpio_v2.hh          ← STM32F4/F7/L4 共通 GPIO IP
│   └── usart_v2.hh         ← STM32F4/F7 共通 USART IP
├── stm32f4/pal/
│   ├── gpio.hh             ← #include "../stm32_common/pal/gpio_v2.hh" + インスタンス定義
│   ├── rcc.hh              ← F4 固有（RCC は MCU ファミリごとに異なる）
│   └── ...
└── stm32h7/pal/
    ├── gpio.hh             ← #include "../stm32_common/pal/gpio_v2.hh" + インスタンス定義
    ├── rcc.hh              ← H7 固有
    └── ...
```

### 9.2 同一ファミリ内の derivedFrom

SVD の `derivedFrom` はテンプレート化された Device + `using` エイリアスで自然に表現される:

```cpp
// GPIOB は GPIOA と同じレジスタ構造、ベースアドレスのみ異なる
template <mm::Addr BaseAddr>
struct GPIOx : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;
    /* レジスタ定義 */
};
using GPIOA = GPIOx<0x4002'0000>;
using GPIOB = GPIOx<0x4002'0400>;  // derivedFrom="GPIOA" — 同一テンプレート
```

---

## 10. 未解決の論点

### 10.1 列挙値の網羅的な生成

SVD に列挙値が存在しないフィールドが大半。Phase 2 でパッチを適用するとしても、全フィールドに列挙値を付与することは現実的でない。`DynamicValue` による任意値書き込みと、重要フィールドへの手動列挙値追加のバランスをどうとるか。

### 10.2 RCC の MCU ファミリ差異

RCC（クロック制御）は MCU ファミリごとに構造が大きく異なり、共有が困難。Phase 1 では各 MCU ファミリごとに手書きする。

### 10.3 レジスタ配列 (dim/dimIncrement)

SVD の `dim` による配列レジスタ（例: DMA チャネル 0-7）の umimmio での表現方法。C++ テンプレートの非型パラメータでインデックスを渡す設計が候補。既存 svd2ral v2 では `dim/dimIncrement` の自動展開が実装済みであり、このロジックを流用可能。

### 10.4 生成コードと手書きコードの共存ルール

Phase 2 以降、自動生成ファイルと手書き拡張ファイルが混在する。`// Auto-generated — DO NOT EDIT` マーカーによる区別と、手書き拡張用の別ヘッダ（`gpio_ext.hh`）を用意するパターンが候補。

### 10.5 コンパイル時間への影響

svd2rust の教訓から、MCU 全ペリフェラルを一つのヘッダに生成するとコンパイル時間が爆発する。ペリフェラルごとに個別ヘッダを生成し、必要なものだけ include する現在の方針を維持。

---

## 11. ロードマップ

| フェーズ | 内容 | 判断基準 |
|---------|------|---------|
| **Phase 1** | STM32F4 の GPIO, RCC, USART, I2C, SPI, Timer を手書き | 現行 umiport ドライバが PAL を参照する形にリファクタリングできれば完了 |
| **Phase 2** | 既存 svd2ral のリファクタリング + umimmio API 向け再設計。STM32F4 で検証 | 生成コードと手書きコードが同一の利用体験を提供できれば完了 |
| **Phase 3** | 複数 MCU ファミリへの展開 (STM32H7, nRF52 等) | 新 MCU 追加が「SVD + パッチ + 生成」のみで完結すれば完了 |

---

## 12. 参考文献

本提案は [04_ANALYSIS.md](04_ANALYSIS.md) の横断分析に基づく。特に以下のアプローチから設計要素を採用:

| 採用元 | 採用した要素 |
|--------|------------|
| **svd2rust** | クロージャ的な型安全 Write API の概念 |
| **chiptool** | ペリフェラル構造とインスタンスの分離（Fieldset 的アプローチ） |
| **modm** | Python + Jinja2 による生成スタック、ペリフェラルごとの個別ヘッダ |
| **stm32-rs** | YAML パッチフォーマットの互換性 |
| **mmio.hh (umi_mmio)** | Transport 統一設計（umimmio に継承済み）、svd2ral v1/v2 のパーサー・バリデータ・生成器（リファクタリングして再利用） |
| **Kvasir** | 型安全な列挙値による操作の概念 |
