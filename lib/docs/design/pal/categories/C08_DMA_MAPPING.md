# C8: DMA マッピング

---

## 1. 概要

DMA (Direct Memory Access) マッピングとは、ペリフェラルと DMA コントローラ間の接続関係を定義するメタデータである。
多くの MCU では、DMA リクエストラインの割り当てがハードウェアで固定されているか、
MUX を介してソフトウェアで設定可能かのいずれかである。

PAL レイヤでは以下を定義する:

| 定義項目 | 説明 |
|---------|------|
| DMA コントローラ構成 | コントローラ数、ストリーム/チャネル数 |
| リクエストマッピング | ペリフェラル→DMA ストリーム/チャネルの対応表 |
| 転送設定パラメータ | バースト長、FIFO 閾値、データ幅の制約 |
| FIFO 構成 | FIFO の有無とサイズ |

**PAL レイヤとの対応**:

- L3 (MCU 固有): DMA コントローラの構成と MUX テーブル
- L4 (デバイス固有): 特定デバイスで利用可能な DMA チャネル数

---

## 2. 構成要素

### 2.1 DMA コントローラ

各プラットフォームの DMA コントローラ構成は大きく異なる。
固定マッピング (STM32F4) とダイナミックマッピング (DMAMUX, GDMA) の 2 つのモデルが存在する。

### 2.2 リクエストマッピング

ペリフェラルの TX/RX イベントが DMA コントローラに送られるルーティング情報。
固定テーブル型では、特定のストリーム+チャネル番号の組み合わせでペリフェラルが決まる。
MUX 型では、任意のチャネルに任意のペリフェラルを割り当て可能。

### 2.3 転送設定

DMA 転送の設定パラメータはプラットフォームにより制約が異なる:

| パラメータ | 説明 |
|-----------|------|
| データ幅 | byte / half-word / word |
| バースト長 | 1, 4, 8, 16 (STM32), 可変 (i.MX RT) |
| 転送モード | Normal / Circular / Double-buffer |
| 優先度 | Low / Medium / High / Very High |

### 2.4 FIFO

一部の DMA コントローラは FIFO バッファを内蔵し、バースト転送やデータ幅変換を行う。

---

## 3. プラットフォーム差異

| プラットフォーム | DMA 構成 | マッピング方式 | チャネル数 | 特記事項 |
|----------------|---------|--------------|-----------|---------|
| STM32F4 | DMA1 + DMA2 | 固定テーブル (stream x channel) | 2 x 8 streams x 8 channels | 各ストリームに FIFO (4 words) |
| STM32H7 | MDMA + DMA1 + DMA2 + BDMA + DMAMUX | DMAMUX による動的割り当て | MDMA 16ch, DMA 8+8, BDMA 8 | 3 層 DMA アーキテクチャ |
| RP2040 | DMA (single controller) | DREQ 番号による選択 | 12 チャネル | 各チャネルが独立した DREQ 選択レジスタを持つ |
| RP2350 | DMA (single controller) | DREQ 番号による選択 | 16 チャネル | RP2040 と互換、チャネル数増加 |
| ESP32-S3 | GDMA (General DMA) | 動的割り当て (peripheral ID) | 5 チャネル (TX/RX 独立) | 任意のチャネルを任意のペリフェラルに割り当て可能 |
| ESP32-P4 | AHB-DMA + AXI-DMA | 動的割り当て | AHB: 3ch, AXI: 3ch | HP/LP 独立バスによる帯域分離 |
| i.MX RT | eDMA + DMAMUX | DMAMUX による動的割り当て | 32 チャネル | Minor/Major loop、スキャッタ・ギャザー対応 |

---

## 4. 生成ヘッダのコード例

### 4.1 STM32F4 DMA マッピング (固定テーブル)

STM32F4 では、DMA ストリームとチャネル番号の組み合わせでペリフェラルが一意に決まる。
リファレンスマニュアルの DMA request mapping table をそのまま constexpr データとして表現する。

```cpp
// pal/mcu/stm32f4/dma_mapping.hh
#pragma once
#include <cstdint>

namespace umi::pal::stm32f4::dma {

/// @brief DMA リクエスト定義 — コントローラ / ストリーム / チャネルの固定マッピング
struct DmaRequest {
    uint8_t controller; // 1 or 2
    uint8_t stream;     // 0-7
    uint8_t channel;    // 0-7
};

// ---- USART ----
// USART1_TX -> DMA2 Stream 7 Channel 4
constexpr DmaRequest usart1_tx = {2, 7, 4};
// USART1_RX -> DMA2 Stream 2 Channel 4 (or Stream 5 Channel 4)
constexpr DmaRequest usart1_rx = {2, 2, 4};
// USART2_TX -> DMA1 Stream 6 Channel 4
constexpr DmaRequest usart2_tx = {1, 6, 4};
// USART2_RX -> DMA1 Stream 5 Channel 4
constexpr DmaRequest usart2_rx = {1, 5, 4};

// ---- SPI ----
// SPI1_TX -> DMA2 Stream 3 Channel 3
constexpr DmaRequest spi1_tx   = {2, 3, 3};
// SPI1_RX -> DMA2 Stream 0 Channel 3
constexpr DmaRequest spi1_rx   = {2, 0, 3};

// ---- I2C ----
// I2C1_TX -> DMA1 Stream 6 Channel 1
constexpr DmaRequest i2c1_tx   = {1, 6, 1};
// I2C1_RX -> DMA1 Stream 0 Channel 1
constexpr DmaRequest i2c1_rx   = {1, 0, 1};

// ---- ADC ----
// ADC1 -> DMA2 Stream 0 Channel 0
constexpr DmaRequest adc1      = {2, 0, 0};

// ---- I2S / SAI (Audio) ----
// SPI2_TX (I2S2) -> DMA1 Stream 4 Channel 0
constexpr DmaRequest i2s2_tx   = {1, 4, 0};
// SPI3_RX (I2S3) -> DMA1 Stream 0 Channel 0
constexpr DmaRequest i2s3_rx   = {1, 0, 0};

/// @brief DMA FIFO 設定
namespace fifo {
    constexpr uint8_t fifo_depth_words = 4; // 各ストリームに 4 ワード FIFO
    enum class Threshold : uint8_t {
        QUARTER = 0,
        HALF    = 1,
        THREE_QUARTER = 2,
        FULL    = 3,
    };
}

/// @brief 転送データ幅
enum class DataSize : uint8_t {
    BYTE      = 0,
    HALF_WORD = 1,
    WORD      = 2,
};

/// @brief バースト転送長
enum class BurstSize : uint8_t {
    SINGLE = 0,
    INCR4  = 1,
    INCR8  = 2,
    INCR16 = 3,
};

} // namespace umi::pal::stm32f4::dma
```

### 4.2 RP2040 DREQ マッピング

RP2040 の DMA は DREQ 番号で制御される。各チャネルの CTRL レジスタに DREQ 番号を設定することで、
任意のチャネルから任意のペリフェラルにアクセス可能。

```cpp
// pal/mcu/rp2040/dma_dreq.hh
#pragma once
#include <cstdint>

namespace umi::pal::rp2040::dma {

/// @brief DREQ numbers -- ペリフェラルから DMA への転送要求番号 (固定割り当て)
namespace dreq {
    constexpr uint8_t PIO0_TX0 = 0;
    constexpr uint8_t PIO0_TX1 = 1;
    constexpr uint8_t PIO0_TX2 = 2;
    constexpr uint8_t PIO0_TX3 = 3;
    constexpr uint8_t PIO0_RX0 = 4;
    constexpr uint8_t PIO0_RX1 = 5;
    constexpr uint8_t PIO0_RX2 = 6;
    constexpr uint8_t PIO0_RX3 = 7;
    constexpr uint8_t PIO1_TX0 = 8;
    constexpr uint8_t PIO1_TX1 = 9;
    constexpr uint8_t PIO1_TX2 = 10;
    constexpr uint8_t PIO1_TX3 = 11;
    constexpr uint8_t PIO1_RX0 = 12;
    constexpr uint8_t PIO1_RX1 = 13;
    constexpr uint8_t PIO1_RX2 = 14;
    constexpr uint8_t PIO1_RX3 = 15;
    constexpr uint8_t SPI0_TX  = 16;
    constexpr uint8_t SPI0_RX  = 17;
    constexpr uint8_t SPI1_TX  = 18;
    constexpr uint8_t SPI1_RX  = 19;
    constexpr uint8_t UART0_TX = 20;
    constexpr uint8_t UART0_RX = 21;
    constexpr uint8_t UART1_TX = 22;
    constexpr uint8_t UART1_RX = 23;
    constexpr uint8_t PWM_WRAP0 = 24;
    constexpr uint8_t PWM_WRAP1 = 25;
    constexpr uint8_t PWM_WRAP2 = 26;
    constexpr uint8_t PWM_WRAP3 = 27;
    constexpr uint8_t PWM_WRAP4 = 28;
    constexpr uint8_t PWM_WRAP5 = 29;
    constexpr uint8_t PWM_WRAP6 = 30;
    constexpr uint8_t PWM_WRAP7 = 31;
    constexpr uint8_t I2C0_TX  = 32;
    constexpr uint8_t I2C0_RX  = 33;
    constexpr uint8_t I2C1_TX  = 34;
    constexpr uint8_t I2C1_RX  = 35;
    constexpr uint8_t ADC      = 36;
    constexpr uint8_t XIP_STREAM = 37;
    constexpr uint8_t XIP_SSITX = 38;
    constexpr uint8_t XIP_SSIRX = 39;
    constexpr uint8_t TIMER0   = 0x3B; // ペーシングタイマ
    constexpr uint8_t TIMER1   = 0x3C;
    constexpr uint8_t TIMER2   = 0x3D;
    constexpr uint8_t TIMER3   = 0x3E;
    constexpr uint8_t PERMANENT = 0x3F; // 常時要求 (メモリ間転送用)
}

/// @brief DMA チャネル数
constexpr uint8_t channel_count = 12;

/// @brief 転送データ幅
enum class TransferSize : uint8_t {
    SIZE_8  = 0,
    SIZE_16 = 1,
    SIZE_32 = 2,
};

} // namespace umi::pal::rp2040::dma
```

### 4.3 ESP32-S3 GDMA (動的割り当て)

ESP32-S3 の GDMA は完全な動的割り当てモデルを採用する。
任意のチャネルを任意のペリフェラルに接続可能であり、チャネル番号とペリフェラル ID を設定するだけで動作する。

```cpp
// pal/mcu/esp32s3/gdma.hh
#pragma once
#include <cstdint>

namespace umi::pal::esp32s3::gdma {

/// @brief ペリフェラル ID -- GDMA チャネルに割り当てるペリフェラル識別子
/// @note 任意のチャネルに任意のペリフェラルを動的に割り当て可能
namespace periph_id {
    constexpr uint8_t SPI2  = 0;
    constexpr uint8_t SPI3  = 1;
    constexpr uint8_t UART0 = 2;
    constexpr uint8_t UART1 = 3;
    constexpr uint8_t I2S0  = 4;
    constexpr uint8_t I2S1  = 5;
    constexpr uint8_t AES   = 6;
    constexpr uint8_t SHA   = 7;
    constexpr uint8_t ADC   = 8;
    constexpr uint8_t LCD_CAM = 9;
    constexpr uint8_t RMT   = 10;
}

/// @brief GDMA チャネル数 (TX / RX 各 5 チャネル)
constexpr uint8_t channel_count = 5;

/// @brief バースト転送モード
enum class BurstMode : uint8_t {
    SINGLE = 0,
    INCR4  = 1,
};

/// @brief TX / RX チャネルは独立して設定可能
/// 同一ペリフェラルの TX/RX を異なるチャネルに割り当てることも可能

} // namespace umi::pal::esp32s3::gdma
```

### 4.4 ESP32-P4 デュアルバス DMA

ESP32-P4 は AHB-DMA と AXI-DMA の 2 系統を持ち、帯域幅要件に応じてバスを選択する。

```cpp
// pal/mcu/esp32p4/dma.hh
#pragma once
#include <cstdint>

namespace umi::pal::esp32p4::dma {

/// @brief AHB-DMA -- 低帯域ペリフェラル向け
namespace ahb {
    constexpr uint8_t channel_count = 3;

    namespace periph_id {
        constexpr uint8_t SPI2  = 0;
        constexpr uint8_t UART0 = 1;
        constexpr uint8_t UART1 = 2;
        constexpr uint8_t I2C0  = 3;
    }
}

/// @brief AXI-DMA -- 高帯域ペリフェラル向け (PSRAM, LCD, カメラ等)
namespace axi {
    constexpr uint8_t channel_count = 3;

    namespace periph_id {
        constexpr uint8_t I2S0     = 0;
        constexpr uint8_t LCD_CAM  = 1;
        constexpr uint8_t AES      = 2;
        constexpr uint8_t SHA      = 3;
    }
}

} // namespace umi::pal::esp32p4::dma
```

### 4.5 i.MX RT eDMA + DMAMUX

```cpp
// pal/mcu/imxrt/edma.hh
#pragma once
#include <cstdint>

namespace umi::pal::imxrt::dma {

/// @brief eDMA チャネル数
constexpr uint8_t channel_count = 32;

/// @brief DMAMUX ソース番号 (i.MX RT1060)
namespace mux_source {
    constexpr uint8_t LPUART1_TX = 2;
    constexpr uint8_t LPUART1_RX = 3;
    constexpr uint8_t LPSPI1_TX  = 13;
    constexpr uint8_t LPSPI1_RX  = 12;
    constexpr uint8_t SAI1_TX    = 19;
    constexpr uint8_t SAI1_RX    = 20;
    constexpr uint8_t SAI2_TX    = 21;
    constexpr uint8_t SAI2_RX    = 22;
    constexpr uint8_t LPI2C1     = 17;
    constexpr uint8_t ADC1       = 40;
    constexpr uint8_t ADC2       = 41;
    constexpr uint8_t FLEXSPI    = 30;
}

/// @brief eDMA Minor/Major loop 制御
/// Minor loop: 1 回の DMA リクエストで転送するバイト数
/// Major loop: Minor loop の繰り返し回数
/// スキャッタ・ギャザー: Major loop 完了時に次の TCD を自動ロード

} // namespace umi::pal::imxrt::dma
```

---

## 5. データソース

| プラットフォーム | ドキュメント | 入手先 |
|----------------|------------|--------|
| STM32F4 | STM32F4xx Reference Manual (RM0090) — Table 42/43: DMA1/2 request mapping | st.com |
| STM32H7 | STM32H7xx Reference Manual (RM0433) — DMAMUX chapter | st.com |
| RP2040 | RP2040 Datasheet — Chapter 2.5: DMA | raspberrypi.com |
| RP2350 | RP2350 Datasheet — DMA chapter | raspberrypi.com |
| ESP32-S3 | ESP32-S3 Technical Reference Manual — Chapter: GDMA | espressif.com |
| ESP32-P4 | ESP32-P4 Technical Reference Manual — Chapter: DMA | espressif.com |
| i.MX RT | i.MX RT1060 Reference Manual — Chapter: eDMA / DMAMUX | nxp.com |
| STM32F4 SVD | STM32F407 SVD — DMA peripheral definition | st.com |

**注記**: DMA マッピングテーブルは MCU リファレンスマニュアルに記載されている固定情報であり、
SVD ファイルには DMA リクエストマッピング情報が含まれない場合が多い。
PAL ジェネレータではリファレンスマニュアルの表から直接生成するか、
CMSIS-Pack の DMA description を利用する。
