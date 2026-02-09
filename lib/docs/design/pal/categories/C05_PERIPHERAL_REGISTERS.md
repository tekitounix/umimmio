# C5: ペリフェラルレジスタ

SVD (System View Description) ファイルの主要カバー範囲であり、PAL 全カテゴリの中で **最大のデータ量** を持つ。
GPIO, UART, SPI, I2C, Timer, ADC, DMA, USB 等、MCU のペリフェラルレジスタ定義を扱う。

---

## 1. 概要

ペリフェラルレジスタカテゴリは、MCU ベンダーが実装したペリフェラル IP のレジスタマップを型安全に表現する。
SVD ファイルから自動生成されるコードの大部分がこのカテゴリに属する。

**所属レイヤ**: L3 (MCU ファミリ固有)
ペリフェラルの「レジスタ構造」はファミリ内で共通。個々のインスタンスのベースアドレスは L3〜L4。

**umimmio との対応**:

| ハードウェア概念 | umimmio 型 | 役割 |
|----------------|-----------|------|
| ペリフェラル構造 | `mm::Device<Access, AllowedTransports...>` | デバイスルート（レジスタ構造定義） |
| ペリフェラルインスタンス | `template<mm::Addr> + using` | テンプレート Device に `using` でアドレス束縛 |
| レジスタ | `mm::Register<Parent, Offset, Bits, Access, Reset>` | オフセット付きレジスタ |
| ビットフィールド | `mm::Field<Register, BitOffset, BitWidth, Access>` | レジスタ内のフィールド |
| 列挙値 | `mm::Value<Field, EnumVal>` | コンパイル時列挙値 |
| 動的値 | `mm::DynamicValue<Field, T>` | 実行時値 |

---

## 2. 構成要素

### 2.1 ペリフェラル構造 (Device テンプレート)

ペリフェラルの「型」をテンプレートで定義する。レジスタレイアウトとアクセスポリシーを持ち、
ベースアドレスはテンプレートパラメータとして受け取る。

```cpp
template <mm::Addr BaseAddr>
struct GPIOx : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;
    // レジスタ定義がネストされる
};
```

### 2.2 ペリフェラルインスタンス (using エイリアス)

同一構造の複数インスタンスに、`using` でそれぞれ異なるベースアドレスを割り当てる。

```cpp
using GPIOA = GPIOx<0x4002'0000>;
using GPIOB = GPIOx<0x4002'0400>;
```

### 2.3 レジスタ (Register)

`Register` は `BitRegion` の `using` エイリアスであり、`IsRegister=true` を固定する。

```cpp
// Register<Parent, Offset, Bits, Access = RW, Reset = 0>
// → BitRegion<Parent, Offset, Bits, 0, Bits, Access, Reset, true>
struct MODER : mm::Register<GPIOx, 0x00, 32, mm::RW, 0xA800'0000> {};
```

### 2.4 ビットフィールド (Field)

レジスタ内の特定ビット範囲を型安全に切り出す。

```cpp
// Field<Register, BitOffset, BitWidth, Access = Inherit>
struct MODER0 : mm::Field<MODER, 0, 2> {};  // bit[1:0], アクセスは親レジスタを継承
```

1 ビットフィールドには自動的に `Set` / `Reset` が提供される:
```cpp
struct OT0 : mm::Field<OTYPER, 0, 1> {};
// OT0::Set  → Value<OT0, 1>
// OT0::Reset → Value<OT0, 0>
```

### 2.5 列挙値 (Value / DynamicValue)

```cpp
// コンパイル時列挙値
using ModeOutput = mm::Value<MODER0, static_cast<uint8_t>(GpioMode::OUTPUT)>;

// 実行時値
auto mode = MODER0::value(0b01);  // DynamicValue を返す
```

### 2.6 レジスタクラスタ

連続するレジスタ群をテンプレート Device でグループ化できる。DMA チャネルやタイマーチャネルなど、
繰り返し構造の表現に使用する。

```cpp
/// @brief DMA Stream registers (repeated structure)
template <mm::Addr BaseAddr>
struct DMAStreamx : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    struct CR   : mm::Register<DMAStreamx, 0x00, 32> {};
    struct NDTR : mm::Register<DMAStreamx, 0x04, 32> {};
    struct PAR  : mm::Register<DMAStreamx, 0x08, 32> {};
    struct M0AR : mm::Register<DMAStreamx, 0x0C, 32> {};
    struct M1AR : mm::Register<DMAStreamx, 0x10, 32> {};
    struct FCR  : mm::Register<DMAStreamx, 0x14, 32, mm::RW, 0x0000'0021> {};
};

// DMA1 の各ストリーム（0x18 バイト間隔）
using DMA1_S0 = DMAStreamx<0x4002'6010>;
using DMA1_S1 = DMAStreamx<0x4002'6028>;
using DMA1_S2 = DMAStreamx<0x4002'6040>;
// ...
```

---

## 3. プラットフォーム差異

同一機能のペリフェラルでも、ベンダー/ファミリにより IP 実装が大きく異なる。

| ペリフェラル | STM32F4 | RP2040 | ESP32-S3 | i.MX RT1060 |
|------------|---------|--------|----------|-------------|
| GPIO | MODER/OTYPER/OSPEEDR/PUPDR/IDR/ODR/BSRR/LCKR/AFR | SIO + IO_BANK0 + PADS_BANK0 (3 層) | GPIO Matrix (any-to-any) + IO MUX | IOMUXC + GPIO (DR/GDIR/PSR) |
| UART | USART (SR/DR/BRR/CR1-3) | PL011 準拠 UART | UART (FIFO_CONF, CLK_CONF) | LPUART (BAUD/STAT/CTRL/DATA) |
| SPI | SPI (CR1/CR2/SR/DR) | PL022 準拠 SPI | SPI (GP-SPI, CMD/ADDR/CTRL) | LPSPI (CR/SR/IER/CFGR) |
| I2C | I2C (CR1/CR2/OAR1/OAR2/DR/SR1/SR2) | I2C (IC_CON/IC_TAR) | I2C (SCL_LOW/SDA_HOLD) | LPI2C (MCR/MSR/MIER/MCFGR) |
| Timer | TIM (CR1/CR2/PSC/ARR/CCR) | PWM (CSR/DIV/TOP/CC) + Timer (64-bit) | Timer Group (TCONFIG/TLOADLO/THI/TLO) | GPT (CR/PR/SR/IR/OCR) |
| ADC | ADC (SR/CR1/CR2/SQR/DR) | ADC (CS/RESULT/FCS) | SAR ADC (CTRL/STATUS1/STATUS2) | ADC_ETC + ADC (HC/HS/R) |
| DMA | DMA (LISR/HISR/SxCR/SxNDTR) | 12ch DMA (READ_ADDR/WRITE_ADDR/TRANS_COUNT/CTRL_TRIG) | GDMA (IN_CONF/OUT_CONF) | eDMA (TCD: SADDR/SOFF/ATTR/NBYTES) |
| USB | OTG_FS/OTG_HS | USB (DPRAM ベース) | USB Serial/JTAG + USB OTG | USB (EHCI 互換) |

---

## 4. ペリフェラル構造とインスタンスの分離

PAL の最重要設計パターンは **テンプレート Device と `using` エイリアスによるインスタンス化** である。

```
┌──────────────────────────────────────────┐
│  template <mm::Addr BaseAddr>            │
│  struct GPIOx : mm::Device<...> {        │
│    static constexpr mm::Addr             │  ← L3: ファミリ共通
│      base_address = BaseAddr;            │
│    ・レジスタオフセット                    │
│    ・ビットフィールド                      │
│    ・アクセスポリシー                      │
│  };                                      │
└──────────┬───────────────────────────────┘
           │  using エイリアス
     ┌─────┴─────┬─────────┬─────────┐
     ▼           ▼         ▼         ▼
┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐
│ GPIOA  │ │ GPIOB  │ │ GPIOC  │ │ GPIOD  │  ← L3〜L4: インスタンス
│ 0x4002 │ │ 0x4002 │ │ 0x4002 │ │ 0x4002 │
│ 0000   │ │ 0400   │ │ 0800   │ │ 0C00   │
└────────┘ └────────┘ └────────┘ └────────┘
```

**利点**:

- **コード重複の排除**: 16 個の GPIO ポートで同一構造を共有
- **SVD `derivedFrom` との対応**: SVD の `derivedFrom` 属性は自然に `using` エイリアスで表現される
- **テスタビリティ**: Device 構造を MockTransport でテスト可能
- **アドレス計算の型安全性**: `GPIOx::base_address + Register::offset` がコンパイル時に解決

---

## 5. 生成ヘッダのコード例

### 5.1 STM32F4 GPIO（完全例）

```cpp
// pal/mcu/stm32f4/gpio.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::stm32f4 {

namespace mm = umi::mmio;

/// @brief GPIO Mode enumeration
enum class GpioMode : uint8_t {
    INPUT     = 0b00,
    OUTPUT    = 0b01,
    ALTERNATE = 0b10,
    ANALOG    = 0b11,
};

/// @brief GPIO Output Type
enum class GpioOutputType : uint8_t {
    PUSH_PULL  = 0,
    OPEN_DRAIN = 1,
};

/// @brief GPIO Output Speed
enum class GpioSpeed : uint8_t {
    LOW       = 0b00,
    MEDIUM    = 0b01,
    HIGH      = 0b10,
    VERY_HIGH = 0b11,
};

/// @brief GPIO Pull-up/Pull-down
enum class GpioPull : uint8_t {
    NONE      = 0b00,
    PULL_UP   = 0b01,
    PULL_DOWN = 0b10,
};

/// @brief STM32F4 GPIO peripheral register layout
template <mm::Addr BaseAddr>
struct GPIOx : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    /// @brief GPIO port mode register
    struct MODER : mm::Register<GPIOx, 0x00, 32, mm::RW, 0xA800'0000> {
        struct MODER0  : mm::Field<MODER, 0,  2> {};
        struct MODER1  : mm::Field<MODER, 2,  2> {};
        struct MODER2  : mm::Field<MODER, 4,  2> {};
        struct MODER3  : mm::Field<MODER, 6,  2> {};
        struct MODER4  : mm::Field<MODER, 8,  2> {};
        struct MODER5  : mm::Field<MODER, 10, 2> {};
        struct MODER6  : mm::Field<MODER, 12, 2> {};
        struct MODER7  : mm::Field<MODER, 14, 2> {};
        struct MODER8  : mm::Field<MODER, 16, 2> {};
        struct MODER9  : mm::Field<MODER, 18, 2> {};
        struct MODER10 : mm::Field<MODER, 20, 2> {};
        struct MODER11 : mm::Field<MODER, 22, 2> {};
        struct MODER12 : mm::Field<MODER, 24, 2> {};
        struct MODER13 : mm::Field<MODER, 26, 2> {};
        struct MODER14 : mm::Field<MODER, 28, 2> {};
        struct MODER15 : mm::Field<MODER, 30, 2> {};
    };

    /// @brief GPIO port output type register
    struct OTYPER : mm::Register<GPIOx, 0x04, 32> {
        struct OT0  : mm::Field<OTYPER, 0,  1> {};
        struct OT1  : mm::Field<OTYPER, 1,  1> {};
        struct OT2  : mm::Field<OTYPER, 2,  1> {};
        struct OT3  : mm::Field<OTYPER, 3,  1> {};
        struct OT4  : mm::Field<OTYPER, 4,  1> {};
        struct OT5  : mm::Field<OTYPER, 5,  1> {};
        struct OT6  : mm::Field<OTYPER, 6,  1> {};
        struct OT7  : mm::Field<OTYPER, 7,  1> {};
        struct OT8  : mm::Field<OTYPER, 8,  1> {};
        struct OT9  : mm::Field<OTYPER, 9,  1> {};
        struct OT10 : mm::Field<OTYPER, 10, 1> {};
        struct OT11 : mm::Field<OTYPER, 11, 1> {};
        struct OT12 : mm::Field<OTYPER, 12, 1> {};
        struct OT13 : mm::Field<OTYPER, 13, 1> {};
        struct OT14 : mm::Field<OTYPER, 14, 1> {};
        struct OT15 : mm::Field<OTYPER, 15, 1> {};
    };

    /// @brief GPIO port output speed register
    struct OSPEEDR : mm::Register<GPIOx, 0x08, 32> {
        struct OSPEEDR0  : mm::Field<OSPEEDR, 0,  2> {};
        struct OSPEEDR1  : mm::Field<OSPEEDR, 2,  2> {};
        // ... OSPEEDR2-OSPEEDR14
        struct OSPEEDR15 : mm::Field<OSPEEDR, 30, 2> {};
    };

    /// @brief GPIO port pull-up/pull-down register
    struct PUPDR : mm::Register<GPIOx, 0x0C, 32, mm::RW, 0x6400'0000> {
        struct PUPDR0  : mm::Field<PUPDR, 0,  2> {};
        struct PUPDR1  : mm::Field<PUPDR, 2,  2> {};
        // ... PUPDR2-PUPDR14
        struct PUPDR15 : mm::Field<PUPDR, 30, 2> {};
    };

    /// @brief GPIO port input data register (read-only)
    struct IDR : mm::Register<GPIOx, 0x10, 32, mm::RO> {
        struct IDR0  : mm::Field<IDR, 0,  1> {};
        // ... IDR1-IDR14
        struct IDR15 : mm::Field<IDR, 15, 1> {};
    };

    /// @brief GPIO port output data register
    struct ODR : mm::Register<GPIOx, 0x14, 32> {
        struct ODR0  : mm::Field<ODR, 0,  1> {};
        // ... ODR1-ODR14
        struct ODR15 : mm::Field<ODR, 15, 1> {};
    };

    /// @brief GPIO port bit set/reset register (write-only)
    struct BSRR : mm::Register<GPIOx, 0x18, 32, mm::WO> {
        struct BS0  : mm::Field<BSRR, 0,  1> {};  // Set bit 0
        // ... BS1-BS15
        struct BS15 : mm::Field<BSRR, 15, 1> {};
        struct BR0  : mm::Field<BSRR, 16, 1> {};  // Reset bit 0
        // ... BR1-BR15
        struct BR15 : mm::Field<BSRR, 31, 1> {};
    };

    /// @brief GPIO port configuration lock register
    struct LCKR : mm::Register<GPIOx, 0x1C, 32> {};

    /// @brief GPIO alternate function low register (pins 0-7)
    struct AFRL : mm::Register<GPIOx, 0x20, 32> {
        struct AFRL0 : mm::Field<AFRL, 0,  4> {};
        struct AFRL1 : mm::Field<AFRL, 4,  4> {};
        struct AFRL2 : mm::Field<AFRL, 8,  4> {};
        struct AFRL3 : mm::Field<AFRL, 12, 4> {};
        struct AFRL4 : mm::Field<AFRL, 16, 4> {};
        struct AFRL5 : mm::Field<AFRL, 20, 4> {};
        struct AFRL6 : mm::Field<AFRL, 24, 4> {};
        struct AFRL7 : mm::Field<AFRL, 28, 4> {};
    };

    /// @brief GPIO alternate function high register (pins 8-15)
    struct AFRH : mm::Register<GPIOx, 0x24, 32> {
        struct AFRH8  : mm::Field<AFRH, 0,  4> {};
        struct AFRH9  : mm::Field<AFRH, 4,  4> {};
        struct AFRH10 : mm::Field<AFRH, 8,  4> {};
        struct AFRH11 : mm::Field<AFRH, 12, 4> {};
        struct AFRH12 : mm::Field<AFRH, 16, 4> {};
        struct AFRH13 : mm::Field<AFRH, 20, 4> {};
        struct AFRH14 : mm::Field<AFRH, 24, 4> {};
        struct AFRH15 : mm::Field<AFRH, 28, 4> {};
    };
};

// ─────────────────────────────────────────────────────────────
// Peripheral instances (base addresses)
// STM32F40x/41x: GPIOA-GPIOI on AHB1 bus
// ─────────────────────────────────────────────────────────────
using GPIOA = GPIOx<0x4002'0000>;
using GPIOB = GPIOx<0x4002'0400>;
using GPIOC = GPIOx<0x4002'0800>;
using GPIOD = GPIOx<0x4002'0C00>;
using GPIOE = GPIOx<0x4002'1000>;
using GPIOF = GPIOx<0x4002'1400>;
using GPIOG = GPIOx<0x4002'1800>;
using GPIOH = GPIOx<0x4002'1C00>;
using GPIOI = GPIOx<0x4002'2000>;

} // namespace umi::pal::stm32f4
```

**使用例** (DirectTransport):

```cpp
#include <umimmio/mmio.hh>
#include <pal/mcu/stm32f4/gpio.hh>

using namespace umi::pal::stm32f4;
using namespace umi::mmio;

// DirectTransport はベースアドレスの volatile ポインタ経由でレジスタアクセス
DirectTransport hw;

// PA5 を出力モードに設定（LED ピン）
hw.modify(GPIOA::MODER::MODER5::value(static_cast<uint8_t>(GpioMode::OUTPUT)));

// PA5 の出力を High に設定（BSRR 書き込み）
hw.write(GPIOA::BSRR::BS5::Set{});

// 入力ピンの状態を読み取り
auto pin_state = hw.read(GPIOA::IDR::IDR0{});
```

### 5.2 RP2040 GPIO（3 層アーキテクチャ）

RP2040 の GPIO は STM32 と異なり、3 つの独立したペリフェラルで構成される:

```
┌─────────────────┐   ┌──────────────────┐   ┌──────────────────┐
│ SIO (0xD000_0000)│   │ IO_BANK0         │   │ PADS_BANK0       │
│                 │   │ (0x4001_4000)    │   │ (0x4001_C000)    │
│ 高速 GPIO 入出力│   │ 機能選択 (FUNCSEL)│   │ 電気特性         │
│ ・GPIO_IN       │   │ ・GPIO0_CTRL     │   │ ・GPIO0 パッド   │
│ ・GPIO_OUT      │   │ ・GPIO1_CTRL     │   │ ・ドライブ強度   │
│ ・GPIO_OE       │   │ ・GPIO0_STATUS   │   │ ・プルアップ/ダウン│
│ (シングルサイクル)│   │ ・割り込みマスク  │   │ ・スルーレート   │
└─────────────────┘   └──────────────────┘   └──────────────────┘
```

```cpp
// pal/mcu/rp2040/gpio.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::rp2040 {

namespace mm = umi::mmio;

// ─────────────────────────────────────────────────────────────
// SIO — Single-cycle IO (fast GPIO access, no wait states)
// RP2040 固有: バスマトリクスを介さず直接アクセス可能
// ─────────────────────────────────────────────────────────────
struct SIO : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0xD000'0000;

    /// @brief GPIO input value (all 30 pins)
    struct GPIO_IN : mm::Register<SIO, 0x004, 32, mm::RO> {
        // bit[29:0] = GPIO0-GPIO29 input state
    };

    /// @brief GPIO output value
    struct GPIO_OUT : mm::Register<SIO, 0x010, 32> {};

    /// @brief GPIO output set (atomic, write-only)
    struct GPIO_OUT_SET : mm::Register<SIO, 0x014, 32, mm::WO> {};

    /// @brief GPIO output clear (atomic, write-only)
    struct GPIO_OUT_CLR : mm::Register<SIO, 0x018, 32, mm::WO> {};

    /// @brief GPIO output XOR (atomic toggle, write-only)
    struct GPIO_OUT_XOR : mm::Register<SIO, 0x01C, 32, mm::WO> {};

    /// @brief GPIO output enable
    struct GPIO_OE : mm::Register<SIO, 0x020, 32> {};

    /// @brief GPIO output enable set (atomic, write-only)
    struct GPIO_OE_SET : mm::Register<SIO, 0x024, 32, mm::WO> {};

    /// @brief GPIO output enable clear (atomic, write-only)
    struct GPIO_OE_CLR : mm::Register<SIO, 0x028, 32, mm::WO> {};
};

// ─────────────────────────────────────────────────────────────
// IO_BANK0 — GPIO function selection + interrupt control
// 各 GPIO に STATUS (RO) + CTRL (RW) のペアが 8 バイト間隔で配置
// ─────────────────────────────────────────────────────────────
struct IO_BANK0 : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x4001'4000;

    /// @brief GPIO0 status (read-only, shows current pin state and overrides)
    struct GPIO0_STATUS : mm::Register<IO_BANK0, 0x000, 32, mm::RO> {
        struct OUTFROMPERI : mm::Field<GPIO0_STATUS, 8,  1> {};
        struct OUTTOPAD    : mm::Field<GPIO0_STATUS, 9,  1> {};
        struct INFROMPAD   : mm::Field<GPIO0_STATUS, 17, 1> {};
        struct INTOPERI    : mm::Field<GPIO0_STATUS, 19, 1> {};
        struct IRQFROMPAD  : mm::Field<GPIO0_STATUS, 24, 1> {};
        struct IRQTOPROC   : mm::Field<GPIO0_STATUS, 26, 1> {};
    };

    /// @brief GPIO0 control (function select, overrides, interrupt config)
    struct GPIO0_CTRL : mm::Register<IO_BANK0, 0x004, 32, mm::RW, 0x1F> {
        /// @brief Function select (5-bit): selects which peripheral drives this GPIO
        struct FUNCSEL   : mm::Field<GPIO0_CTRL, 0, 5> {};
        struct OUTOVER   : mm::Field<GPIO0_CTRL, 8,  2> {};
        struct OEOVER    : mm::Field<GPIO0_CTRL, 12, 2> {};
        struct INOVER    : mm::Field<GPIO0_CTRL, 16, 2> {};
        struct IRQOVER   : mm::Field<GPIO0_CTRL, 28, 2> {};
    };

    // GPIO1-GPIO29: STATUS at 8*n, CTRL at 8*n+4
    struct GPIO1_STATUS : mm::Register<IO_BANK0, 0x008, 32, mm::RO> {};
    struct GPIO1_CTRL   : mm::Register<IO_BANK0, 0x00C, 32, mm::RW, 0x1F> {
        struct FUNCSEL : mm::Field<GPIO1_CTRL, 0, 5> {};
    };
    // ... GPIO2-GPIO28 at 8-byte intervals
    struct GPIO29_STATUS : mm::Register<IO_BANK0, 0x0E8, 32, mm::RO> {};
    struct GPIO29_CTRL   : mm::Register<IO_BANK0, 0x0EC, 32, mm::RW, 0x1F> {
        struct FUNCSEL : mm::Field<GPIO29_CTRL, 0, 5> {};
    };

    // Interrupt registers (per-processor)
    struct INTR0 : mm::Register<IO_BANK0, 0x0F0, 32> {};
    struct INTR1 : mm::Register<IO_BANK0, 0x0F4, 32> {};
    struct INTR2 : mm::Register<IO_BANK0, 0x0F8, 32> {};
    struct INTR3 : mm::Register<IO_BANK0, 0x0FC, 32> {};
};

// ─────────────────────────────────────────────────────────────
// PADS_BANK0 — Pad electrical characteristics
// ─────────────────────────────────────────────────────────────
struct PADS_BANK0 : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x4001'C000;

    /// @brief Voltage select for all pads
    struct VOLTAGE_SELECT : mm::Register<PADS_BANK0, 0x00, 32> {
        struct VOLTAGE_SELECT_BIT : mm::Field<VOLTAGE_SELECT, 0, 1> {};
    };

    /// @brief GPIO0 pad control
    struct GPIO0 : mm::Register<PADS_BANK0, 0x04, 32, mm::RW, 0x56> {
        struct SLEWFAST : mm::Field<GPIO0, 0, 1> {};  // Slew rate
        struct SCHMITT  : mm::Field<GPIO0, 1, 1> {};  // Schmitt trigger
        struct PDE      : mm::Field<GPIO0, 2, 1> {};  // Pull-down enable
        struct PUE      : mm::Field<GPIO0, 3, 1> {};  // Pull-up enable
        struct DRIVE    : mm::Field<GPIO0, 4, 2> {};  // Drive strength (2/4/8/12 mA)
        struct IE       : mm::Field<GPIO0, 6, 1> {};  // Input enable
        struct OD       : mm::Field<GPIO0, 7, 1> {};  // Output disable
    };
    // GPIO1-GPIO29 at 4-byte intervals (0x08, 0x0C, ...)
};

} // namespace umi::pal::rp2040
```

**STM32 と RP2040 の GPIO モデリングの違い**:

| 側面 | STM32 | RP2040 |
|------|-------|--------|
| Device 数 | 1 (GPIOx) | 3 (SIO, IO_BANK0, PADS_BANK0) |
| 入出力制御 | MODER + ODR/IDR + BSRR | SIO (GPIO_OUT/GPIO_IN/GPIO_OE) |
| 機能選択 | AFR (4bit/pin, GPIOx 内) | IO_BANK0 FUNCSEL (5bit/pin, 別 Device) |
| 電気特性 | OTYPER + OSPEEDR + PUPDR (GPIOx 内) | PADS_BANK0 (別 Device) |
| アトミック操作 | BSRR (set/reset) | SIO の SET/CLR/XOR レジスタ |
| インスタンス | GPIOA-GPIOI (ポート単位) | SIO/IO_BANK0/PADS_BANK0 各1つ |

### 5.3 STM32F4 USART

```cpp
// pal/mcu/stm32f4/usart.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::stm32f4 {

namespace mm = umi::mmio;

/// @brief Word length
enum class UsartWordLength : uint8_t {
    BITS_8 = 0,
    BITS_9 = 1,
};

/// @brief Stop bits
enum class UsartStopBits : uint8_t {
    STOP_1   = 0b00,
    STOP_0_5 = 0b01,
    STOP_2   = 0b10,
    STOP_1_5 = 0b11,
};

/// @brief Oversampling mode
enum class UsartOversampling : uint8_t {
    BY_16 = 0,
    BY_8  = 1,
};

/// @brief STM32F4 USART peripheral register layout
template <mm::Addr BaseAddr>
struct USARTx : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    /// @brief Status register
    struct SR : mm::Register<USARTx, 0x00, 32, mm::RW, 0x00C0'0000> {
        struct PE   : mm::Field<SR, 0,  1, mm::RO> {};  // Parity error
        struct FE   : mm::Field<SR, 1,  1, mm::RO> {};  // Framing error
        struct NF   : mm::Field<SR, 2,  1, mm::RO> {};  // Noise detected
        struct ORE  : mm::Field<SR, 3,  1, mm::RO> {};  // Overrun error
        struct IDLE : mm::Field<SR, 4,  1, mm::RO> {};  // IDLE line detected
        struct RXNE : mm::Field<SR, 5,  1> {};           // Read data register not empty
        struct TC   : mm::Field<SR, 6,  1> {};           // Transmission complete
        struct TXE  : mm::Field<SR, 7,  1, mm::RO> {};  // Transmit data register empty
        struct LBD  : mm::Field<SR, 8,  1> {};           // LIN break detection
        struct CTS  : mm::Field<SR, 9,  1> {};           // CTS flag
    };

    /// @brief Data register
    struct DR : mm::Register<USARTx, 0x04, 32> {
        struct DATA : mm::Field<DR, 0, 9> {};
    };

    /// @brief Baud rate register
    struct BRR : mm::Register<USARTx, 0x08, 32> {
        struct DIV_FRACTION : mm::Field<BRR, 0,  4> {};
        struct DIV_MANTISSA : mm::Field<BRR, 4, 12> {};
    };

    /// @brief Control register 1
    struct CR1 : mm::Register<USARTx, 0x0C, 32> {
        struct SBK    : mm::Field<CR1, 0,  1> {};  // Send break
        struct RWU    : mm::Field<CR1, 1,  1> {};  // Receiver wakeup
        struct RE     : mm::Field<CR1, 2,  1> {};  // Receiver enable
        struct TE     : mm::Field<CR1, 3,  1> {};  // Transmitter enable
        struct IDLEIE : mm::Field<CR1, 4,  1> {};  // IDLE interrupt enable
        struct RXNEIE : mm::Field<CR1, 5,  1> {};  // RXNE interrupt enable
        struct TCIE   : mm::Field<CR1, 6,  1> {};  // TC interrupt enable
        struct TXEIE  : mm::Field<CR1, 7,  1> {};  // TXE interrupt enable
        struct PEIE   : mm::Field<CR1, 8,  1> {};  // PE interrupt enable
        struct PS     : mm::Field<CR1, 9,  1> {};  // Parity selection
        struct PCE    : mm::Field<CR1, 10, 1> {};  // Parity control enable
        struct M      : mm::Field<CR1, 12, 1> {};  // Word length
        struct UE     : mm::Field<CR1, 13, 1> {};  // USART enable
        struct OVER8  : mm::Field<CR1, 15, 1> {};  // Oversampling mode
    };

    /// @brief Control register 2
    struct CR2 : mm::Register<USARTx, 0x10, 32> {
        struct STOP  : mm::Field<CR2, 12, 2> {};  // Stop bits
        struct LINEN : mm::Field<CR2, 14, 1> {};  // LIN mode enable
    };

    /// @brief Control register 3
    struct CR3 : mm::Register<USARTx, 0x14, 32> {
        struct EIE   : mm::Field<CR3, 0,  1> {};  // Error interrupt enable
        struct IREN  : mm::Field<CR3, 1,  1> {};  // IrDA mode enable
        struct HDSEL : mm::Field<CR3, 3,  1> {};  // Half-duplex selection
        struct DMAR  : mm::Field<CR3, 6,  1> {};  // DMA enable receiver
        struct DMAT  : mm::Field<CR3, 7,  1> {};  // DMA enable transmitter
        struct RTSE  : mm::Field<CR3, 8,  1> {};  // RTS enable
        struct CTSE  : mm::Field<CR3, 9,  1> {};  // CTS enable
        struct CTSIE : mm::Field<CR3, 10, 1> {};  // CTS interrupt enable
        struct ONEBIT : mm::Field<CR3, 11, 1> {}; // One sample bit method enable
    };

    /// @brief Guard time and prescaler register
    struct GTPR : mm::Register<USARTx, 0x18, 32> {
        struct PSC : mm::Field<GTPR, 0, 8> {};
        struct GT  : mm::Field<GTPR, 8, 8> {};
    };
};

// Peripheral instances
using USART1 = USARTx<0x4001'1000>;  // APB2
using USART2 = USARTx<0x4000'4400>;  // APB1
using USART3 = USARTx<0x4000'4800>;  // APB1
using UART4  = USARTx<0x4000'4C00>;  // APB1 (UART, no sync mode)
using UART5  = USARTx<0x4000'5000>;  // APB1 (UART, no sync mode)
using USART6 = USARTx<0x4001'1400>;  // APB2

} // namespace umi::pal::stm32f4
```

### 5.4 i.MX RT1060 LPUART（ベンダー差異の例）

i.MX RT の LPUART は STM32 USART とは全く異なるレジスタ構造を持つ。
同じ「UART」でも、IP が異なるため PAL では完全に別の Device として定義される。

```cpp
// pal/mcu/imxrt1060/lpuart.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::imxrt1060 {

namespace mm = umi::mmio;

/// @brief i.MX RT1060 LPUART peripheral register layout
template <mm::Addr BaseAddr>
struct LPUARTx : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    /// @brief Version ID register (read-only)
    struct VERID : mm::Register<LPUARTx, 0x00, 32, mm::RO, 0x0400'0003> {};

    /// @brief Parameter register (read-only)
    struct PARAM : mm::Register<LPUARTx, 0x04, 32, mm::RO> {
        struct TXFIFO : mm::Field<PARAM, 0, 8, mm::RO> {};   // TX FIFO depth
        struct RXFIFO : mm::Field<PARAM, 8, 8, mm::RO> {};   // RX FIFO depth
    };

    /// @brief Global register
    struct GLOBAL : mm::Register<LPUARTx, 0x08, 32> {
        struct RST : mm::Field<GLOBAL, 1, 1> {};  // Software reset
    };

    /// @brief Baud rate register
    struct BAUD : mm::Register<LPUARTx, 0x10, 32, mm::RW, 0x0F00'0004> {
        struct SBR     : mm::Field<BAUD, 0,  13> {};  // Baud rate modulo divisor
        struct SBNS    : mm::Field<BAUD, 13, 1> {};   // Stop bit number select
        struct BOTHEDGE : mm::Field<BAUD, 17, 1> {};  // Both edge sampling
        struct OSR     : mm::Field<BAUD, 24, 5> {};   // Oversampling ratio
    };

    /// @brief Status register
    struct STAT : mm::Register<LPUARTx, 0x14, 32, mm::RW, 0x00C0'0000> {
        struct RDRF : mm::Field<STAT, 21, 1, mm::RO> {};  // Receive data register full
        struct TC   : mm::Field<STAT, 22, 1, mm::RO> {};  // Transmission complete
        struct TDRE : mm::Field<STAT, 23, 1, mm::RO> {};  // Transmit data register empty
    };

    /// @brief Control register
    struct CTRL : mm::Register<LPUARTx, 0x18, 32> {
        struct RE  : mm::Field<CTRL, 18, 1> {};  // Receiver enable
        struct TE  : mm::Field<CTRL, 19, 1> {};  // Transmitter enable
        struct RIE : mm::Field<CTRL, 21, 1> {};  // Receiver interrupt enable
        struct TIE : mm::Field<CTRL, 23, 1> {};  // Transmitter interrupt enable
    };

    /// @brief Data register
    struct DATA : mm::Register<LPUARTx, 0x1C, 32> {
        struct R0T0 : mm::Field<DATA, 0, 8> {};  // Read/transmit data
    };

    /// @brief Watermark register
    struct WATER : mm::Register<LPUARTx, 0x2C, 32> {
        struct TXWATER : mm::Field<WATER, 0,  2> {};
        struct TXCOUNT : mm::Field<WATER, 8,  3, mm::RO> {};
        struct RXWATER : mm::Field<WATER, 16, 2> {};
        struct RXCOUNT : mm::Field<WATER, 24, 3, mm::RO> {};
    };
};

// Peripheral instances (8 LPUART instances on i.MX RT1060)
using LPUART1 = LPUARTx<0x4018'4000>;
using LPUART2 = LPUARTx<0x4018'8000>;
using LPUART3 = LPUARTx<0x4018'C000>;
using LPUART4 = LPUARTx<0x4019'0000>;
using LPUART5 = LPUARTx<0x4019'4000>;
using LPUART6 = LPUARTx<0x4019'8000>;
using LPUART7 = LPUARTx<0x4019'C000>;
using LPUART8 = LPUARTx<0x401A'0000>;

} // namespace umi::pal::imxrt1060
```

---

## 6. ファミリ間共有と IP バージョン

### 6.1 IP 共有の実態

多くのペリフェラルは複数の MCU ファミリで同一の IP ブロックを共有する。

| IP ブロック | 共有ファミリ | 差異 |
|-----------|------------|------|
| STM32 GPIO (v2) | STM32F4, STM32F7, STM32L4, STM32G4 | レジスタ構造同一、リセット値が異なる場合あり |
| STM32 GPIO (v1) | STM32F1 | 完全に異なる構造 (CRL/CRH 方式) |
| PL011 UART | RP2040, RP2350 | ARM 標準 IP、レジスタ配置同一 |
| LPUART | i.MX RT 全シリーズ | NXP 共通 IP |

### 6.2 SVD `derivedFrom` の扱い

SVD ファイルでは `derivedFrom` 属性で「GPIOB は GPIOA と同じ構造」を表現する:

```xml
<peripheral derivedFrom="GPIOA">
  <name>GPIOB</name>
  <baseAddress>0x40020400</baseAddress>
</peripheral>
```

umimmio ではテンプレート + `using` パターンで自然に表現される:

```cpp
// GPIOA と GPIOB は同じ GPIOx テンプレートを共有
using GPIOA = GPIOx<0x4002'0000>;  // = derivedFrom の参照元
using GPIOB = GPIOx<0x4002'0400>;  // = derivedFrom 先
```

### 6.3 ファミリ間での Device 再利用

同一 IP を使用するファミリ間では、Device 定義を `mcu/common/` に配置して共有できる:

```
pal/
├── mcu/
│   ├── common/
│   │   └── stm32_gpio_v2.hh    # STM32F4/F7/L4/G4 共通 GPIO 構造
│   ├── stm32f4/
│   │   └── gpio.hh             # #include "../common/stm32_gpio_v2.hh" + F4 固有インスタンス
│   └── stm32f7/
│       └── gpio.hh             # #include "../common/stm32_gpio_v2.hh" + F7 固有インスタンス
```

---

## 7. データソース

| データソース | 取得方法 | カバー範囲 | 備考 |
|------------|---------|-----------|------|
| **SVD ファイル** | CMSIS-Pack / ベンダー配布 | レジスタ、フィールド、列挙値、アクセスポリシー | 最も包括的。品質はベンダーにより差がある |
| **CMSIS-SVD パッチ** | cmsis-svd, stm32-rs/stm32-data 等 | SVD の誤り修正 | STM32 は活発なパッチコミュニティあり |
| **リファレンスマニュアル** | PDF (ベンダー公式) | レジスタ記述、ビットフィールド詳細 | SVD にない情報の補完に使用 |
| **RP2040 SVD** | raspberrypi/pico-sdk | SIO, IO_BANK0, PADS 等 | 公式 SVD 品質は良好 |
| **ESP-IDF SVD** | espressif/esp-idf | GPIO Matrix, UART, SPI 等 | SVD 品質にばらつきあり。レジスタ記述ファイルも参照 |
| **i.MX RT SVD** | NXP MCUXpresso SDK | LPUART, LPSPI, IOMUXC 等 | 大規模 SVD。フィールド名がユニーク |
