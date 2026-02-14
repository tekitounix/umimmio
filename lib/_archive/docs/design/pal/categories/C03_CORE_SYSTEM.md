# C3: コアシステム定義

---

## 1. 概要

コアシステム定義は、ビルド時に確定する MCU の静的構成情報を constexpr 定数として提供する。
レジスタアクセスではなく、コンパイル時にコードパスを選択するための構成パラメータである。

**典型的な用途**:
- FPU 有無による浮動小数点コード生成の切り替え
- NVIC 優先度ビット数によるプライオリティ設定のスケーリング
- キャッシュ有無による DMA バッファアラインメントの制御
- マルチコア環境でのコア数に基づくリソース分配

**PAL レイヤとの対応**:

| レイヤ | 含まれる定義 |
|--------|------------|
| L2 (コアプロファイル固有) | core_type, fpu_present, mpu_present, dsp_present, trustzone_present, icache/dcache |
| L4 (デバイスバリアント固有) | nvic_priority_bits, max_frequency, num_cores |

---

## 2. 定義項目の全一覧

| 項目 | 型 | 概要 | プラットフォーム差異 |
|------|-----|------|-------------------|
| `core_type` | `const char*` | コアアーキテクチャ名 | "Cortex-M4F", "Cortex-M0+", "Hazard3-RV32IMAC", "Xtensa-LX7" |
| `fpu_present` | `bool` | 浮動小数点ユニットの有無 | M4F/M7/M33: true, M0/M0+: false |
| `fpu_type` | `const char*` | FPU の種類 | "FPv4-SP" (M4F), "FPv5-DP" (M7), "FPv5-SP" (M33), "none" |
| `mpu_present` | `bool` | メモリ保護ユニットの有無 | M4/M7/M33: true, M0: false, M0+: optional |
| `mpu_regions` | `uint32_t` | MPU リージョン数 | 8 (ARMv7-M), 8-16 (ARMv8-M), 4-16 (RISC-V PMP) |
| `dsp_present` | `bool` | DSP 拡張命令の有無 | M4/M7: true, M0/M0+/M33: false or optional |
| `trustzone_present` | `bool` | TrustZone (ARMv8-M Security Extension) の有無 | M33: true, M4/M7: false |
| `nvic_priority_bits` | `uint32_t` | NVIC 割り込み優先度ビット数 | 2 (M0), 3-4 (M4/M33), 3-8 (実装依存) |
| `num_cores` | `uint32_t` | CPUコア数 | 1 (STM32F4), 2 (RP2040/RP2350/ESP32-S3) |
| `max_frequency` | `uint32_t` | 最大動作周波数 (Hz) | 168MHz (STM32F407), 600MHz (i.MX RT1060) |
| `icache_present` | `bool` | 命令キャッシュの有無 | M7: true, M4: false |
| `dcache_present` | `bool` | データキャッシュの有無 | M7: true, M4: false |
| `icache_size` | `uint32_t` | 命令キャッシュサイズ (bytes) | 16KB-64KB (M7) |
| `dcache_size` | `uint32_t` | データキャッシュサイズ (bytes) | 16KB-64KB (M7) |
| `cache_line_size` | `uint32_t` | キャッシュラインサイズ (bytes) | 32 (M7) |
| `vtor_present` | `bool` | VTOR (Vector Table Offset Register) の有無 | M0: false, M0+: optional, M3+: true |
| `sau_regions` | `uint32_t` | SAU リージョン数 (TrustZone) | 0 (非対応), 8 (M33 typical) |
| `endianness` | `const char*` | バイトオーダー | "little" (ほぼ全て), "big" (一部 M3/M4 設定可能) |

---

## 3. プラットフォーム別の特記事項

### 3.1 RP2350 -- デュアル ISA

RP2350 は同一チップ上で Cortex-M33 (ARM) と Hazard3 (RISC-V) の 2 つの ISA をサポートする。
ビルド時にどちらの ISA を使用するかが決定されるため、プリプロセッサ条件分岐が必要。

```
RP2350 起動時:
  Boot ROM → OTP 設定を読み取り → ARM or RISC-V モードで起動
  ビルド時: xmake f --arch=arm または xmake f --arch=riscv で選択
```

### 3.2 ESP32-P4 -- HP/LP デュアルサブシステム

ESP32-P4 は 2 つの異なるコアサブシステムを持つ:

| サブシステム | コア | ISA | 最大周波数 | 用途 |
|------------|------|-----|----------|------|
| HP (High Performance) | RISC-V (dual-core) | RV32IMAFC | 400MHz | メイン処理 |
| LP (Low Power) | RISC-V (single-core) | RV32IMC | 40MHz | スリープ時 ULP 処理 |

system.hh ではメインコア (HP) の情報を定義し、LP コアは別途 `lp_system` namespace で提供する。

### 3.3 ESP32-S3 -- Xtensa デュアルコア

ESP32-S3 は Xtensa LX7 デュアルコアであり、ARM / RISC-V とは大きく異なる:
- レジスタウィンドウ方式 (ARM の push/pop に相当する機能がハードウェアで自動化)
- 独自の割り込みアーキテクチャ (Interrupt Matrix + 32 割り込みライン)
- 組み込みの MAC/baseband ハードウェア

### 3.4 i.MX RT -- 高性能 Cortex-M7

i.MX RT シリーズは Cortex-M7 の全機能を活用する:
- I-Cache / D-Cache (各 32KB)
- FPv5 倍精度 FPU
- TCM (Tightly Coupled Memory) — FlexRAM で動的に構成

---

## 4. 生成ヘッダのコード例

### 4.1 STM32F407VG

```cpp
// pal/device/stm32f407vg/system.hh
#pragma once
#include <cstdint>

namespace umi::pal::stm32f407 {

namespace system {
    constexpr auto core_type = "Cortex-M4F";
    constexpr auto fpu_type  = "FPv4-SP";
    constexpr bool fpu_present = true;
    constexpr bool mpu_present = true;
    constexpr uint32_t mpu_regions = 8;
    constexpr bool dsp_present = true;
    constexpr bool trustzone_present = false;
    constexpr uint32_t nvic_priority_bits = 4;
    constexpr uint32_t num_cores = 1;
    constexpr uint32_t max_frequency = 168'000'000;
    constexpr bool icache_present = false;
    constexpr bool dcache_present = false;
    constexpr bool vtor_present = true;
    constexpr auto endianness = "little";
}

} // namespace umi::pal::stm32f407
```

### 4.2 RP2350 (デュアル ISA)

```cpp
// pal/device/rp2350/system.hh
#pragma once
#include <cstdint>

namespace umi::pal::rp2350 {

namespace system {
    // RP2350 supports dual ISA -- selected at build time
#if defined(UMI_RP2350_ARM)
    constexpr auto core_type = "Cortex-M33";
    constexpr auto fpu_type  = "FPv5-SP";
    constexpr bool trustzone_present = true;
    constexpr uint32_t sau_regions = 8;
    constexpr bool vtor_present = true;
#elif defined(UMI_RP2350_RISCV)
    constexpr auto core_type = "Hazard3-RV32IMAC";
    constexpr auto fpu_type  = "none"; // Hazard3 has no FPU
    constexpr bool trustzone_present = false;
    constexpr uint32_t sau_regions = 0;
    constexpr bool vtor_present = false; // RISC-V uses mtvec
#else
    #error "RP2350: Define UMI_RP2350_ARM or UMI_RP2350_RISCV"
#endif

    constexpr bool fpu_present = true;   // ARM: FPv5-SP, RISC-V: soft float
    constexpr bool mpu_present = true;   // ARM: ARMv8-M MPU, RISC-V: PMP
    constexpr uint32_t mpu_regions = 8;
    constexpr bool dsp_present = false;
    constexpr uint32_t nvic_priority_bits = 4;
    constexpr uint32_t num_cores = 2;
    constexpr uint32_t max_frequency = 150'000'000;
    constexpr bool icache_present = false;
    constexpr bool dcache_present = false;
    constexpr auto endianness = "little";
}

} // namespace umi::pal::rp2350
```

### 4.3 ESP32-S3

```cpp
// pal/device/esp32s3/system.hh
#pragma once
#include <cstdint>

namespace umi::pal::esp32s3 {

namespace system {
    constexpr auto core_type = "Xtensa-LX7";
    constexpr auto fpu_type  = "none"; // ESP32-S3 has no standard FPU (uses vector extensions)
    constexpr bool fpu_present = false;
    constexpr bool mpu_present = false;  // Xtensa uses PMS (Permission Management System)
    constexpr bool dsp_present = true;   // SIMD/MAC instructions
    constexpr bool trustzone_present = false;
    constexpr uint32_t num_cores = 2;
    constexpr uint32_t max_frequency = 240'000'000;
    constexpr bool icache_present = true;
    constexpr bool dcache_present = true;
    constexpr uint32_t icache_size = 16'384;   // 16 KB
    constexpr uint32_t dcache_size = 32'768;   // 32 KB (configurable: 16/32/64 KB)
    constexpr auto endianness = "little";

    // Xtensa-specific
    constexpr uint32_t interrupt_lines = 32;        // Per-core interrupt lines
    constexpr uint32_t interrupt_sources = 99;       // External interrupt sources
    constexpr uint32_t register_window_size = 64;    // A0-A63 physical registers

    // HP/LP subsystem -- ESP32-S3 does not have separate LP core (unlike P4)
    constexpr bool lp_core_present = false;
}

} // namespace umi::pal::esp32s3
```

### 4.4 ESP32-P4 (HP/LP デュアルサブシステム)

```cpp
// pal/device/esp32p4/system.hh
#pragma once
#include <cstdint>

namespace umi::pal::esp32p4 {

namespace system {
    constexpr auto core_type = "RISC-V-RV32IMAFC";
    constexpr auto fpu_type  = "RV32F"; // Single-precision float extension
    constexpr bool fpu_present = true;
    constexpr bool mpu_present = true;   // RISC-V PMP
    constexpr uint32_t mpu_regions = 16;
    constexpr bool dsp_present = true;   // P extension (packed SIMD)
    constexpr bool trustzone_present = false;
    constexpr uint32_t num_cores = 2;    // HP dual-core
    constexpr uint32_t max_frequency = 400'000'000;
    constexpr bool icache_present = true;
    constexpr bool dcache_present = true;
    constexpr uint32_t icache_size = 32'768;  // 32 KB
    constexpr uint32_t dcache_size = 64'000;  // 64 KB (configurable)
    constexpr auto endianness = "little";

    constexpr bool lp_core_present = true;
}

/// @brief LP (Low-Power) core system definitions
namespace lp_system {
    constexpr auto core_type = "RISC-V-RV32IMC";
    constexpr auto fpu_type  = "none";
    constexpr bool fpu_present = false;
    constexpr uint32_t num_cores = 1;
    constexpr uint32_t max_frequency = 40'000'000;
}

} // namespace umi::pal::esp32p4
```

### 4.5 i.MX RT1062

```cpp
// pal/device/imxrt1062/system.hh
#pragma once
#include <cstdint>

namespace umi::pal::imxrt1062 {

namespace system {
    constexpr auto core_type = "Cortex-M7";
    constexpr auto fpu_type  = "FPv5-DP"; // Double-precision FPU
    constexpr bool fpu_present = true;
    constexpr bool mpu_present = true;
    constexpr uint32_t mpu_regions = 16;
    constexpr bool dsp_present = true;
    constexpr bool trustzone_present = false;
    constexpr uint32_t nvic_priority_bits = 4;
    constexpr uint32_t num_cores = 1;
    constexpr uint32_t max_frequency = 600'000'000;
    constexpr bool icache_present = true;
    constexpr bool dcache_present = true;
    constexpr uint32_t icache_size = 32'768;  // 32 KB
    constexpr uint32_t dcache_size = 32'768;  // 32 KB
    constexpr uint32_t cache_line_size = 32;
    constexpr bool vtor_present = true;
    constexpr auto endianness = "little";

    // i.MX RT specific: FlexRAM bank configuration
    constexpr uint32_t flexram_banks = 16;        // 16 x 32KB banks
    constexpr uint32_t flexram_bank_size = 32'768; // 32 KB per bank
    constexpr uint32_t flexram_total = 512'000;    // 512 KB total
}

} // namespace umi::pal::imxrt1062
```

**使用例 (コンパイル時コード分岐)**:

```cpp
#include <pal/device/stm32f407vg/system.hh>

namespace sys = umi::pal::stm32f407::system;

void init_system() {
    if constexpr (sys::fpu_present) {
        // Enable FPU access in CPACR
        // SCB->CPACR |= (0xF << 20);  -- umimmio style:
        // transport.modify(Scb::CPACR::CP10::value(0x3), Scb::CPACR::CP11::value(0x3));
    }

    if constexpr (sys::icache_present) {
        // Enable instruction cache
        // SCB_EnableICache();
    }

    if constexpr (sys::dcache_present) {
        // Enable data cache (requires DMA buffer alignment awareness)
        // SCB_EnableDCache();
    }
}
```

---

## 5. データソース

| 情報 | ソース | 備考 |
|------|--------|------|
| コアプロファイル (FPU/MPU/DSP) | ARM Cortex-M TRM | コア固定 |
| NVIC 優先度ビット数 | SVD ファイル `<cpu>` セクション / データシート | デバイス依存 |
| 最大動作周波数 | データシート | デバイスバリアント依存 |
| キャッシュ構成 | Cortex-M7 TRM + ベンダーデータシート | M7 のみ |
| RP2350 デュアル ISA | RP2350 Datasheet, Chapter 2 | ビルド時選択 |
| ESP32 サブシステム | ESP32-S3/P4 Technical Reference Manual | ベンダー固有 |
| i.MX RT FlexRAM | i.MX RT1060 Reference Manual, Chapter 30 | ベンダー固有 |
| RISC-V コア情報 | RISC-V ISA Manual + ベンダー TRM | PMP リージョン数等は実装依存 |
