# C12: デバイスメタデータ

---

## 1. 概要

デバイスメタデータは、特定の MCU デバイス (パーツナンバー単位) に固有の情報を
constexpr データとして定義するカテゴリである。
他のカテゴリ (C1-C10, C12-C13) がペリフェラルやアーキテクチャの機能を定義するのに対し、
本カテゴリはデバイスそのもののスペック・能力・構成を定義する。

この情報はビルドシステムでのターゲット選択、静的検証 (コンパイル時のメモリ範囲チェック)、
およびドキュメント自動生成に利用される。

**PAL レイヤとの対応**:

| レイヤ | 含まれる定義 |
|--------|------------|
| L4 (デバイス固有) | パーツナンバー、パッケージ、ピン数、温度範囲、フラッシュ/SRAM サイズ |
| L4 (デバイス固有) | ペリフェラルインスタンスの有無と数量 |
| L4 (デバイス固有) | 最大動作周波数、電源電圧範囲 |

---

## 2. 定義項目一覧

| カテゴリ | 項目 | 型 | 説明 |
|---------|------|-----|------|
| 識別 | part_number | string | 完全なパーツナンバー (例: "STM32F407VGT6") |
| 識別 | family | string | ファミリ名 (例: "STM32F4") |
| 識別 | silicon_revision | string | シリコンリビジョン (該当する場合) |
| パッケージ | package | string | パッケージ種別 (例: "LQFP100", "QFN56") |
| パッケージ | pin_count | uint16_t | ピン数 |
| 動作条件 | temp_min / temp_max | int8_t | 動作温度範囲 (degC) |
| 動作条件 | temp_grade | string | 温度グレード (Commercial / Industrial / Automotive) |
| 動作条件 | vdd_min / vdd_max | float | 電源電圧範囲 (V) |
| 性能 | max_frequency | uint32_t | 最大 CPU クロック周波数 (Hz) |
| メモリ | flash_size | uint32_t | 内蔵フラッシュサイズ (バイト) |
| メモリ | sram_size | uint32_t | メイン SRAM サイズ (バイト) |
| メモリ | 追加メモリ | uint32_t | CCM, DTCM, ITCM, PSRAM 等 |
| ペリフェラル | has_xxx | bool | 特定ペリフェラルの有無 |
| ペリフェラル | xxx_count | uint8_t | ペリフェラルインスタンス数 |
| GPIO | gpio_port_count | uint8_t | GPIO ポート数 |
| GPIO | pinout | テーブル | ピン番号→GPIO/AF マッピング (C05 と連携) |

---

## 3. 生成ヘッダのコード例

### 3.1 STM32F407VG

```cpp
// pal/device/stm32f407vg/device.hh
#pragma once
#include <cstdint>

namespace umi::pal::stm32f407 {

namespace device {
    // ---- 識別情報 ----
    constexpr auto part_number = "STM32F407VGT6";
    constexpr auto family      = "STM32F4";
    constexpr auto subfamily   = "STM32F407";

    // ---- パッケージ ----
    constexpr auto package     = "LQFP100";
    constexpr uint16_t pin_count = 100;

    // ---- 動作条件 ----
    constexpr int8_t temp_min  = -40;
    constexpr int8_t temp_max  = 85;
    constexpr auto temp_grade  = "Industrial";
    constexpr float vdd_min    = 1.8f;
    constexpr float vdd_max    = 3.6f;

    // ---- 性能 ----
    constexpr uint32_t max_frequency = 168'000'000; // 168 MHz

    // ---- メモリ ----
    constexpr uint32_t flash_size = 1024 * 1024;     // 1MB
    constexpr uint32_t sram_size  = 192 * 1024;      // 192KB (SRAM1 112KB + SRAM2 16KB + backup 64KB)
    constexpr uint32_t sram1_size = 112 * 1024;      // SRAM1
    constexpr uint32_t sram2_size = 16 * 1024;       // SRAM2
    constexpr uint32_t ccm_size   = 64 * 1024;       // 64KB CCM (Core Coupled Memory)
    constexpr uint32_t bkpsram_size = 4 * 1024;      // 4KB バックアップ SRAM

    // ---- ペリフェラルインスタンス (有無) ----
    constexpr bool has_ethernet    = true;
    constexpr bool has_usb_otg_fs  = true;
    constexpr bool has_usb_otg_hs  = true;
    constexpr bool has_dcmi        = true;   // Digital Camera Interface
    constexpr bool has_fsmc        = true;   // Flexible Static Memory Controller
    constexpr bool has_rng         = true;   // Random Number Generator
    constexpr bool has_crc         = true;   // CRC calculation unit
    constexpr bool has_dac         = true;
    constexpr bool has_can         = true;
    constexpr bool has_sdio        = true;
    constexpr bool has_i2s         = true;

    // ---- ペリフェラルインスタンス (数量) ----
    constexpr uint8_t usart_count      = 6;   // USART1-3, UART4-5, USART6
    constexpr uint8_t spi_count        = 3;   // SPI1-3
    constexpr uint8_t i2c_count        = 3;   // I2C1-3
    constexpr uint8_t timer_count      = 14;  // TIM1-14
    constexpr uint8_t adc_count        = 3;   // ADC1-3
    constexpr uint8_t dac_count        = 2;   // DAC1-2
    constexpr uint8_t dma_count        = 2;   // DMA1-2
    constexpr uint8_t gpio_port_count  = 9;   // GPIOA-GPIOI
    constexpr uint8_t can_count        = 2;   // CAN1-2
    constexpr uint8_t usb_otg_count    = 2;   // OTG_FS + OTG_HS
}

} // namespace umi::pal::stm32f407
```

### 3.2 RP2040

```cpp
// pal/device/rp2040/device.hh
#pragma once
#include <cstdint>

namespace umi::pal::rp2040 {

namespace device {
    // ---- 識別情報 ----
    constexpr auto part_number = "RP2040";
    constexpr auto family      = "RP2";

    // ---- パッケージ ----
    constexpr auto package     = "QFN56";
    constexpr uint16_t pin_count = 56;

    // ---- 動作条件 ----
    constexpr int8_t temp_min  = -20;
    constexpr int8_t temp_max  = 85;
    constexpr auto temp_grade  = "Commercial";
    constexpr float vdd_min    = 1.62f; // IOVDD
    constexpr float vdd_max    = 3.63f;

    // ---- 性能 ----
    constexpr uint32_t max_frequency = 133'000'000; // 133 MHz (デュアルコア)
    constexpr uint8_t core_count = 2;               // Dual Cortex-M0+

    // ---- メモリ ----
    constexpr uint32_t flash_size = 0;                // 外部フラッシュ (内蔵なし)
    constexpr uint32_t sram_size  = 264 * 1024;       // 264KB (6 バンク)
    constexpr uint32_t sram_bank_count = 6;
    // Bank 0-3: 64KB each (striped), Bank 4: 4KB, Bank 5: 4KB

    // ---- ペリフェラルインスタンス ----
    constexpr bool has_pio       = true;   // Programmable I/O
    constexpr bool has_usb       = true;   // USB 1.1 Device/Host
    constexpr bool has_adc       = true;
    constexpr bool has_rtc       = true;
    constexpr bool has_pwm       = true;

    constexpr uint8_t uart_count   = 2;   // UART0-1
    constexpr uint8_t spi_count    = 2;   // SPI0-1
    constexpr uint8_t i2c_count    = 2;   // I2C0-1
    constexpr uint8_t pio_count    = 2;   // PIO0-1 (各 4 ステートマシン)
    constexpr uint8_t pwm_count    = 8;   // PWM0-7 (16 チャネル)
    constexpr uint8_t adc_count    = 1;   // ADC (4 入力 + 温度センサ)
    constexpr uint8_t adc_channel_count = 5;
    constexpr uint8_t timer_count  = 1;   // 64-bit system timer
    constexpr uint8_t dma_count    = 1;   // 12 チャネル
    constexpr uint8_t gpio_count   = 30;  // GPIO0-29
}

} // namespace umi::pal::rp2040
```

### 3.3 ESP32-S3

```cpp
// pal/device/esp32s3/device.hh
#pragma once
#include <cstdint>

namespace umi::pal::esp32s3 {

namespace device {
    // ---- 識別情報 ----
    constexpr auto part_number = "ESP32-S3";
    constexpr auto family      = "ESP32";

    // ---- パッケージ ----
    constexpr auto package     = "QFN56";
    constexpr uint16_t pin_count = 56;

    // ---- 動作条件 ----
    constexpr int8_t temp_min  = -40;
    constexpr int8_t temp_max  = 85;
    constexpr auto temp_grade  = "Industrial";
    constexpr float vdd_min    = 3.0f;
    constexpr float vdd_max    = 3.6f;

    // ---- 性能 ----
    constexpr uint32_t max_frequency = 240'000'000; // 240 MHz (デュアルコア Xtensa LX7)
    constexpr uint8_t core_count = 2;

    // ---- メモリ ----
    constexpr uint32_t flash_size = 0;                 // 外部フラッシュ (モジュール依存)
    constexpr uint32_t sram_size  = 512 * 1024;        // 512KB 内部 SRAM
    constexpr uint32_t rtc_fast_size = 8 * 1024;       // 8KB RTC FAST memory
    constexpr uint32_t rtc_slow_size = 8 * 1024;       // 8KB RTC SLOW memory
    constexpr bool has_psram_support = true;            // PSRAM インターフェース (OPI)

    // ---- 無線 ----
    constexpr bool has_wifi     = true;   // Wi-Fi 802.11 b/g/n
    constexpr bool has_ble      = true;   // Bluetooth 5 (LE)

    // ---- ペリフェラルインスタンス ----
    constexpr bool has_usb_otg  = true;   // USB OTG 1.1
    constexpr bool has_lcd      = true;   // LCD controller
    constexpr bool has_camera   = true;   // Camera interface
    constexpr bool has_touch    = true;   // Touch sensor

    constexpr uint8_t uart_count   = 3;   // UART0-2
    constexpr uint8_t spi_count    = 3;   // SPI0-2 (SPI0/1 はフラッシュ/PSRAM 専用)
    constexpr uint8_t i2c_count    = 2;   // I2C0-1
    constexpr uint8_t i2s_count    = 2;   // I2S0-1
    constexpr uint8_t adc_count    = 2;   // ADC1-2
    constexpr uint8_t adc_channel_count = 20;
    constexpr uint8_t dac_count    = 0;   // DAC なし (S3 では削除)
    constexpr uint8_t timer_count  = 4;   // Timer Group 0/1 x Timer 0/1
    constexpr uint8_t gpio_count   = 45;  // GPIO0-45 (一部入力専用)
    constexpr uint8_t touch_count  = 14;  // Touch sensor channels
}

} // namespace umi::pal::esp32s3
```

---

## 4. データソース

| プラットフォーム | ドキュメント | 入手先 |
|----------------|------------|--------|
| STM32F4 | STM32F407xx Datasheet — ピン配置、電気特性、ペリフェラル一覧 | st.com |
| STM32F4 | STM32CubeMX MCU database — デバイス別スペック | st.com |
| STM32F4 | STM32F407 SVD — ペリフェラルインスタンス情報 | st.com |
| RP2040 | RP2040 Datasheet — Chapter 1: Introduction | raspberrypi.com |
| RP2350 | RP2350 Datasheet — Introduction | raspberrypi.com |
| ESP32-S3 | ESP32-S3 Datasheet — Feature summary, Pin definitions | espressif.com |
| ESP32-P4 | ESP32-P4 Datasheet — Feature summary | espressif.com |
| i.MX RT | i.MX RT1060 Datasheet — Product overview, Ordering information | nxp.com |

**注記**: デバイスメタデータの多くはデータシートの冒頭 (Feature summary, Ordering information) に記載されている。
パーツナンバーのサフィックスからパッケージ、温度グレード、フラッシュサイズを自動判定可能な場合が多い。
PAL ジェネレータでは CMSIS-Pack の pdsc ファイルまたは CubeMX データベースからの自動抽出を推奨する。
