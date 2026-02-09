# C7: クロックツリー

クロックツリーは MCU の全ペリフェラルの動作速度を決定する基盤であり、
PLL 設定、バスプリスケーラ、ペリフェラルクロックイネーブル/リセットを含む。

---

## 1. 概要

クロックツリーカテゴリは以下を扱う:

- **クロックソース**: 内蔵 RC オシレータ (HSI/IRC)、外部水晶 (HSE/XOSC)
- **PLL (Phase-Locked Loop)**: クロック逓倍・分周
- **バスプリスケーラ**: AHB, APB 等のバスクロック分周
- **ペリフェラルクロックイネーブル**: 個別ペリフェラルのクロック供給制御
- **ペリフェラルリセット**: 個別ペリフェラルのソフトウェアリセット
- **クロックゲーティング**: 低消費電力モードでのクロック制御

**所属レイヤ**: L3 (MCU ファミリ固有)
クロックツリーの構造はファミリ内で共通。最大周波数等のパラメータは L4 (バリアント固有)。

**他カテゴリとの関係**:

| 関連カテゴリ | 関係 |
|------------|------|
| C5 ペリフェラルレジスタ | クロック制御レジスタ (RCC, CCM 等) はペリフェラルの一種 |
| C9 電力管理 | 低消費電力モードではクロックゲーティングと密接に連携 |
| C4 メモリマップ | クロック制御ペリフェラルのベースアドレス |

---

## 2. 構成要素

### 2.1 クロックソース

| 種類 | 説明 | 例 |
|------|------|-----|
| 高速内蔵 RC | 起動直後のデフォルトクロック | HSI (STM32, 16MHz), IRC (i.MX RT, 24MHz), ROSC (RP2040, 6.5MHz) |
| 高速外部水晶 | 精度の高い外部クロック | HSE (STM32, 8-25MHz), XOSC (RP2040, 12MHz), XTAL (ESP32, 40MHz) |
| 低速内蔵 RC | RTC / ウォッチドッグ用 | LSI (STM32, 32kHz), LP_OSC (i.MX RT, 32kHz) |
| 低速外部水晶 | 高精度 RTC 用 | LSE (STM32, 32.768kHz) |

### 2.2 PLL

PLL はクロックソースを逓倍・分周して高速クロックを生成する。

```
                 入力クロック
                     │
                 ┌───▼───┐
                 │  /M   │  ← 入力分周 (VCO 入力を 1-2MHz 帯に)
                 └───┬───┘
                     │
                 ┌───▼───┐
                 │  xN   │  ← VCO 逓倍 (192-432MHz 帯に)
                 └───┬───┘
                     │
              ┌──────┼──────┐
          ┌───▼───┐  │  ┌───▼───┐
          │  /P   │  │  │  /Q   │  ← 出力分周 (用途別)
          └───┬───┘  │  └───┬───┘
              │      │      │
         SYSCLK   (予備)  USB CLK (48MHz)
```

### 2.3 バスプリスケーラ

```
SYSCLK ──► AHB Prescaler ──► HCLK (AHB bus, max 168MHz)
                                │
                          ┌─────┴─────┐
                     ┌────▼────┐  ┌───▼────┐
                     │APB1 /x  │  │APB2 /x │
                     └────┬────┘  └───┬────┘
                          │           │
                     PCLK1 (max 42MHz)  PCLK2 (max 84MHz)
                     USART2,3    USART1,6
                     SPI2,3      SPI1
                     I2C1,2,3    TIM1,8
                     TIM2-7      ADC1-3
```

### 2.4 ペリフェラルクロックイネーブル

MCU はデフォルトでほとんどのペリフェラルクロックを無効化しており、
使用前に明示的にイネーブルする必要がある。

```cpp
// STM32: RCC の AHB1ENR/APB1ENR/APB2ENR レジスタ
hw.modify(Rcc::AHB1ENR::GPIOAEN::Set{});   // GPIOA クロックイネーブル

// RP2040: RESETS ペリフェラルでリセット解除 (= イネーブル)
hw.modify(Resets::RESET::IO_BANK0::Reset{}); // IO_BANK0 リセット解除

// i.MX RT: CCM の CCGR レジスタでクロックゲーティング制御
hw.modify(Ccm::CCGR1::CG13::value(0b11));  // LPUART1 クロック常時ON
```

### 2.5 クロックゲーティング

低消費電力モードで特定のペリフェラルクロックを停止する機能。
ベンダーにより制御粒度が異なる:

| ベンダー | 制御方式 | 粒度 |
|---------|---------|------|
| STM32 | RCC_AHBxLPENR / APBxLPENR | ペリフェラル単位 (Sleep モード用) |
| RP2040 | WAKE_EN レジスタ | ペリフェラル単位 |
| ESP32 | PCR (Peripheral Clock Register) | ペリフェラル単位 + クロック分周 |
| i.MX RT | CCM CCGR (Clock Gating Register) | ペリフェラル単位 (3 段階: OFF/RUN/ALWAYS) |

---

## 3. プラットフォーム差異

| 側面 | STM32F4 | RP2040 | ESP32-S3 | i.MX RT1060 |
|------|---------|--------|----------|-------------|
| **クロック制御ペリフェラル** | RCC (Reset and Clock Control) | CLOCKS + RESETS + PLL | SYSTEM (PCR 等) | CCM (Clock Controller Module) |
| **PLL 数** | 1 メイン PLL + 1 PLLI2S | 2 (PLL_SYS, PLL_USB) | 1 CPLL + 1 APLL | 7 PLL (ARM, SYS, USB, Audio, Video, ENET, ...) |
| **PLL 複雑度** | 中 (PLLM/N/P/Q) | 低 (FBDIV/POSTDIV1/POSTDIV2) | 中 (分数 PLL) | 高 (多段分周 + Frac-N) |
| **バス構造** | AHB → APB1/APB2 | AHB-Lite (単一バス) | AHB/APB (HP/LP 分離) | AHB → IPG (=APB相当, 多段) |
| **イネーブル方式** | RCC_xxxENR ビットセット | RESETS レジスタでリセット解除 | PCR レジスタ | CCM_CCGR ビットフィールド |
| **リセット方式** | RCC_xxxRSTR ビットセット | RESETS レジスタでリセットアサート | PCR レジスタ | SRC (System Reset Controller) |
| **最大 SYSCLK** | 168MHz (F407) | 133MHz | 240MHz | 600MHz |
| **USB クロック** | PLL48CK (PLLQ=48MHz) | PLL_USB (48MHz) | PLL (48MHz 分周) | USB1_PLL (480MHz / 10) |

---

## 4. 生成ヘッダのコード例

### 4.1 STM32F4 RCC

```cpp
// pal/mcu/stm32f4/rcc.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::stm32f4 {

namespace mm = umi::mmio;

/// @brief Clock source selection
enum class SysClkSource : uint8_t {
    HSI = 0b00,  // High-Speed Internal (16 MHz)
    HSE = 0b01,  // High-Speed External (8-25 MHz)
    PLL = 0b10,  // PLL output
};

/// @brief PLL clock source
enum class PllSource : uint8_t {
    HSI = 0,
    HSE = 1,
};

/// @brief AHB prescaler values
enum class AhbPrescaler : uint8_t {
    DIV1   = 0b0000,   // SYSCLK not divided
    DIV2   = 0b1000,
    DIV4   = 0b1001,
    DIV8   = 0b1010,
    DIV16  = 0b1011,
    DIV64  = 0b1100,
    DIV128 = 0b1101,
    DIV256 = 0b1110,
    DIV512 = 0b1111,
};

/// @brief APB prescaler values
enum class ApbPrescaler : uint8_t {
    DIV1  = 0b000,   // HCLK not divided
    DIV2  = 0b100,
    DIV4  = 0b101,
    DIV8  = 0b110,
    DIV16 = 0b111,
};

/// @brief STM32F4 RCC peripheral register layout
struct Rcc : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x4002'3800;

    /// @brief Clock control register
    struct CR : mm::Register<Rcc, 0x00, 32, mm::RW, 0x0000'0083> {
        struct HSION     : mm::Field<CR, 0,  1> {};    // HSI oscillator ON
        struct HSIRDY    : mm::Field<CR, 1,  1, mm::RO> {};  // HSI ready flag
        struct HSITRIM   : mm::Field<CR, 3,  5> {};    // HSI trimming
        struct HSICAL    : mm::Field<CR, 8,  8, mm::RO> {};  // HSI calibration
        struct HSEON     : mm::Field<CR, 16, 1> {};    // HSE oscillator ON
        struct HSERDY    : mm::Field<CR, 17, 1, mm::RO> {};  // HSE ready flag
        struct HSEBYP    : mm::Field<CR, 18, 1> {};    // HSE bypass
        struct CSSON     : mm::Field<CR, 19, 1> {};    // Clock security system ON
        struct PLLON     : mm::Field<CR, 24, 1> {};    // Main PLL ON
        struct PLLRDY    : mm::Field<CR, 25, 1, mm::RO> {};  // Main PLL ready
        struct PLLI2SON  : mm::Field<CR, 26, 1> {};    // PLLI2S ON
        struct PLLI2SRDY : mm::Field<CR, 27, 1, mm::RO> {};  // PLLI2S ready
    };

    /// @brief PLL configuration register
    /// PLL output = (PLL input / PLLM) * PLLN / PLLP
    /// USB clock  = (PLL input / PLLM) * PLLN / PLLQ  (must be 48 MHz)
    struct PLLCFGR : mm::Register<Rcc, 0x04, 32, mm::RW, 0x2400'3010> {
        struct PLLM   : mm::Field<PLLCFGR, 0,  6> {};   // Division factor for VCO input (2-63)
        struct PLLN   : mm::Field<PLLCFGR, 6,  9> {};   // Multiplication factor for VCO (50-432)
        struct PLLP   : mm::Field<PLLCFGR, 16, 2> {};   // Division for main SYSCLK (00=/2, 01=/4, 10=/6, 11=/8)
        struct PLLSRC : mm::Field<PLLCFGR, 22, 1> {};   // PLL source: 0=HSI, 1=HSE
        struct PLLQ   : mm::Field<PLLCFGR, 24, 4> {};   // Division for USB OTG FS / SDIO (2-15)
    };

    /// @brief Clock configuration register
    struct CFGR : mm::Register<Rcc, 0x08, 32> {
        struct SW    : mm::Field<CFGR, 0,  2> {};   // System clock switch (00=HSI, 01=HSE, 10=PLL)
        struct SWS   : mm::Field<CFGR, 2,  2, mm::RO> {};  // System clock switch status
        struct HPRE  : mm::Field<CFGR, 4,  4> {};   // AHB prescaler
        struct PPRE1 : mm::Field<CFGR, 10, 3> {};   // APB1 (low-speed) prescaler
        struct PPRE2 : mm::Field<CFGR, 13, 3> {};   // APB2 (high-speed) prescaler
        struct RTCPRE : mm::Field<CFGR, 16, 5> {};  // HSE division for RTC
        struct MCO1  : mm::Field<CFGR, 21, 2> {};   // MCO1 source selection
        struct I2SSCR : mm::Field<CFGR, 23, 1> {};  // I2S clock source
        struct MCO1PRE : mm::Field<CFGR, 24, 3> {}; // MCO1 prescaler
        struct MCO2PRE : mm::Field<CFGR, 27, 3> {}; // MCO2 prescaler
        struct MCO2  : mm::Field<CFGR, 30, 2> {};   // MCO2 source selection
    };

    /// @brief Clock interrupt register
    struct CIR : mm::Register<Rcc, 0x0C, 32> {
        struct LSIRDYF   : mm::Field<CIR, 0,  1, mm::RO> {};
        struct LSERDYF   : mm::Field<CIR, 1,  1, mm::RO> {};
        struct HSIRDYF   : mm::Field<CIR, 2,  1, mm::RO> {};
        struct HSERDYF   : mm::Field<CIR, 3,  1, mm::RO> {};
        struct PLLRDYF   : mm::Field<CIR, 4,  1, mm::RO> {};
        struct PLLI2SRDYF : mm::Field<CIR, 5,  1, mm::RO> {};
        struct CSSF      : mm::Field<CIR, 7,  1, mm::RO> {};
        struct LSIRDYIE  : mm::Field<CIR, 8,  1> {};
        struct LSERDYIE  : mm::Field<CIR, 9,  1> {};
        struct HSIRDYIE  : mm::Field<CIR, 10, 1> {};
        struct HSERDYIE  : mm::Field<CIR, 11, 1> {};
        struct PLLRDYIE  : mm::Field<CIR, 12, 1> {};
        struct LSIRDYC   : mm::Field<CIR, 16, 1, mm::WO> {};
        struct LSERDYC   : mm::Field<CIR, 17, 1, mm::WO> {};
        struct HSIRDYC   : mm::Field<CIR, 18, 1, mm::WO> {};
        struct HSERDYC   : mm::Field<CIR, 19, 1, mm::WO> {};
        struct PLLRDYC   : mm::Field<CIR, 20, 1, mm::WO> {};
        struct CSSC      : mm::Field<CIR, 23, 1, mm::WO> {};
    };

    // ─────────────────────────────────────────────────────────
    // AHB1 peripheral clock enable / reset
    // ─────────────────────────────────────────────────────────

    /// @brief AHB1 peripheral clock enable register
    struct AHB1ENR : mm::Register<Rcc, 0x30, 32, mm::RW, 0x0010'0000> {
        struct GPIOAEN  : mm::Field<AHB1ENR, 0,  1> {};
        struct GPIOBEN  : mm::Field<AHB1ENR, 1,  1> {};
        struct GPIOCEN  : mm::Field<AHB1ENR, 2,  1> {};
        struct GPIODEN  : mm::Field<AHB1ENR, 3,  1> {};
        struct GPIOEEN  : mm::Field<AHB1ENR, 4,  1> {};
        struct GPIOFEN  : mm::Field<AHB1ENR, 5,  1> {};
        struct GPIOGEN  : mm::Field<AHB1ENR, 6,  1> {};
        struct GPIOHEN  : mm::Field<AHB1ENR, 7,  1> {};
        struct GPIOIEN  : mm::Field<AHB1ENR, 8,  1> {};
        struct CRCEN    : mm::Field<AHB1ENR, 12, 1> {};
        struct DMA1EN   : mm::Field<AHB1ENR, 21, 1> {};
        struct DMA2EN   : mm::Field<AHB1ENR, 22, 1> {};
        struct OTGHSEN  : mm::Field<AHB1ENR, 29, 1> {};
    };

    /// @brief AHB1 peripheral reset register
    struct AHB1RSTR : mm::Register<Rcc, 0x10, 32> {
        struct GPIOARST : mm::Field<AHB1RSTR, 0,  1> {};
        struct GPIOBRST : mm::Field<AHB1RSTR, 1,  1> {};
        struct GPIOCRST : mm::Field<AHB1RSTR, 2,  1> {};
        struct GPIODRST : mm::Field<AHB1RSTR, 3,  1> {};
        struct GPIOERST : mm::Field<AHB1RSTR, 4,  1> {};
        struct DMA1RST  : mm::Field<AHB1RSTR, 21, 1> {};
        struct DMA2RST  : mm::Field<AHB1RSTR, 22, 1> {};
    };

    // ─────────────────────────────────────────────────────────
    // APB1 peripheral clock enable / reset
    // ─────────────────────────────────────────────────────────

    /// @brief APB1 peripheral clock enable register
    struct APB1ENR : mm::Register<Rcc, 0x40, 32> {
        struct TIM2EN    : mm::Field<APB1ENR, 0,  1> {};
        struct TIM3EN    : mm::Field<APB1ENR, 1,  1> {};
        struct TIM4EN    : mm::Field<APB1ENR, 2,  1> {};
        struct TIM5EN    : mm::Field<APB1ENR, 3,  1> {};
        struct TIM6EN    : mm::Field<APB1ENR, 4,  1> {};
        struct TIM7EN    : mm::Field<APB1ENR, 5,  1> {};
        struct TIM12EN   : mm::Field<APB1ENR, 6,  1> {};
        struct TIM13EN   : mm::Field<APB1ENR, 7,  1> {};
        struct TIM14EN   : mm::Field<APB1ENR, 8,  1> {};
        struct WWDGEN    : mm::Field<APB1ENR, 11, 1> {};
        struct SPI2EN    : mm::Field<APB1ENR, 14, 1> {};
        struct SPI3EN    : mm::Field<APB1ENR, 15, 1> {};
        struct USART2EN  : mm::Field<APB1ENR, 17, 1> {};
        struct USART3EN  : mm::Field<APB1ENR, 18, 1> {};
        struct UART4EN   : mm::Field<APB1ENR, 19, 1> {};
        struct UART5EN   : mm::Field<APB1ENR, 20, 1> {};
        struct I2C1EN    : mm::Field<APB1ENR, 21, 1> {};
        struct I2C2EN    : mm::Field<APB1ENR, 22, 1> {};
        struct I2C3EN    : mm::Field<APB1ENR, 23, 1> {};
        struct CAN1EN    : mm::Field<APB1ENR, 25, 1> {};
        struct CAN2EN    : mm::Field<APB1ENR, 26, 1> {};
        struct PWREN     : mm::Field<APB1ENR, 28, 1> {};
        struct DACEN     : mm::Field<APB1ENR, 29, 1> {};
    };

    /// @brief APB1 peripheral reset register
    struct APB1RSTR : mm::Register<Rcc, 0x20, 32> {
        struct TIM2RST   : mm::Field<APB1RSTR, 0,  1> {};
        struct USART2RST : mm::Field<APB1RSTR, 17, 1> {};
        struct I2C1RST   : mm::Field<APB1RSTR, 21, 1> {};
        struct SPI2RST   : mm::Field<APB1RSTR, 14, 1> {};
    };

    // ─────────────────────────────────────────────────────────
    // APB2 peripheral clock enable / reset
    // ─────────────────────────────────────────────────────────

    /// @brief APB2 peripheral clock enable register
    struct APB2ENR : mm::Register<Rcc, 0x44, 32> {
        struct TIM1EN    : mm::Field<APB2ENR, 0,  1> {};
        struct TIM8EN    : mm::Field<APB2ENR, 1,  1> {};
        struct USART1EN  : mm::Field<APB2ENR, 4,  1> {};
        struct USART6EN  : mm::Field<APB2ENR, 5,  1> {};
        struct ADC1EN    : mm::Field<APB2ENR, 8,  1> {};
        struct ADC2EN    : mm::Field<APB2ENR, 9,  1> {};
        struct ADC3EN    : mm::Field<APB2ENR, 10, 1> {};
        struct SDIOEN    : mm::Field<APB2ENR, 11, 1> {};
        struct SPI1EN    : mm::Field<APB2ENR, 12, 1> {};
        struct SYSCFGEN  : mm::Field<APB2ENR, 14, 1> {};
        struct TIM9EN    : mm::Field<APB2ENR, 16, 1> {};
        struct TIM10EN   : mm::Field<APB2ENR, 17, 1> {};
        struct TIM11EN   : mm::Field<APB2ENR, 18, 1> {};
    };

    /// @brief APB2 peripheral reset register
    struct APB2RSTR : mm::Register<Rcc, 0x24, 32> {
        struct USART1RST : mm::Field<APB2RSTR, 4,  1> {};
        struct SPI1RST   : mm::Field<APB2RSTR, 12, 1> {};
        struct SYSCFGRST : mm::Field<APB2RSTR, 14, 1> {};
    };

    // ─────────────────────────────────────────────────────────
    // Low-power mode clock enable (Sleep mode)
    // ─────────────────────────────────────────────────────────

    /// @brief AHB1 peripheral clock enable in low-power mode
    struct AHB1LPENR : mm::Register<Rcc, 0x50, 32, mm::RW, 0x7E67'91FF> {
        struct GPIOALPEN : mm::Field<AHB1LPENR, 0,  1> {};
        struct GPIOBLPEN : mm::Field<AHB1LPENR, 1,  1> {};
        struct GPIOCLPEN : mm::Field<AHB1LPENR, 2,  1> {};
        struct DMA1LPEN  : mm::Field<AHB1LPENR, 21, 1> {};
        struct DMA2LPEN  : mm::Field<AHB1LPENR, 22, 1> {};
    };

    // ─────────────────────────────────────────────────────────
    // Backup domain / clock source control
    // ─────────────────────────────────────────────────────────

    /// @brief Backup domain control register
    struct BDCR : mm::Register<Rcc, 0x70, 32> {
        struct LSEON  : mm::Field<BDCR, 0,  1> {};   // LSE oscillator ON
        struct LSERDY : mm::Field<BDCR, 1,  1, mm::RO> {};  // LSE ready
        struct LSEBYP : mm::Field<BDCR, 2,  1> {};   // LSE bypass
        struct RTCSEL : mm::Field<BDCR, 8,  2> {};   // RTC clock source (00=no, 01=LSE, 10=LSI, 11=HSE/RTCPRE)
        struct RTCEN  : mm::Field<BDCR, 15, 1> {};   // RTC clock enable
        struct BDRST  : mm::Field<BDCR, 16, 1> {};   // Backup domain reset
    };

    /// @brief Clock control & status register
    struct CSR : mm::Register<Rcc, 0x74, 32, mm::RW, 0x0E00'0000> {
        struct LSION   : mm::Field<CSR, 0,  1> {};   // LSI oscillator ON
        struct LSIRDY  : mm::Field<CSR, 1,  1, mm::RO> {};  // LSI ready
        struct RMVF    : mm::Field<CSR, 24, 1> {};   // Remove reset flag
        struct BORRSTF : mm::Field<CSR, 25, 1, mm::RO> {};  // BOR reset flag
        struct PINRSTF : mm::Field<CSR, 26, 1, mm::RO> {};  // PIN reset flag
        struct PORRSTF : mm::Field<CSR, 27, 1, mm::RO> {};  // POR/PDR reset flag
        struct SFTRSTF : mm::Field<CSR, 28, 1, mm::RO> {};  // Software reset flag
        struct IWDGRSTF : mm::Field<CSR, 29, 1, mm::RO> {}; // IWDG reset flag
        struct WWDGRSTF : mm::Field<CSR, 30, 1, mm::RO> {}; // WWDG reset flag
        struct LPWRRSTF : mm::Field<CSR, 31, 1, mm::RO> {}; // Low-power reset flag
    };

    /// @brief Spread spectrum clock generation register
    struct SSCGR : mm::Register<Rcc, 0x80, 32> {
        struct MODPER    : mm::Field<SSCGR, 0,  13> {};
        struct INCSTEP   : mm::Field<SSCGR, 13, 15> {};
        struct SPREADSEL : mm::Field<SSCGR, 30, 1> {};
        struct SSCGEN    : mm::Field<SSCGR, 31, 1> {};
    };

    /// @brief PLLI2S configuration register
    struct PLLI2SCFGR : mm::Register<Rcc, 0x84, 32, mm::RW, 0x2000'3000> {
        struct PLLI2SN : mm::Field<PLLI2SCFGR, 6,  9> {};  // PLLI2S VCO multiplier
        struct PLLI2SR : mm::Field<PLLI2SCFGR, 28, 3> {};  // PLLI2S output divider for I2S
    };
};

/// @brief RCC peripheral instance
using RCC = Rcc;

// ─────────────────────────────────────────────────────────────
// PLL 制約 (constexpr, コンパイル時検証に使用)
// ─────────────────────────────────────────────────────────────

/// @brief PLL parameter constraints (from Reference Manual)
namespace pll_constraints {
    constexpr uint32_t vco_input_min  = 1'000'000;    // 1 MHz
    constexpr uint32_t vco_input_max  = 2'000'000;    // 2 MHz
    constexpr uint32_t vco_output_min = 192'000'000;  // 192 MHz
    constexpr uint32_t vco_output_max = 432'000'000;  // 432 MHz
    constexpr uint32_t pllm_min = 2;
    constexpr uint32_t pllm_max = 63;
    constexpr uint32_t plln_min = 50;
    constexpr uint32_t plln_max = 432;
    constexpr uint32_t pllp_values[] = {2, 4, 6, 8};  // PLLP encoding: 00=/2, 01=/4, 10=/6, 11=/8
    constexpr uint32_t pllq_min = 2;
    constexpr uint32_t pllq_max = 15;
}

/// @brief Bus frequency limits
namespace bus_limits {
    constexpr uint32_t sysclk_max = 168'000'000;  // 168 MHz (STM32F407)
    constexpr uint32_t ahb_max    = 168'000'000;  // HCLK max
    constexpr uint32_t apb1_max   = 42'000'000;   // PCLK1 max (low-speed APB)
    constexpr uint32_t apb2_max   = 84'000'000;   // PCLK2 max (high-speed APB)
}

/// @brief Common PLL configurations (for typical crystal frequencies)
namespace pll_config {
    /// 8 MHz HSE → 168 MHz SYSCLK, 48 MHz USB
    /// VCO input = 8M / 8 = 1 MHz, VCO output = 1M * 336 = 336 MHz
    /// SYSCLK = 336M / 2 = 168 MHz, USB = 336M / 7 = 48 MHz
    constexpr uint32_t hse_8mhz_pllm = 8;
    constexpr uint32_t hse_8mhz_plln = 336;
    constexpr uint32_t hse_8mhz_pllp = 0;   // /2
    constexpr uint32_t hse_8mhz_pllq = 7;

    /// 25 MHz HSE → 168 MHz SYSCLK, 48 MHz USB
    /// VCO input = 25M / 25 = 1 MHz, VCO output = 1M * 336 = 336 MHz
    constexpr uint32_t hse_25mhz_pllm = 25;
    constexpr uint32_t hse_25mhz_plln = 336;
    constexpr uint32_t hse_25mhz_pllp = 0;   // /2
    constexpr uint32_t hse_25mhz_pllq = 7;
}

} // namespace umi::pal::stm32f4
```

**使用例** (クロック初期化シーケンス):

```cpp
using namespace umi::pal::stm32f4;

DirectTransport hw;

// 1. HSE を有効化して安定化を待つ
hw.modify(Rcc::CR::HSEON::Set{});
while (!hw.is(Rcc::CR::HSERDY::Set{})) {}

// 2. PLL を設定 (8 MHz HSE → 168 MHz SYSCLK)
hw.write(
    Rcc::PLLCFGR::PLLM::value(pll_config::hse_8mhz_pllm),
    Rcc::PLLCFGR::PLLN::value(pll_config::hse_8mhz_plln),
    Rcc::PLLCFGR::PLLP::value(pll_config::hse_8mhz_pllp),
    Rcc::PLLCFGR::PLLQ::value(pll_config::hse_8mhz_pllq),
    Rcc::PLLCFGR::PLLSRC::Set{}   // HSE as PLL source
);

// 3. PLL を有効化して安定化を待つ
hw.modify(Rcc::CR::PLLON::Set{});
while (!hw.is(Rcc::CR::PLLRDY::Set{})) {}

// 4. バスプリスケーラ設定
hw.modify(
    Rcc::CFGR::HPRE::value(static_cast<uint8_t>(AhbPrescaler::DIV1)),
    Rcc::CFGR::PPRE1::value(static_cast<uint8_t>(ApbPrescaler::DIV4)),
    Rcc::CFGR::PPRE2::value(static_cast<uint8_t>(ApbPrescaler::DIV2))
);

// 5. システムクロックを PLL に切替
hw.modify(Rcc::CFGR::SW::value(static_cast<uint8_t>(SysClkSource::PLL)));
while (hw.read(Rcc::CFGR::SWS{}) != static_cast<uint8_t>(SysClkSource::PLL)) {}
```

### 4.2 RP2040 クロック

```cpp
// pal/mcu/rp2040/clocks.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::rp2040 {

namespace mm = umi::mmio;

// ─────────────────────────────────────────────────────────────
// XOSC — Crystal Oscillator (12 MHz on most boards)
// ─────────────────────────────────────────────────────────────
struct Xosc : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x4002'4000;

    struct CTRL : mm::Register<Xosc, 0x00, 32> {
        struct FREQ_RANGE : mm::Field<CTRL, 0,  12> {};  // 0xAA0 = 1-15 MHz
        struct ENABLE     : mm::Field<CTRL, 12, 12> {};  // 0xFAB = enable
    };

    struct STATUS : mm::Register<Xosc, 0x04, 32> {
        struct FREQ_RANGE : mm::Field<STATUS, 0,  2, mm::RO> {};
        struct ENABLED    : mm::Field<STATUS, 12, 1, mm::RO> {};
        struct STABLE     : mm::Field<STATUS, 31, 1, mm::RO> {};
    };

    struct STARTUP : mm::Register<Xosc, 0x0C, 32, mm::RW, 0xC4> {
        struct DELAY : mm::Field<STARTUP, 0, 14> {};
    };
};

using XOSC = Xosc;

// ─────────────────────────────────────────────────────────────
// PLL — Phase-Locked Loop (PLL_SYS and PLL_USB)
//
// Output = (XOSC / REFDIV) * FBDIV / (POSTDIV1 * POSTDIV2)
// PLL_SYS: 12 MHz / 1 * 133 / (6 * 2) = 133 MHz (default)
// PLL_USB: 12 MHz / 1 * 100 / (5 * 5) = 48 MHz (for USB)
// ─────────────────────────────────────────────────────────────
template <mm::Addr BaseAddr>
struct Pllx : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    struct CS : mm::Register<Pllx, 0x00, 32, mm::RW, 0x01> {
        struct REFDIV : mm::Field<CS, 0, 6> {};    // Reference divider (default 1)
        struct BYPASS : mm::Field<CS, 8, 1> {};    // PLL bypass
        struct LOCK   : mm::Field<CS, 31, 1, mm::RO> {};  // PLL lock indicator
    };

    struct PWR : mm::Register<Pllx, 0x04, 32, mm::RW, 0x2D> {
        struct PD      : mm::Field<PWR, 0, 1> {};  // PLL power down
        struct DSMPD   : mm::Field<PWR, 2, 1> {};  // DSM power down
        struct POSTDIVPD : mm::Field<PWR, 3, 1> {};  // Post divider power down
        struct VCOPD   : mm::Field<PWR, 5, 1> {};  // VCO power down
    };

    struct FBDIV_INT : mm::Register<Pllx, 0x08, 32> {
        struct FBDIV : mm::Field<FBDIV_INT, 0, 12> {};  // Feedback divider (16-320)
    };

    struct PRIM : mm::Register<Pllx, 0x0C, 32, mm::RW, 0x0007'7000> {
        struct POSTDIV2 : mm::Field<PRIM, 12, 3> {};  // Post divider 2 (1-7)
        struct POSTDIV1 : mm::Field<PRIM, 16, 3> {};  // Post divider 1 (1-7)
    };
};

using PLL_SYS = Pllx<0x4002'8000>;
using PLL_USB = Pllx<0x4002'C000>;

// ─────────────────────────────────────────────────────────────
// CLOCKS — Clock mux and divider control
// RP2040 has multiple clock generators, each with a source mux
// ─────────────────────────────────────────────────────────────
struct Clocks : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x4000'8000;

    /// @brief Reference clock control (source for watchdog, timer)
    struct CLK_REF_CTRL : mm::Register<Clocks, 0x30, 32> {
        struct SRC     : mm::Field<CLK_REF_CTRL, 0, 2> {};  // 0=ROSC, 1=AUX, 2=XOSC
        struct AUXSRC  : mm::Field<CLK_REF_CTRL, 5, 2> {};
    };

    /// @brief System clock control (main CPU clock)
    struct CLK_SYS_CTRL : mm::Register<Clocks, 0x3C, 32> {
        struct SRC    : mm::Field<CLK_SYS_CTRL, 0, 1> {};  // 0=CLK_REF, 1=AUX
        struct AUXSRC : mm::Field<CLK_SYS_CTRL, 5, 3> {};  // 0=PLL_SYS, ...
    };

    struct CLK_SYS_DIV : mm::Register<Clocks, 0x40, 32, mm::RW, 0x0000'0100> {
        struct FRAC : mm::Field<CLK_SYS_DIV, 0,  8> {};
        struct INT  : mm::Field<CLK_SYS_DIV, 8, 24> {};
    };

    /// @brief Peripheral clock control (for UART, SPI, etc.)
    struct CLK_PERI_CTRL : mm::Register<Clocks, 0x48, 32> {
        struct AUXSRC : mm::Field<CLK_PERI_CTRL, 5, 3> {};  // 0=CLK_SYS, 1=PLL_SYS, ...
        struct ENABLE : mm::Field<CLK_PERI_CTRL, 11, 1> {};
    };

    /// @brief USB clock control (must be 48 MHz)
    struct CLK_USB_CTRL : mm::Register<Clocks, 0x54, 32> {
        struct AUXSRC : mm::Field<CLK_USB_CTRL, 5, 3> {};  // 0=PLL_USB, ...
        struct ENABLE : mm::Field<CLK_USB_CTRL, 11, 1> {};
    };

    /// @brief ADC clock control (must be 48 MHz)
    struct CLK_ADC_CTRL : mm::Register<Clocks, 0x60, 32> {
        struct AUXSRC : mm::Field<CLK_ADC_CTRL, 5, 3> {};
        struct ENABLE : mm::Field<CLK_ADC_CTRL, 11, 1> {};
    };

    /// @brief RTC clock control (ideally 46875 Hz = 48 MHz / 1024)
    struct CLK_RTC_CTRL : mm::Register<Clocks, 0x6C, 32> {
        struct AUXSRC : mm::Field<CLK_RTC_CTRL, 5, 3> {};
        struct ENABLE : mm::Field<CLK_RTC_CTRL, 11, 1> {};
    };

    struct CLK_RTC_DIV : mm::Register<Clocks, 0x70, 32> {
        struct FRAC : mm::Field<CLK_RTC_DIV, 0,  8> {};
        struct INT  : mm::Field<CLK_RTC_DIV, 8, 24> {};
    };
};

using CLOCKS = Clocks;

// ─────────────────────────────────────────────────────────────
// RESETS — Peripheral reset control (RP2040 unique)
//
// RP2040 はペリフェラルクロックイネーブルではなく
// RESETS ペリフェラルでリセット制御する方式を採用
// リセット解除 = クロックイネーブル に相当
// ─────────────────────────────────────────────────────────────
struct Resets : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x4000'C000;

    /// @brief Reset control (write 1 to assert reset)
    struct RESET : mm::Register<Resets, 0x00, 32, mm::RW, 0x01FF'FFFF> {
        struct ADC       : mm::Field<RESET, 0,  1> {};
        struct BUSCTRL   : mm::Field<RESET, 1,  1> {};
        struct DMA       : mm::Field<RESET, 2,  1> {};
        struct I2C0      : mm::Field<RESET, 3,  1> {};
        struct I2C1      : mm::Field<RESET, 4,  1> {};
        struct IO_BANK0  : mm::Field<RESET, 5,  1> {};
        struct IO_QSPI   : mm::Field<RESET, 6,  1> {};
        struct JTAG      : mm::Field<RESET, 7,  1> {};
        struct PADS_BANK0 : mm::Field<RESET, 8,  1> {};
        struct PADS_QSPI : mm::Field<RESET, 9,  1> {};
        struct PIO0      : mm::Field<RESET, 10, 1> {};
        struct PIO1      : mm::Field<RESET, 11, 1> {};
        struct PLL_SYS   : mm::Field<RESET, 12, 1> {};
        struct PLL_USB   : mm::Field<RESET, 13, 1> {};
        struct PWM       : mm::Field<RESET, 14, 1> {};
        struct RTC       : mm::Field<RESET, 15, 1> {};
        struct SPI0      : mm::Field<RESET, 16, 1> {};
        struct SPI1      : mm::Field<RESET, 17, 1> {};
        struct SYSCFG    : mm::Field<RESET, 18, 1> {};
        struct SYSINFO   : mm::Field<RESET, 19, 1> {};
        struct TBMAN     : mm::Field<RESET, 20, 1> {};
        struct TIMER     : mm::Field<RESET, 21, 1> {};
        struct UART0     : mm::Field<RESET, 22, 1> {};
        struct UART1     : mm::Field<RESET, 23, 1> {};
        struct USBCTRL   : mm::Field<RESET, 24, 1> {};
    };

    /// @brief Watchdog "should have been reset" (for warm boot)
    struct WDSEL : mm::Register<Resets, 0x04, 32> {};

    /// @brief Reset done status (read-only, 1 = reset complete)
    struct RESET_DONE : mm::Register<Resets, 0x08, 32, mm::RO> {
        struct ADC       : mm::Field<RESET_DONE, 0,  1, mm::RO> {};
        struct IO_BANK0  : mm::Field<RESET_DONE, 5,  1, mm::RO> {};
        struct PADS_BANK0 : mm::Field<RESET_DONE, 8,  1, mm::RO> {};
        struct PLL_SYS   : mm::Field<RESET_DONE, 12, 1, mm::RO> {};
        struct PLL_USB   : mm::Field<RESET_DONE, 13, 1, mm::RO> {};
        struct UART0     : mm::Field<RESET_DONE, 22, 1, mm::RO> {};
        struct UART1     : mm::Field<RESET_DONE, 23, 1, mm::RO> {};
        // ... other peripherals
    };
};

using RESETS = Resets;

/// @brief RP2040 clock frequency constants
namespace clock_limits {
    constexpr uint32_t sys_max    = 133'000'000;  // 133 MHz (overclockable)
    constexpr uint32_t peri_max   = 133'000'000;  // Peripheral clock max
    constexpr uint32_t usb_exact  = 48'000'000;   // USB requires exactly 48 MHz
    constexpr uint32_t adc_exact  = 48'000'000;   // ADC requires exactly 48 MHz
    constexpr uint32_t xosc_freq  = 12'000'000;   // Typical crystal frequency
}

/// @brief Common PLL configurations for RP2040
namespace pll_config {
    /// PLL_SYS: 12 MHz → 133 MHz
    /// VCO = 12 MHz / 1 * 133 = 1596 MHz
    /// Output = 1596 MHz / (6 * 2) = 133 MHz
    constexpr uint32_t sys_refdiv  = 1;
    constexpr uint32_t sys_fbdiv   = 133;
    constexpr uint32_t sys_postdiv1 = 6;
    constexpr uint32_t sys_postdiv2 = 2;

    /// PLL_USB: 12 MHz → 48 MHz
    /// VCO = 12 MHz / 1 * 100 = 1200 MHz
    /// Output = 1200 MHz / (5 * 5) = 48 MHz
    constexpr uint32_t usb_refdiv  = 1;
    constexpr uint32_t usb_fbdiv   = 100;
    constexpr uint32_t usb_postdiv1 = 5;
    constexpr uint32_t usb_postdiv2 = 5;
}

} // namespace umi::pal::rp2040
```

### 4.3 ESP32-S3 クロック

```cpp
// pal/mcu/esp32s3/clocks.hh (概要)
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::esp32s3 {

namespace mm = umi::mmio;

/// @brief CPU clock source
enum class CpuClkSource : uint8_t {
    XTAL   = 0,   // 40 MHz crystal
    PLL    = 1,   // CPLL output (480 MHz / divider)
    RC8M   = 2,   // 8 MHz RC oscillator
    XTAL32 = 3,   // 32 kHz crystal
};

/// @brief SYSTEM peripheral — clock and reset control for ESP32-S3
struct System : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x600C'0000;

    /// @brief CPU peripheral clock enable
    struct PERIP_CLK_EN0 : mm::Register<System, 0x0018, 32, mm::RW, 0xF9C1'E06F> {
        struct TIMERS_CLK_EN   : mm::Field<PERIP_CLK_EN0, 0,  1> {};
        struct SPI01_CLK_EN    : mm::Field<PERIP_CLK_EN0, 1,  1> {};
        struct UART_CLK_EN     : mm::Field<PERIP_CLK_EN0, 2,  1> {};
        struct UART1_CLK_EN    : mm::Field<PERIP_CLK_EN0, 5,  1> {};
        struct SPI2_CLK_EN     : mm::Field<PERIP_CLK_EN0, 6,  1> {};
        struct I2C_EXT0_CLK_EN : mm::Field<PERIP_CLK_EN0, 7,  1> {};
        struct LEDC_CLK_EN     : mm::Field<PERIP_CLK_EN0, 11, 1> {};
        struct UART2_CLK_EN    : mm::Field<PERIP_CLK_EN0, 23, 1> {};
        struct USB_CLK_EN      : mm::Field<PERIP_CLK_EN0, 24, 1> {};
    };

    /// @brief CPU peripheral reset
    struct PERIP_RST_EN0 : mm::Register<System, 0x0020, 32> {
        struct TIMERS_RST      : mm::Field<PERIP_RST_EN0, 0,  1> {};
        struct SPI01_RST       : mm::Field<PERIP_RST_EN0, 1,  1> {};
        struct UART_RST        : mm::Field<PERIP_RST_EN0, 2,  1> {};
        struct UART1_RST       : mm::Field<PERIP_RST_EN0, 5,  1> {};
        struct SPI2_RST        : mm::Field<PERIP_RST_EN0, 6,  1> {};
        struct I2C_EXT0_RST    : mm::Field<PERIP_RST_EN0, 7,  1> {};
    };

    /// @brief CPU clock configuration
    struct SYSCLK_CONF : mm::Register<System, 0x0058, 32, mm::RW, 0x01> {
        struct PRE_DIV_CNT  : mm::Field<SYSCLK_CONF, 0,  10> {};  // CPU clock divider
        struct SOC_CLK_SEL  : mm::Field<SYSCLK_CONF, 10, 2> {};   // Clock source
        struct CLK_XTAL_FREQ : mm::Field<SYSCLK_CONF, 12, 7, mm::RO> {};
    };
};

using SYSTEM = System;

/// @brief ESP32-S3 clock frequency constants
namespace clock_limits {
    constexpr uint32_t cpu_max     = 240'000'000;  // 240 MHz
    constexpr uint32_t apb_max     = 80'000'000;   // 80 MHz (CPU/3 or CPU/2)
    constexpr uint32_t xtal_freq   = 40'000'000;   // 40 MHz crystal
    constexpr uint32_t cpll_freq   = 480'000'000;  // CPLL output
    constexpr uint32_t usb_exact   = 48'000'000;   // USB requires 48 MHz
}

} // namespace umi::pal::esp32s3
```

### 4.4 i.MX RT1060 CCM

```cpp
// pal/mcu/imxrt1060/ccm.hh (概要)
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::imxrt1060 {

namespace mm = umi::mmio;

/// @brief i.MX RT1060 CCM (Clock Controller Module)
/// i.MX RT のクロックツリーは非常に複雑で、7 つの PLL と多段分周器を持つ
struct Ccm : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x400F'C000;

    /// @brief CCM Control Register
    struct CCR : mm::Register<Ccm, 0x00, 32, mm::RW, 0x0401'107F> {
        struct OSCNT    : mm::Field<CCR, 0, 8> {};   // Oscillator ready counter
        struct COSC_EN  : mm::Field<CCR, 12, 1> {};  // On-chip oscillator enable
        struct REG_BYPASS_COUNT : mm::Field<CCR, 21, 6> {};
    };

    /// @brief CCM Status Register
    struct CSR : mm::Register<Ccm, 0x04, 32, mm::RO, 0x10> {
        struct REF_EN_B : mm::Field<CSR, 0, 1, mm::RO> {};
        struct COSC_READY : mm::Field<CSR, 5, 1, mm::RO> {};
    };

    /// @brief CCM Clock Switcher Register
    struct CCSR : mm::Register<Ccm, 0x0C, 32, mm::RW, 0x0100> {
        struct PLL3_SW_CLK_SEL : mm::Field<CCSR, 0, 1> {};
    };

    /// @brief CCM Bus Clock Divider Register
    struct CBCDR : mm::Register<Ccm, 0x14, 32, mm::RW, 0x000A'8000> {
        struct IPG_PODF      : mm::Field<CBCDR, 8,  2> {};   // IPG clock divider (1-4)
        struct AHB_PODF      : mm::Field<CBCDR, 10, 3> {};   // AHB clock divider (1-8)
        struct SEMC_PODF     : mm::Field<CBCDR, 16, 3> {};   // SEMC clock divider
        struct PERIPH_CLK_SEL : mm::Field<CBCDR, 25, 1> {};  // Peripheral clock source
    };

    /// @brief CCM Bus Clock Divider Register 2
    struct CBCMR : mm::Register<Ccm, 0x18, 32, mm::RW, 0x2DA8'B240> {
        struct LPSPI_CLK_SEL  : mm::Field<CBCMR, 4,  2> {};
        struct PERIPH_CLK2_SEL : mm::Field<CBCMR, 12, 2> {};
        struct PRE_PERIPH_CLK_SEL : mm::Field<CBCMR, 18, 2> {};
        struct LPSPI_PODF     : mm::Field<CBCMR, 26, 3> {};
    };

    /// @brief UART clock divider
    struct CSCDR1 : mm::Register<Ccm, 0x24, 32, mm::RW, 0x0649'0B00> {
        struct UART_CLK_PODF  : mm::Field<CSCDR1, 0, 6> {};   // UART divider (1-64)
        struct UART_CLK_SEL   : mm::Field<CSCDR1, 6, 1> {};   // 0=pll3_80m, 1=osc_clk
    };

    // ─────────────────────────────────────────────────────────
    // Clock Gating Registers (CCGR0-CCGR7)
    // 各 2 ビット: 00=OFF, 01=RUN only, 11=ALWAYS ON
    // ─────────────────────────────────────────────────────────

    /// @brief Clock Gating Register 0 — GPT2, PIT, etc.
    struct CCGR0 : mm::Register<Ccm, 0x68, 32, mm::RW, 0xFFFF'FFFF> {
        struct CG0  : mm::Field<CCGR0, 0,  2> {};  // AIPS_TZ1
        struct CG1  : mm::Field<CCGR0, 2,  2> {};  // AIPS_TZ2
        struct CG5  : mm::Field<CCGR0, 10, 2> {};  // GPT2 bus
        struct CG6  : mm::Field<CCGR0, 12, 2> {};  // GPT2 serial
        struct CG14 : mm::Field<CCGR0, 28, 2> {};  // TRACE
    };

    /// @brief Clock Gating Register 1 — LPUART, GPT, etc.
    struct CCGR1 : mm::Register<Ccm, 0x6C, 32, mm::RW, 0xFFFF'FFFF> {
        struct CG0  : mm::Field<CCGR1, 0,  2> {};  // LPSPI1
        struct CG1  : mm::Field<CCGR1, 2,  2> {};  // LPSPI2
        struct CG2  : mm::Field<CCGR1, 4,  2> {};  // LPSPI3
        struct CG3  : mm::Field<CCGR1, 6,  2> {};  // LPSPI4
        struct CG10 : mm::Field<CCGR1, 20, 2> {};  // GPT1 bus
        struct CG11 : mm::Field<CCGR1, 22, 2> {};  // GPT1 serial
        struct CG12 : mm::Field<CCGR1, 24, 2> {};  // LPUART1
        struct CG13 : mm::Field<CCGR1, 26, 2> {};  // LPUART2 (Note: not sequential)
    };

    /// @brief Clock Gating Register 2 — GPIO, LPI2C, etc.
    struct CCGR2 : mm::Register<Ccm, 0x70, 32, mm::RW, 0xFFFF'FFFF> {
        struct CG6  : mm::Field<CCGR2, 12, 2> {};  // LPI2C1
        struct CG7  : mm::Field<CCGR2, 14, 2> {};  // LPI2C2
        struct CG8  : mm::Field<CCGR2, 16, 2> {};  // LPI2C3
    };

    /// @brief Clock Gating Register 5 — GPIO, DMA, etc.
    struct CCGR5 : mm::Register<Ccm, 0x7C, 32, mm::RW, 0xFFFF'FFFF> {
        struct CG3  : mm::Field<CCGR5, 6,  2> {};  // LPUART7
        struct CG12 : mm::Field<CCGR5, 24, 2> {};  // GPIO1
        struct CG13 : mm::Field<CCGR5, 26, 2> {};  // GPIO2
    };
};

using CCM = Ccm;

/// @brief CCM Analog — PLL control registers
struct CcmAnalog : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x400D'8000;

    /// @brief ARM PLL (PLL1) — generates core clock
    struct PLL_ARM : mm::Register<CcmAnalog, 0x00, 32, mm::RW, 0x0001'3063> {
        struct DIV_SELECT : mm::Field<PLL_ARM, 0, 7> {};     // Loop divider (54-108)
        struct POWERDOWN  : mm::Field<PLL_ARM, 12, 1> {};    // Power down
        struct ENABLE     : mm::Field<PLL_ARM, 13, 1> {};    // Enable
        struct BYPASS_CLK_SRC : mm::Field<PLL_ARM, 14, 2> {};
        struct BYPASS     : mm::Field<PLL_ARM, 16, 1> {};    // Bypass
        struct LOCK       : mm::Field<PLL_ARM, 31, 1, mm::RO> {};  // Lock indicator
    };

    /// @brief USB1 PLL (PLL3) — 480 MHz for USB
    struct PLL_USB1 : mm::Register<CcmAnalog, 0x10, 32, mm::RW, 0x0001'2000> {
        struct DIV_SELECT : mm::Field<PLL_USB1, 1, 1> {};    // 0=480MHz, 1=528MHz
        struct EN_USB_CLKS : mm::Field<PLL_USB1, 6, 1> {};
        struct POWER     : mm::Field<PLL_USB1, 12, 1> {};
        struct ENABLE    : mm::Field<PLL_USB1, 13, 1> {};
        struct BYPASS    : mm::Field<PLL_USB1, 16, 1> {};
        struct LOCK      : mm::Field<PLL_USB1, 31, 1, mm::RO> {};
    };

    /// @brief System PLL (PLL2) — 528 MHz
    struct PLL_SYS : mm::Register<CcmAnalog, 0x30, 32, mm::RW, 0x0001'3001> {
        struct DIV_SELECT : mm::Field<PLL_SYS, 0, 1> {};    // 0=528MHz, 1=480MHz (nota bene)
        struct POWERDOWN  : mm::Field<PLL_SYS, 12, 1> {};
        struct ENABLE     : mm::Field<PLL_SYS, 13, 1> {};
        struct BYPASS     : mm::Field<PLL_SYS, 16, 1> {};
        struct LOCK       : mm::Field<PLL_SYS, 31, 1, mm::RO> {};
    };

    /// @brief Audio PLL (PLL4) — for SAI/I2S
    struct PLL_AUDIO : mm::Register<CcmAnalog, 0x70, 32, mm::RW, 0x0001'1006> {
        struct DIV_SELECT   : mm::Field<PLL_AUDIO, 0,  7> {};
        struct POWERDOWN    : mm::Field<PLL_AUDIO, 12, 1> {};
        struct ENABLE       : mm::Field<PLL_AUDIO, 13, 1> {};
        struct BYPASS       : mm::Field<PLL_AUDIO, 16, 1> {};
        struct POST_DIV_SELECT : mm::Field<PLL_AUDIO, 19, 2> {};
        struct LOCK         : mm::Field<PLL_AUDIO, 31, 1, mm::RO> {};
    };
};

using CCM_ANALOG = CcmAnalog;

/// @brief i.MX RT1060 clock frequency constants
namespace clock_limits {
    constexpr uint32_t arm_max     = 600'000'000;  // 600 MHz (Cortex-M7)
    constexpr uint32_t ahb_max     = 600'000'000;
    constexpr uint32_t ipg_max     = 150'000'000;  // 150 MHz (peripheral bus)
    constexpr uint32_t semc_max    = 166'000'000;  // SDRAM controller
    constexpr uint32_t osc_freq    = 24'000'000;   // 24 MHz crystal
    constexpr uint32_t usb_pll     = 480'000'000;  // USB PLL output
}

} // namespace umi::pal::imxrt1060
```

---

## 5. ペリフェラルクロックイネーブルの抽象化パターン

各ベンダーのペリフェラルクロック制御は根本的に異なるため、完全な統一抽象化は困難だが、
共通パターンを識別できる:

### 5.1 ベンダー間の制御方式比較

| 操作 | STM32 | RP2040 | ESP32-S3 | i.MX RT |
|------|-------|--------|----------|---------|
| **クロック有効化** | RCC_xxxENR にビットセット | RESETS でリセット解除 | PCR_xxx にビットセット | CCGR に 2bit 値書込 (0b11) |
| **クロック無効化** | RCC_xxxENR にビットクリア | RESETS でリセットアサート | PCR_xxx にビットクリア | CCGR に 2bit 値書込 (0b00) |
| **リセット** | RCC_xxxRSTR にビットセット→クリア | RESETS にビットセット→クリア | PCR_xxx_RST | SRC レジスタ |
| **制御粒度** | 1bit (ON/OFF) | 1bit (RESET/RELEASE) | 1bit (ON/OFF) | 2bit (OFF/RUN/ALWAYS) |
| **レジスタ数** | AHB1ENR, APB1ENR, APB2ENR 等 | RESET (1レジスタ) | PERIP_CLK_EN0/1 | CCGR0-CCGR7 |

### 5.2 生成戦略

PAL レベルでは、各ベンダーのレジスタをそのまま型安全に表現する（抽象化しない）。
抽象化は上位の HAL レイヤで行う:

```
PAL (本カテゴリ)          HAL (上位レイヤ)
─────────────          ──────────────
STM32: RCC_AHB1ENR      ┌──────────────────┐
RP2040: RESETS           │ peripheral_clock  │
ESP32: PCR               │   ::enable()      │
i.MX RT: CCGR            │   ::disable()     │
                         │   ::reset()       │
ベンダー固有レジスタ      └──────────────────┘
型安全だが低レベル        ベンダー抽象化 API
```

---

## 6. データソース

| データソース | ベンダー | 取得方法 | カバー範囲 |
|------------|---------|---------|-----------|
| **SVD ファイル** | 全ベンダー | CMSIS-Pack / SDK | RCC/CLOCKS/CCM のレジスタ定義 |
| **STM32 リファレンスマニュアル** | ST | PDF (RM0090 等) | クロックツリー図、PLL 制約、バス最大周波数 |
| **RP2040 データシート** | Raspberry Pi | PDF Chapter 2.15-2.18 | PLL, CLOCKS, RESETS, XOSC レジスタ |
| **ESP32-S3 TRM** | Espressif | PDF Chapter 7 | クロックソース、PLL、ペリフェラルクロック制御 |
| **i.MX RT1060 RM** | NXP | PDF Chapter 14 (CCM) | CCM, CCM_ANALOG, クロックツリー全体 |
| **CubeMX / MCUXpresso** | ST / NXP | GUI tool | クロック設定の検証・可視化 |
| **RP2040 pico-sdk** | Raspberry Pi | ソースコード | PLL 設定ヘルパ関数の参照実装 |
