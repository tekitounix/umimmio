# C9: 電力管理

---

## 1. 概要

電力管理は、MCU のスリープモード、電源ドメイン、ウェイクアップソース、電圧レギュレータ、
および低消費電力ペリフェラルの構成を定義するカテゴリである。

組み込みオーディオデバイスでは、バッテリー動作時の消費電力削減と
オーディオ処理のリアルタイム性能を両立させる必要がある。
PAL レイヤでは、プラットフォーム固有のスリープモードとウェイクアップメカニズムを
constexpr メタデータとして統一的に表現する。

**PAL レイヤとの対応**:

| レイヤ | 含まれる定義 |
|--------|------------|
| L2 (コアプロファイル固有) | Cortex-M の SCR.SLEEPDEEP, WFI/WFE 命令対応 |
| L3 (MCU 固有) | スリープモード定義、電源ドメイン、レギュレータ設定 |
| L4 (デバイス固有) | ウェイクアップピン割り当て、利用可能な電源オプション |

---

## 2. 構成要素

### 2.1 電源ドメイン

MCU 内部は複数の電源ドメインに分割されている場合がある。
各ドメインは独立して電源を遮断または維持でき、スリープモードでの消費電力に直結する。

| ドメイン種別 | 説明 |
|-------------|------|
| コアドメイン | CPU コアとメイン SRAM |
| ペリフェラルドメイン | GPIO、タイマ、通信ペリフェラル |
| バックアップドメイン | RTC、バックアップレジスタ、SRAM (一部) |
| アナログドメイン | ADC、DAC、PLL |

### 2.2 スリープモード

消費電力とウェイクアップレイテンシのトレードオフにより、複数のスリープレベルが提供される。

### 2.3 ウェイクアップソース

スリープ状態から復帰するためのイベントソース。モードによって使用可能なソースが異なる。

### 2.4 電圧レギュレータ

内蔵レギュレータの動作モード (Normal / Low-Power / Off) はスリープモードと連動する。

### 2.5 低消費電力ペリフェラル

一部の MCU はスリープ中も動作可能な低消費電力ペリフェラルを持つ (LPUART, LPTIM, RTC 等)。

---

## 3. プラットフォーム差異

| プラットフォーム | スリープモード | 最深モード消費電力 | ウェイクアップレイテンシ (最深) | 特記事項 |
|----------------|--------------|------------------|---------------------------|---------|
| STM32F4 | Sleep / Stop / Standby | ~2.5 uA (Standby) | ~数 ms (Standby) | バックアップドメインで RTC + BKPSRAM 維持 |
| STM32H7 | Sleep / Stop / Standby | ~6 uA (Standby) | ~数 ms | D1/D2/D3 ドメイン独立制御、SMPS 内蔵 |
| RP2040 | Sleep / Dormant | ~0.18 mA (Sleep), ~1.3 uA (Dormant) | ~1 ms (Sleep) | Dormant は XOSC/ROSC 停止、GPIO/RTC ウェイクアップ |
| RP2350 | Sleep / Dormant | ~1 uA (Dormant) | ~1 ms | RP2040 と同様のモデル |
| ESP32-S3 | Active / Modem Sleep / Light Sleep / Deep Sleep | ~7 uA (Deep Sleep) | ~数百 us (Light), ~数 ms (Deep) | ULP コプロセッサがディープスリープ中も動作可能 |
| ESP32-P4 | Active / Light Sleep / Deep Sleep | TBD | TBD | HP/LP コア独立スリープ |
| i.MX RT | Run / Wait / Stop / SNVS | ~60 uA (SNVS) | ~数百 us (Wait), ~数 ms (Stop) | FlexRAM 電源制御、SNVS ドメイン |

---

## 4. 生成ヘッダのコード例

### 4.1 STM32F4 電力管理

```cpp
// pal/mcu/stm32f4/power.hh
#pragma once
#include <cstdint>

namespace umi::pal::stm32f4::power {

/// @brief スリープモード定義
enum class SleepMode : uint8_t {
    SLEEP,    // CPU クロック停止、ペリフェラル動作継続
    STOP,     // 全クロック停止 (LSI/LSE 除く)、SRAM 保持、レギュレータ Low-Power
    STANDBY,  // 全電源オフ (バックアップドメイン除く)、WKUP ピン / RTC でウェイクアップ
};

/// @brief Stop モードのレギュレータ設定
enum class StopRegulator : uint8_t {
    MAIN_ON    = 0, // メインレギュレータ維持 (高速復帰、消費電力高)
    LOW_POWER  = 1, // ロー・パワーレギュレータ (低消費電力、復帰時間増加)
};

/// @brief ウェイクアップソース
namespace wakeup {
    constexpr uint8_t WKUP_PIN = 0;          // PA0 (WKUP ピン)
    constexpr uint8_t RTC_ALARM = 1;         // RTC アラーム A/B
    constexpr uint8_t RTC_WAKEUP_TIMER = 2;  // RTC ウェイクアップタイマ
    constexpr uint8_t RTC_TAMPER = 3;        // RTC タンパー検出
    constexpr uint8_t EXTI_LINE = 4;         // 外部割り込みライン (Stop モードのみ)
}

/// @brief バックアップドメイン — Standby モードでも維持される領域
namespace backup {
    constexpr bool rtc_retained = true;           // RTC レジスタ保持
    constexpr bool bkpsram_retained = true;       // BKPSRAM 保持 (レギュレータ有効時)
    constexpr uint32_t bkpsram_size = 4 * 1024;   // 4KB バックアップ SRAM
    constexpr uint8_t bkp_register_count = 20;    // バックアップレジスタ数 (RTC_BKPxR)
}

/// @brief 電圧スケーリング (動的電圧周波数スケーリング)
enum class VoltageScale : uint8_t {
    SCALE1 = 1, // 最大性能 (168 MHz @ 3.3V)
    SCALE2 = 2, // 低消費電力 (144 MHz max)
    SCALE3 = 3, // 最低消費電力 (120 MHz max) — STM32F401 のみ
};

/// @brief Over-drive mode (STM32F42x/F43x only)
/// @note 180 MHz 動作に必要
constexpr bool has_overdrive = true; // F42x/F43x

} // namespace umi::pal::stm32f4::power
```

### 4.2 ESP32-S3 電力管理

ESP32-S3 は ULP (Ultra Low-Power) コプロセッサによるディープスリープ中の自律動作が特徴的。

```cpp
// pal/mcu/esp32s3/power.hh
#pragma once
#include <cstdint>

namespace umi::pal::esp32s3::power {

/// @brief スリープモード定義
enum class SleepMode : uint8_t {
    ACTIVE,       // 全コア・全ペリフェラル動作
    MODEM_SLEEP,  // Wi-Fi/BT モデム停止、CPU 動作
    LIGHT_SLEEP,  // CPU 停止、RAM 保持、高速復帰 (~数百 us)
    DEEP_SLEEP,   // RTC ドメインのみ動作、メイン RAM 消失
};

/// @brief ディープスリープ中のウェイクアップソース
namespace wakeup {
    constexpr uint8_t TIMER = 0;         // RTC タイマ
    constexpr uint8_t TOUCHPAD = 1;      // タッチセンサ
    constexpr uint8_t EXT0 = 2;          // 外部 GPIO (1 ピン、レベル)
    constexpr uint8_t EXT1 = 3;          // 外部 GPIO (複数ピン、マスク)
    constexpr uint8_t ULP = 4;           // ULP コプロセッサ
    constexpr uint8_t GPIO = 5;          // GPIO ウェイクアップ (Light Sleep)
    constexpr uint8_t UART = 6;          // UART 受信 (Light Sleep)
}

/// @brief ULP (Ultra Low-Power) コプロセッサ
/// @note ディープスリープ中もセンサ読み取りや GPIO 制御が可能
namespace ulp {
    constexpr bool available = true;
    constexpr bool riscv_core = true;      // ESP32-S3 ULP は RISC-V コア
    constexpr uint32_t sram_size = 8192;   // 8KB ULP SRAM
}

/// @brief ディープスリープ中に保持可能な RTC メモリ
namespace rtc_memory {
    constexpr uint32_t fast_size = 8 * 1024;   // 8KB RTC FAST (CPU アクセス可能)
    constexpr uint32_t slow_size = 8 * 1024;   // 8KB RTC SLOW (ULP アクセス可能)
}

} // namespace umi::pal::esp32s3::power
```

### 4.3 RP2040 電力管理

```cpp
// pal/mcu/rp2040/power.hh
#pragma once
#include <cstdint>

namespace umi::pal::rp2040::power {

/// @brief スリープモード定義
enum class SleepMode : uint8_t {
    SLEEP,    // CPU 停止、ペリフェラル動作継続、クロック維持
    DORMANT,  // XOSC/ROSC 停止、最低消費電力、GPIO / RTC でウェイクアップ
};

/// @brief Dormant モードからのウェイクアップソース
namespace wakeup {
    constexpr uint8_t GPIO_EDGE = 0;   // GPIO エッジ検出
    constexpr uint8_t GPIO_LEVEL = 1;  // GPIO レベル検出
    constexpr uint8_t RTC = 2;         // RTC アラーム
}

/// @brief クロックゲーティング
/// @note RP2040 はペリフェラル単位でのクロックゲーティングが可能
namespace clock_gate {
    constexpr bool per_peripheral = true;  // WAKE_EN レジスタで個別制御
}

/// @brief 電圧レギュレータ
namespace regulator {
    constexpr float vreg_default = 1.1f;   // コア電圧デフォルト
    constexpr float vreg_min = 0.8f;
    constexpr float vreg_max = 1.3f;
}

} // namespace umi::pal::rp2040::power
```

---

## 5. データソース

| プラットフォーム | ドキュメント | 入手先 |
|----------------|------------|--------|
| STM32F4 | STM32F4xx Reference Manual (RM0090) — Chapter: Power controller (PWR) | st.com |
| STM32F4 | STM32F407xx Datasheet — Section: Power supply / Current consumption | st.com |
| STM32H7 | STM32H7xx Reference Manual (RM0433) — PWR chapter, SMPS | st.com |
| RP2040 | RP2040 Datasheet — Chapter 2.11: Power | raspberrypi.com |
| RP2350 | RP2350 Datasheet — Power management chapter | raspberrypi.com |
| ESP32-S3 | ESP32-S3 Technical Reference Manual — Chapter: Power Management | espressif.com |
| ESP32-S3 | ESP32-S3 Datasheet — Section: Electrical Characteristics | espressif.com |
| ESP32-P4 | ESP32-P4 Technical Reference Manual — Chapter: Power Management | espressif.com |
| i.MX RT | i.MX RT1060 Reference Manual — Chapter: Power Management | nxp.com |

**注記**: 消費電力の具体的な数値はデータシートの電気特性セクションに記載されている。
リファレンスマニュアルではモード遷移のレジスタ設定手順が定義されている。
PAL では静的な構成情報 (モード定義、ウェイクアップソース) のみを定義し、
モード遷移ロジックは HAL 層で実装する。
