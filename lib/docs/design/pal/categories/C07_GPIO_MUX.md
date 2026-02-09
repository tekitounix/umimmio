# C7: GPIO ピンマルチプレクシング

全 13 カテゴリの中で **最もプラットフォーム差異が大きい** カテゴリ。
同じ「ピンに機能を割り当てる」という目的に対し、各ベンダーが根本的に異なるアーキテクチャを採用している。

---

## 1. 概要

GPIO ピンマルチプレクシングは、MCU の物理ピンにどのペリフェラル信号を接続するかを制御する。
この設計は MCU アーキテクチャの根幹に関わるため、ベンダー間の抽象化が最も困難な領域である。

**所属レイヤ**: L3 (MUX 方式) + L4 (ピンごとの割当テーブル)

| ベンダー | 方式 | 特徴 |
|---------|------|------|
| STM32 | **Alternate Function (AF)** | ピンごとに AF0-AF15 の 16 種から選択 |
| RP2040/RP2350 | **3 層 GPIO + FUNCSEL** | SIO / IO_BANK0 / PADS の 3 層構成。F1-F9 から選択 |
| ESP32-S3/P4 | **GPIO Matrix** | 入出力信号を任意の GPIO に自由にルーティング |
| i.MX RT | **IOMUXC** | 専用マルチプレクサコントローラ + Daisy Chain |

---

## 2. STM32: Alternate Function (AF) 方式

### 2.1 アーキテクチャ

STM32 の GPIO ピンマルチプレクシングは、各ピンの AFRL/AFRH レジスタで 4 ビットの AF 番号を設定する方式。
MODER を `ALTERNATE (0b10)` に設定した上で、AF 番号を書き込む。

```
                    MODER = ALTERNATE (0b10)
                         │
    ┌────────────────────┼────────────────────┐
    │  GPIO ピン (例: PA9)                      │
    │                    │                      │
    │  ┌─────────────────▼──────────────────┐  │
    │  │         AFR (4-bit MUX)            │  │
    │  │                                    │  │
    │  │  AF0 ─── MCO1                      │  │
    │  │  AF1 ─── TIM1_CH2                  │  │
    │  │  AF2 ─── (reserved)                │  │
    │  │  AF3 ─── (reserved)                │  │
    │  │  AF4 ─── I2C3_SMBA                 │  │
    │  │  AF5 ─── SPI2_MISO                 │  │
    │  │  AF6 ─── (reserved)                │  │
    │  │  AF7 ─── USART1_TX     ◄── 選択   │  │
    │  │  AF8-AF15 ─── ...                  │  │
    │  └────────────────────────────────────┘  │
    └──────────────────────────────────────────┘
```

### 2.2 設定に必要な定義

| 定義 | レジスタ/データ | カテゴリ |
|------|--------------|---------|
| ピンモード設定 | GPIOx_MODER | C5 (ペリフェラルレジスタ) |
| AF 番号設定 | GPIOx_AFRL / GPIOx_AFRH | C5 (ペリフェラルレジスタ) |
| ピン-AF-信号マッピング | データシート AF テーブル | **C6 (本カテゴリ)** |
| 電気特性設定 | GPIOx_OTYPER / OSPEEDR / PUPDR | C5 (ペリフェラルレジスタ) |

### 2.3 AF テーブルの特性

- **パッケージ依存**: LQFP100 と LQFP144 では利用可能なピンと AF が異なる
- **ファミリ内差異**: STM32F405 と STM32F407 で Ethernet AF の有無が異なる
- **データソース**: CubeMX DB (Open Pin Data)、データシートの AF テーブル

---

## 3. RP2040/RP2350: 3 層 GPIO + FUNCSEL 方式

### 3.1 アーキテクチャ

RP2040 は GPIO を 3 つの独立ペリフェラルで管理する。ピン機能は IO_BANK0 の FUNCSEL フィールドで選択する。

```
┌─────────────────────────────────────────────────────────────────┐
│  GPIO ピン (例: GPIO0)                                          │
│                                                                 │
│  ┌─────────────────┐  ┌──────────────────┐  ┌───────────────┐  │
│  │ PADS_BANK0      │  │ IO_BANK0         │  │ SIO           │  │
│  │ (電気特性)      │  │ (機能選択)       │  │ (高速 I/O)    │  │
│  │                 │  │                  │  │               │  │
│  │ ・Drive (2-12mA)│  │ FUNCSEL (5-bit): │  │ F5 = SIO:     │  │
│  │ ・Pull-up/down  │  │  F1 = SPI        │  │  GPIO_OUT     │  │
│  │ ・Schmitt       │  │  F2 = UART       │  │  GPIO_OE      │  │
│  │ ・Slew rate     │  │  F3 = I2C        │  │  GPIO_IN      │  │
│  │ ・Input enable  │  │  F4 = PWM        │  │ (1 サイクル)  │  │
│  │                 │  │  F5 = SIO ────────┼──┤               │  │
│  │                 │  │  F6 = PIO0       │  │               │  │
│  │                 │  │  F7 = PIO1       │  │               │  │
│  │                 │  │  F9 = USB        │  │               │  │
│  │                 │  │  0x1F = NULL     │  │               │  │
│  └─────────────────┘  └──────────────────┘  └───────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 STM32 との比較

| 側面 | STM32 AF 方式 | RP2040 FUNCSEL 方式 |
|------|-------------|-------------------|
| 選択肢数 | 16 (AF0-AF15) | 最大 10 (F0-F9, 0x1F) |
| 選択粒度 | ピンごとに個別の信号 | ピンごとに機能ブロックを選択 |
| 電気特性 | 同じ GPIO レジスタ内 | PADS_BANK0 で独立管理 |
| 入出力操作 | ODR/IDR (通常バス) | SIO (シングルサイクル) |
| 信号の固定度 | AF 番号は固定 | 機能番号は固定、信号はピン依存 |

### 3.3 RP2350 の拡張

RP2350 は RP2040 の FUNCSEL 方式を拡張:
- **Cortex-M33 / Hazard3 (RISC-V) デュアル ISA**: コアに関わらず GPIO 方式は同一
- **F10 = UART**: UART 専用機能が追加
- **IO_BANK0/PADS_BANK0 に加え IO_BANK1/PADS_BANK1**: GPIO 数の拡張 (GPIO0-47)
- **セキュリティ属性**: TrustZone 対応パッドにセキュア/ノンセキュア属性

---

## 4. ESP32-S3/P4: GPIO Matrix 方式

### 4.1 アーキテクチャ

ESP32 シリーズは「GPIO Matrix」と呼ばれる完全クロスバースイッチを採用。
**任意のペリフェラル信号を任意の GPIO にルーティング** できる（一部の専用ピンを除く）。

```
┌─────────────────────────────────────────────────────────────┐
│                     GPIO Matrix                             │
│                                                             │
│  ペリフェラル出力          クロスバー         GPIO ピン     │
│  ┌─────────────┐      ┌───────────────┐   ┌──────────┐    │
│  │ UART0_TXD   │──────┤               ├───│ GPIO1    │    │
│  │ UART1_TXD   │──────┤   出力信号    ├───│ GPIO2    │    │
│  │ SPI2_CLK    │──────┤   ルーティング ├───│ GPIO3    │    │
│  │ I2C0_SDA    │──────┤   (128信号)   ├───│ ...      │    │
│  │ ...         │──────┤               ├───│ GPIO48   │    │
│  └─────────────┘      └───────────────┘   └──────────┘    │
│                                                             │
│  GPIO ピン             クロスバー         ペリフェラル入力  │
│  ┌──────────┐      ┌───────────────┐   ┌─────────────┐    │
│  │ GPIO1    │──────┤               ├───│ UART0_RXD   │    │
│  │ GPIO2    │──────┤   入力信号    ├───│ UART1_RXD   │    │
│  │ GPIO3    │──────┤   ルーティング ├───│ SPI2_MISO   │    │
│  │ ...      │──────┤   (128信号)   ├───│ I2C0_SDA    │    │
│  │ GPIO48   │──────┤               ├───│ ...         │    │
│  └──────────┘      └───────────────┘   └─────────────┘    │
│                                                             │
│  ※ 一部ピンは専用 (USB D+/D-, JTAG 等)                     │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 IO MUX との関係

ESP32-S3 には GPIO Matrix に加えて **IO MUX** がある。IO MUX はバイパスパスで、
GPIO Matrix を経由せず直接接続するため低レイテンシが実現できる。
高速ペリフェラル (SPI, I2S 等) では IO MUX を使用することが推奨される。

### 4.3 ESP32-P4 の違い

ESP32-P4 は HP (High Performance) と LP (Low Power) の 2 つのサブシステムを持ち、
それぞれ独立した GPIO Matrix と IO MUX を持つ:

| サブシステム | コア | GPIO 範囲 | 用途 |
|------------|------|-----------|------|
| HP | RISC-V (デュアルコア, 400MHz) | GPIO0-GPIO54 | メイン処理 |
| LP | RISC-V (シングルコア, 40MHz) | LP_GPIO0-LP_GPIO15 | 低電力スリープ中の I/O |

---

## 5. i.MX RT: IOMUXC 方式

### 5.1 アーキテクチャ

i.MX RT は GPIO ピンの機能選択を **IOMUXC (IO Multiplexer Controller)** という
専用ペリフェラルで管理する。GPIO ペリフェラルとは完全に独立している。

```
┌─────────────────────────────────────────────────────────────────┐
│  パッド (例: GPIO_AD_B0_09)                                     │
│                                                                 │
│  ┌──────────────────────────┐    ┌────────────────────────────┐ │
│  │ IOMUXC                   │    │ GPIO1                      │ │
│  │ (IO マルチプレクサ)       │    │ (データレジスタ)           │ │
│  │                          │    │                            │ │
│  │ SW_MUX_CTL_PAD:          │    │ ALT5 選択時:               │ │
│  │  ALT0 = GPIO1_IO09       │    │  DR   (Data)               │ │
│  │  ALT1 = FLEXPWM2_PWMA3   │    │  GDIR (Direction)          │ │
│  │  ALT2 = LPUART1_TXD ◄───┼────┤  PSR  (Pad Status)         │ │
│  │  ALT3 = SAI1_MCLK        │    │  ICR  (Interrupt Config)   │ │
│  │  ALT5 = GPIO1_IO09       │    │  IMR  (Interrupt Mask)     │ │
│  │                          │    │  ISR  (Interrupt Status)   │ │
│  │ SW_PAD_CTL_PAD:          │    └────────────────────────────┘ │
│  │  SRE, DSE, SPEED         │                                  │
│  │  ODE, PKE, PUE, PUS      │    ┌────────────────────────────┐ │
│  │  HYS                     │    │ Daisy Chain                │ │
│  │                          │    │ (入力パス選択)             │ │
│  │                          │    │                            │ │
│  │                          │    │ 同じ信号が複数パッドに     │ │
│  │                          │    │ 割当可能 → どのパッドから  │ │
│  │                          │    │ 入力するか SELECT_INPUT    │ │
│  │                          │    │ レジスタで指定             │ │
│  └──────────────────────────┘    └────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

### 5.2 Daisy Chain (Input Path Selection)

i.MX RT 固有の概念。同じペリフェラル入力信号 (例: LPUART1_RXD) を複数のパッドに
ALT 設定できるが、実際にどのパッドから入力を受けるかは **SELECT_INPUT** レジスタで決定する。

```
GPIO_AD_B0_09 ─── ALT2 ─── LPUART1_TXD (出力: MUX で決定)
GPIO_AD_B0_10 ─── ALT2 ─── LPUART1_RXD ──┐
GPIO_B1_01    ─── ALT2 ─── LPUART1_RXD ──┤── SELECT_INPUT で選択
                                           │   (0 = AD_B0_10, 1 = B1_01)
                                           └── → ペリフェラル入力
```

---

## 6. GPIO マルチプレクシング比較まとめ

| 側面 | STM32 AF | RP2040 FUNCSEL | ESP32-S3 Matrix | i.MX RT IOMUXC |
|------|----------|----------------|-----------------|----------------|
| **選択メカニズム** | AFR レジスタ (4bit/pin) | FUNCSEL (5bit/pin) | GPIO_FUNCx_OUT_SEL / IN_SEL | SW_MUX_CTL_PAD (3bit) |
| **選択肢数** | 16 (AF0-AF15) | ~10 (F0-F9) | 128+ 信号番号 | 8 (ALT0-ALT7) |
| **ルーティング自由度** | 固定 (ピンごとに決定) | 固定 (ピンごとに決定) | **自由** (any-to-any) | 固定 (パッドごとに決定) |
| **入力パス選択** | なし (1:1) | なし (1:1) | 入力信号ごとに GPIO 選択 | **Daisy Chain** (SELECT_INPUT) |
| **電気特性設定** | GPIO レジスタ内 | PADS_BANK0 (別 Device) | IO MUX レジスタ | SW_PAD_CTL_PAD (IOMUXC 内) |
| **MUX コントローラ** | GPIO に統合 | IO_BANK0 (別 Device) | GPIO Matrix (別 Device) | **IOMUXC** (完全に独立) |
| **データソース** | CubeMX DB / データシート | RP2040 データシート Table 2 | ESP32-S3 TRM Chapter 6 | i.MX RT1060 RM Chapter 10 |
| **生成難易度** | 中 (AF テーブルが大量) | 低 (規則的) | 高 (信号番号テーブル大量) | 高 (MUX + PAD + Daisy 3 種) |

---

## 7. 生成ヘッダのコード例

### 7.1 STM32 AF テーブル

```cpp
// pal/device/stm32f407vg/gpio_af.hh
// L4: デバイスバリアント固有 — パッケージにより利用可能なピン/AF が異なる
#pragma once
#include <cstdint>

namespace umi::pal::stm32f407::gpio_af {

/// @brief Alternate Function number
enum class Af : uint8_t {
    AF0  = 0,  AF1  = 1,  AF2  = 2,  AF3  = 3,
    AF4  = 4,  AF5  = 5,  AF6  = 6,  AF7  = 7,
    AF8  = 8,  AF9  = 9,  AF10 = 10, AF11 = 11,
    AF12 = 12, AF13 = 13, AF14 = 14, AF15 = 15,
};

/// @brief Pin-AF-Signal mapping entry (generated from CubeMX / Open Pin Data)
struct PinAfEntry {
    Af af;
    const char* signal;
};

// ─────────────────────────────────────────────────────────────
// PA9 AF mappings
// ─────────────────────────────────────────────────────────────
constexpr PinAfEntry pa9_af[] = {
    {Af::AF0,  "MCO1"},
    {Af::AF1,  "TIM1_CH2"},
    {Af::AF4,  "I2C3_SMBA"},
    {Af::AF5,  "SPI2_MISO"},      // Note: not available on all packages
    {Af::AF7,  "USART1_TX"},
    {Af::AF11, "OTG_FS_VBUS"},
    {Af::AF14, "DCMI_D0"},
};

// ─────────────────────────────────────────────────────────────
// PA5 AF mappings (SPI1_SCK / LED on Nucleo boards)
// ─────────────────────────────────────────────────────────────
constexpr PinAfEntry pa5_af[] = {
    {Af::AF0,  "MCO1"},
    {Af::AF1,  "TIM2_CH1_ETR"},
    {Af::AF5,  "SPI1_SCK"},
    {Af::AF8,  "TIM8_CH1N"},
};

// ─────────────────────────────────────────────────────────────
// Compile-time AF lookup templates
// Port: 0=A, 1=B, 2=C, ...  Pin: 0-15
// ─────────────────────────────────────────────────────────────
template<uint8_t Port, uint8_t Pin>
struct PinAf;

/// @brief PA9 — USART1_TX, TIM1_CH2, I2C3_SMBA, SPI2_MISO
template<> struct PinAf<0, 9> {
    static constexpr Af usart1_tx = Af::AF7;
    static constexpr Af tim1_ch2  = Af::AF1;
    static constexpr Af i2c3_smba = Af::AF4;
    static constexpr Af spi2_miso = Af::AF5;
    static constexpr Af otg_fs_vbus = Af::AF11;
    static constexpr Af dcmi_d0   = Af::AF14;
};

/// @brief PA5 — SPI1_SCK
template<> struct PinAf<0, 5> {
    static constexpr Af tim2_ch1  = Af::AF1;
    static constexpr Af spi1_sck  = Af::AF5;
    static constexpr Af tim8_ch1n = Af::AF8;
};

/// @brief PB6 — I2C1_SCL, USART1_TX, TIM4_CH1
template<> struct PinAf<1, 6> {
    static constexpr Af tim4_ch1  = Af::AF2;
    static constexpr Af i2c1_scl  = Af::AF4;
    static constexpr Af usart1_tx = Af::AF7;
};

/// @brief PB7 — I2C1_SDA, USART1_RX, TIM4_CH2
template<> struct PinAf<1, 7> {
    static constexpr Af tim4_ch2  = Af::AF2;
    static constexpr Af i2c1_sda  = Af::AF4;
    static constexpr Af usart1_rx = Af::AF7;
};

// ─────────────────────────────────────────────────────────────
// 設定ヘルパ
// ─────────────────────────────────────────────────────────────

/// @brief GPIO ポートとピン番号からの AF 設定用定義
/// @tparam Port GPIO ポート (0=A, 1=B, ...)
/// @tparam Pin ピン番号 (0-15)
/// @tparam AfNum AF 番号
/// @note 実際のレジスタ操作は HAL レイヤで行う
template<uint8_t Port, uint8_t Pin, Af AfNum>
struct PinAfConfig {
    static constexpr uint8_t port = Port;
    static constexpr uint8_t pin  = Pin;
    static constexpr Af af = AfNum;

    /// @brief AFRL (pin 0-7) / AFRH (pin 8-15) のどちらを使用するか
    static constexpr bool use_afrh = (Pin >= 8);

    /// @brief AFR レジスタ内のビットオフセット
    static constexpr uint8_t bit_offset = (Pin % 8) * 4;
};

} // namespace umi::pal::stm32f407::gpio_af
```

**使用例**:

```cpp
using namespace umi::pal::stm32f407::gpio_af;

// PA9 を USART1_TX に設定
constexpr auto usart1_tx_af = PinAf<0, 9>::usart1_tx;  // Af::AF7
static_assert(usart1_tx_af == Af::AF7);

// PinAfConfig でレジスタ操作パラメータを取得
using Usart1TxPin = PinAfConfig<0, 9, PinAf<0, 9>::usart1_tx>;
static_assert(Usart1TxPin::use_afrh);          // PA9 → AFRH
static_assert(Usart1TxPin::bit_offset == 4);   // (9 % 8) * 4 = 4
```

### 7.2 RP2040 FUNCSEL

```cpp
// pal/mcu/rp2040/gpio_func.hh
// L3: MCU ファミリ固有 — RP2040 の全 GPIO で共通の機能定義
#pragma once
#include <cstdint>

namespace umi::pal::rp2040::gpio_func {

/// @brief GPIO function selection values
/// Each GPIO can select one function from this table.
/// The actual signal assigned depends on the GPIO number.
enum class Func : uint8_t {
    XIP       = 0,     // F0: XIP (flash execute-in-place)
    SPI       = 1,     // F1: SPI
    UART      = 2,     // F2: UART
    I2C       = 3,     // F3: I2C
    PWM       = 4,     // F4: PWM
    SIO       = 5,     // F5: Software-controlled GPIO (via SIO)
    PIO0      = 6,     // F6: PIO block 0
    PIO1      = 7,     // F7: PIO block 1
    CLOCK     = 8,     // F8: Clock input/output
    USB       = 9,     // F9: USB
    NULL_FUNC = 0x1F,  // Disable (default reset value)
};

// ─────────────────────────────────────────────────────────────
// Per-GPIO function assignment tables
// RP2040 datasheet Table 2: "GPIO Functions"
//
// 各 GPIO の各 Func が具体的にどの信号になるかを定義
// ─────────────────────────────────────────────────────────────
template<uint8_t GpioNum>
struct GpioFunc;

/// @brief GPIO0 functions
template<> struct GpioFunc<0> {
    static constexpr Func spi0_rx   = Func::SPI;   // F1: SPI0 RX
    static constexpr Func uart0_tx  = Func::UART;  // F2: UART0 TX
    static constexpr Func i2c0_sda  = Func::I2C;   // F3: I2C0 SDA
    static constexpr Func pwm0_a    = Func::PWM;   // F4: PWM0 A
    static constexpr Func sio       = Func::SIO;   // F5: GPIO (SIO)
    static constexpr Func pio0      = Func::PIO0;  // F6: PIO0
    static constexpr Func pio1      = Func::PIO1;  // F7: PIO1
    static constexpr Func usb_ovcur = Func::USB;   // F9: USB OVCUR DET
};

/// @brief GPIO1 functions
template<> struct GpioFunc<1> {
    static constexpr Func spi0_csn  = Func::SPI;   // F1: SPI0 CSn
    static constexpr Func uart0_rx  = Func::UART;  // F2: UART0 RX
    static constexpr Func i2c0_scl  = Func::I2C;   // F3: I2C0 SCL
    static constexpr Func pwm0_b    = Func::PWM;   // F4: PWM0 B
    static constexpr Func sio       = Func::SIO;   // F5: GPIO (SIO)
    static constexpr Func pio0      = Func::PIO0;  // F6: PIO0
    static constexpr Func pio1      = Func::PIO1;  // F7: PIO1
    static constexpr Func usb_vbus  = Func::USB;   // F9: USB VBUS DET
};

/// @brief GPIO2 functions
template<> struct GpioFunc<2> {
    static constexpr Func spi0_sck  = Func::SPI;   // F1: SPI0 SCK
    static constexpr Func uart0_cts = Func::UART;  // F2: UART0 CTS
    static constexpr Func i2c1_sda  = Func::I2C;   // F3: I2C1 SDA
    static constexpr Func pwm1_a    = Func::PWM;   // F4: PWM1 A
    static constexpr Func sio       = Func::SIO;   // F5: GPIO (SIO)
    static constexpr Func pio0      = Func::PIO0;  // F6: PIO0
    static constexpr Func pio1      = Func::PIO1;  // F7: PIO1
    static constexpr Func usb_vbus_en = Func::USB; // F9: USB VBUS EN
};

/// @brief GPIO3 functions
template<> struct GpioFunc<3> {
    static constexpr Func spi0_tx   = Func::SPI;   // F1: SPI0 TX
    static constexpr Func uart0_rts = Func::UART;  // F2: UART0 RTS
    static constexpr Func i2c1_scl  = Func::I2C;   // F3: I2C1 SCL
    static constexpr Func pwm1_b    = Func::PWM;   // F4: PWM1 B
    static constexpr Func sio       = Func::SIO;   // F5: GPIO (SIO)
    static constexpr Func pio0      = Func::PIO0;  // F6: PIO0
    static constexpr Func pio1      = Func::PIO1;  // F7: PIO1
    static constexpr Func usb_ovcur = Func::USB;   // F9: USB OVCUR DET
};

/// @brief GPIO4 functions
template<> struct GpioFunc<4> {
    static constexpr Func spi0_rx   = Func::SPI;   // F1: SPI0 RX (repeats every 4 GPIOs)
    static constexpr Func uart1_tx  = Func::UART;  // F2: UART1 TX
    static constexpr Func i2c0_sda  = Func::I2C;   // F3: I2C0 SDA
    static constexpr Func pwm2_a    = Func::PWM;   // F4: PWM2 A
    static constexpr Func sio       = Func::SIO;   // F5: GPIO (SIO)
    static constexpr Func pio0      = Func::PIO0;  // F6: PIO0
    static constexpr Func pio1      = Func::PIO1;  // F7: PIO1
    static constexpr Func usb_vbus  = Func::USB;   // F9: USB VBUS DET
};

// ... GPIO5-GPIO29 follow similar patterns
// SPI: repeats every 4 GPIOs (0-3=SPI0, 4-7=SPI0, 8-11=SPI1, ...)
// UART: alternates (0-1=UART0, 4-5=UART1, 8-9=UART0, ...)
// I2C: repeats every 4 GPIOs (0-1=I2C0, 2-3=I2C1, ...)
// PWM: sequential (0=PWM0A, 1=PWM0B, 2=PWM1A, ...)

} // namespace umi::pal::rp2040::gpio_func
```

### 7.3 ESP32-S3 GPIO Matrix 信号テーブル

```cpp
// pal/mcu/esp32s3/gpio_matrix.hh
// L3: MCU ファミリ固有 — GPIO Matrix の信号番号定義
#pragma once
#include <cstdint>

namespace umi::pal::esp32s3::gpio_matrix {

// ─────────────────────────────────────────────────────────────
// ペリフェラル信号番号
// GPIO Matrix はこの番号を使って GPIO ← → ペリフェラル間を接続する
//
// 入力信号: GPIO_FUNCn_IN_SEL_CFG レジスタで GPIO 番号を指定
// 出力信号: GPIO_FUNCn_OUT_SEL_CFG レジスタで信号番号を指定
// ─────────────────────────────────────────────────────────────

/// @brief Input signal numbers (routed from GPIO pin to peripheral input)
namespace input_signal {
    constexpr uint16_t SPICLK_IN         = 0;
    constexpr uint16_t SPIQ_IN           = 1;
    constexpr uint16_t SPID_IN           = 2;
    constexpr uint16_t SPIHD_IN          = 3;
    constexpr uint16_t SPIWP_IN          = 4;
    constexpr uint16_t SPICS0_IN         = 5;
    constexpr uint16_t UART0_RXD         = 6;
    constexpr uint16_t UART0_CTS         = 7;
    constexpr uint16_t UART0_DSR         = 8;
    constexpr uint16_t UART1_RXD         = 9;
    constexpr uint16_t UART1_CTS         = 10;
    constexpr uint16_t UART1_DSR         = 11;
    constexpr uint16_t UART2_RXD         = 12;
    constexpr uint16_t UART2_CTS         = 13;
    constexpr uint16_t UART2_DSR         = 14;
    constexpr uint16_t I2C0_SDA_IN       = 17;
    constexpr uint16_t I2C0_SCL_IN       = 18;
    constexpr uint16_t I2C1_SDA_IN       = 19;
    constexpr uint16_t I2C1_SCL_IN       = 20;
    constexpr uint16_t SPI2_CLK_IN       = 64;
    constexpr uint16_t SPI2_MISO_IN      = 65;
    constexpr uint16_t SPI2_CS0_IN       = 68;
    constexpr uint16_t SPI3_CLK_IN       = 69;
    constexpr uint16_t SPI3_MISO_IN      = 70;
    constexpr uint16_t SPI3_CS0_IN       = 73;
    constexpr uint16_t I2S0_I_SD_IN      = 46;
    constexpr uint16_t I2S0_I_BCK_IN     = 47;
    constexpr uint16_t I2S0_I_WS_IN      = 48;
    constexpr uint16_t I2S1_I_SD_IN      = 49;
    constexpr uint16_t I2S1_I_BCK_IN     = 50;
    constexpr uint16_t I2S1_I_WS_IN      = 51;
    // ... total ~128 input signals
}  // namespace input_signal

/// @brief Output signal numbers (routed from peripheral output to GPIO pin)
namespace output_signal {
    constexpr uint16_t SPICLK_OUT        = 0;
    constexpr uint16_t SPIQ_OUT          = 1;
    constexpr uint16_t SPID_OUT          = 2;
    constexpr uint16_t SPIHD_OUT         = 3;
    constexpr uint16_t SPIWP_OUT         = 4;
    constexpr uint16_t SPICS0_OUT        = 5;
    constexpr uint16_t UART0_TXD         = 6;
    constexpr uint16_t UART0_RTS         = 7;
    constexpr uint16_t UART0_DTR         = 8;
    constexpr uint16_t UART1_TXD         = 9;
    constexpr uint16_t UART1_RTS         = 10;
    constexpr uint16_t UART1_DTR         = 11;
    constexpr uint16_t UART2_TXD         = 12;
    constexpr uint16_t UART2_RTS         = 13;
    constexpr uint16_t UART2_DTR         = 14;
    constexpr uint16_t I2C0_SDA_OUT      = 17;
    constexpr uint16_t I2C0_SCL_OUT      = 18;
    constexpr uint16_t I2C1_SDA_OUT      = 19;
    constexpr uint16_t I2C1_SCL_OUT      = 20;
    constexpr uint16_t SPI2_CLK_OUT      = 64;
    constexpr uint16_t SPI2_MOSI_OUT     = 65;
    constexpr uint16_t SPI2_CS0_OUT      = 68;
    constexpr uint16_t SPI3_CLK_OUT      = 69;
    constexpr uint16_t SPI3_MOSI_OUT     = 70;
    constexpr uint16_t SPI3_CS0_OUT      = 73;
    constexpr uint16_t I2S0_O_SD_OUT     = 46;
    constexpr uint16_t I2S0_O_BCK_OUT    = 47;
    constexpr uint16_t I2S0_O_WS_OUT     = 48;
    constexpr uint16_t I2S1_O_SD_OUT     = 49;
    constexpr uint16_t I2S1_O_BCK_OUT    = 50;
    constexpr uint16_t I2S1_O_WS_OUT     = 51;
    // ... total ~128 output signals
}  // namespace output_signal

/// @brief Dedicated GPIO pins (fixed function, cannot use GPIO Matrix)
namespace dedicated {
    constexpr uint8_t USB_DM     = 19;   // USB D-
    constexpr uint8_t USB_DP     = 20;   // USB D+
    constexpr uint8_t JTAG_TCK   = 39;   // JTAG TCK
    constexpr uint8_t JTAG_TMS   = 42;   // JTAG TMS
    constexpr uint8_t JTAG_TDI   = 41;   // JTAG TDI
    constexpr uint8_t JTAG_TDO   = 40;   // JTAG TDO
    constexpr uint8_t SPI0_CS0   = 26;   // SPI0 flash CS
    constexpr uint8_t SPI0_CLK   = 30;   // SPI0 flash CLK
    constexpr uint8_t SPI0_D     = 31;   // SPI0 flash D0
    constexpr uint8_t SPI0_Q     = 32;   // SPI0 flash D1
    constexpr uint8_t SPI0_WP    = 28;   // SPI0 flash D2
    constexpr uint8_t SPI0_HD    = 27;   // SPI0 flash D3
}  // namespace dedicated

/// @brief GPIO Matrix routing configuration
/// @note このデータは ESP-IDF の soc/esp32s3/gpio_sig_map.h に相当
struct MatrixRoute {
    uint16_t signal_id;
    uint8_t  gpio_num;
    bool     invert;       // 信号の反転
    bool     enable;       // ルーティング有効化
};

} // namespace umi::pal::esp32s3::gpio_matrix
```

### 7.4 i.MX RT IOMUXC

```cpp
// pal/mcu/imxrt1060/iomuxc.hh
// L3: MCU ファミリ固有 — IOMUXC マルチプレクサ定義
#pragma once
#include <cstdint>
#include <umimmio/register.hh>

namespace umi::pal::imxrt1060 {

namespace mm = umi::mmio;

/// @brief ALT function selection (3-bit, up to 8 alternatives)
enum class Alt : uint8_t {
    ALT0 = 0, ALT1 = 1, ALT2 = 2, ALT3 = 3,
    ALT4 = 4, ALT5 = 5, ALT6 = 6, ALT7 = 7,
};

/// @brief IOMUXC pad configuration fields
struct PadConfig {
    uint8_t sre;    // Slew Rate: 0=slow, 1=fast
    uint8_t dse;    // Drive Strength: 0=disabled, 1-7 = R0/x to R0/7
    uint8_t speed;  // Speed: 0=50MHz, 1=100MHz, 2=150MHz, 3=200MHz
    uint8_t ode;    // Open Drain: 0=disabled, 1=enabled
    uint8_t pke;    // Pull/Keep Enable
    uint8_t pue;    // Pull/Keep Select: 0=keeper, 1=pull
    uint8_t pus;    // Pull Up/Down: 0=100K down, 1=47K up, 2=100K up, 3=22K up
    uint8_t hys;    // Hysteresis: 0=disabled, 1=enabled
};

/// @brief Pad-ALT-Signal mapping entry
struct PadAltEntry {
    Alt alt;
    const char* signal;
};

// ─────────────────────────────────────────────────────────────
// IOMUXC レジスタ定義 (umimmio 型)
// ─────────────────────────────────────────────────────────────
struct Iomuxc : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x401F'8000;

    // SW_MUX_CTL_PAD registers — function selection
    struct SW_MUX_CTL_PAD_GPIO_AD_B0_09
        : mm::Register<Iomuxc, 0x00E4, 32, mm::RW, 0x05> {
        struct MUX_MODE : mm::Field<SW_MUX_CTL_PAD_GPIO_AD_B0_09, 0, 3> {};
        struct SION     : mm::Field<SW_MUX_CTL_PAD_GPIO_AD_B0_09, 4, 1> {};
    };

    // SW_PAD_CTL_PAD registers — electrical characteristics
    struct SW_PAD_CTL_PAD_GPIO_AD_B0_09
        : mm::Register<Iomuxc, 0x02D4, 32, mm::RW, 0x10B0> {
        struct SRE   : mm::Field<SW_PAD_CTL_PAD_GPIO_AD_B0_09, 0,  1> {};
        struct DSE    : mm::Field<SW_PAD_CTL_PAD_GPIO_AD_B0_09, 3,  3> {};
        struct SPEED : mm::Field<SW_PAD_CTL_PAD_GPIO_AD_B0_09, 6,  2> {};
        struct ODE   : mm::Field<SW_PAD_CTL_PAD_GPIO_AD_B0_09, 11, 1> {};
        struct PKE   : mm::Field<SW_PAD_CTL_PAD_GPIO_AD_B0_09, 12, 1> {};
        struct PUE   : mm::Field<SW_PAD_CTL_PAD_GPIO_AD_B0_09, 13, 1> {};
        struct PUS   : mm::Field<SW_PAD_CTL_PAD_GPIO_AD_B0_09, 14, 2> {};
        struct HYS   : mm::Field<SW_PAD_CTL_PAD_GPIO_AD_B0_09, 16, 1> {};
    };

    // SELECT_INPUT registers — Daisy Chain input path selection
    struct LPUART1_TX_SELECT_INPUT
        : mm::Register<Iomuxc, 0x0530, 32> {
        struct DAISY : mm::Field<LPUART1_TX_SELECT_INPUT, 0, 1> {};
    };

    struct LPUART1_RX_SELECT_INPUT
        : mm::Register<Iomuxc, 0x052C, 32> {
        struct DAISY : mm::Field<LPUART1_RX_SELECT_INPUT, 0, 1> {};
    };

    // ... hundreds of SW_MUX_CTL_PAD, SW_PAD_CTL_PAD, SELECT_INPUT registers
};

using IOMUXC = Iomuxc;

// ─────────────────────────────────────────────────────────────
// パッドごとの ALT マッピングテーブル
// ─────────────────────────────────────────────────────────────

/// @brief GPIO_AD_B0_09 ALT function assignments
constexpr PadAltEntry gpio_ad_b0_09_alt[] = {
    {Alt::ALT0, "FLEXPWM2_PWMA3"},
    {Alt::ALT1, "FLEXCAN2_RX"},
    {Alt::ALT2, "LPUART1_TXD"},
    {Alt::ALT3, "SAI1_MCLK"},
    {Alt::ALT4, "CSI_DATA04"},
    {Alt::ALT5, "GPIO1_IO09"},       // GPIO mode
    {Alt::ALT6, "USDHC2_CLK"},
    {Alt::ALT7, "KPP_COL01"},
};

/// @brief Daisy chain configuration entry
struct DaisyEntry {
    uint32_t select_input_offset;  // IOMUXC 内の SELECT_INPUT レジスタオフセット
    uint8_t daisy_value;           // 書き込む DAISY 値
};

/// @brief GPIO_AD_B0_09 を LPUART1_TXD に使用する場合の Daisy 設定
constexpr DaisyEntry gpio_ad_b0_09_lpuart1_txd = {
    .select_input_offset = 0x0530,
    .daisy_value = 0,  // 0 = GPIO_AD_B0_09 を選択
};

} // namespace umi::pal::imxrt1060
```

---

## 8. データソース

| データソース | ベンダー | 取得方法 | カバー範囲 |
|------------|---------|---------|-----------|
| **STM32CubeMX DB** (Open Pin Data) | ST | `db/mcu/` ディレクトリ (XML) | 全 STM32 の AF テーブル、パッケージ別ピン配置 |
| **STM32 データシート** | ST | PDF | AF テーブル (Table "Alternate function mapping") |
| **RP2040 データシート** | Raspberry Pi | PDF Table 2 | GPIO0-GPIO29 の全 FUNCSEL 割当 |
| **RP2350 データシート** | Raspberry Pi | PDF | GPIO0-GPIO47 の FUNCSEL + セキュリティ属性 |
| **ESP32-S3 TRM** Chapter 6 | Espressif | PDF + ESP-IDF `gpio_sig_map.h` | GPIO Matrix 信号番号テーブル |
| **ESP32-P4 TRM** | Espressif | PDF + ESP-IDF | HP/LP GPIO Matrix 信号テーブル |
| **i.MX RT1060 RM** Chapter 10-11 | NXP | PDF + MCUXpresso Config Tools | IOMUXC レジスタ、ALT テーブル、Daisy Chain |
| **MCUXpresso Config Tools** (Pins Tool) | NXP | GUI tool / export | i.MX RT の MUX + PAD + Daisy 設定 |
