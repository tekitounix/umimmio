# C5: メモリマップ

---

## 1. 概要

メモリマップは MCU のアドレス空間における各メモリ領域の配置 (ベースアドレスとサイズ) を定義する。
リンカスクリプト (C12) の生成、DMA バッファ配置、スタック/ヒープ設定の基礎データとなる。

**PAL レイヤとの対応**:

| レイヤ | 含まれる定義 |
|--------|------------|
| L3 (MCU ファミリ固有) | メモリ種類 (Flash, SRAM, CCM, XIP 等)、ペリフェラルバスベースアドレス |
| L4 (デバイスバリアント固有) | 各メモリの具体的なサイズ、バンク数 |

---

## 2. 構成要素

### 2.1 基本メモリ領域

| 領域 | 概要 | 典型的な属性 |
|------|------|------------|
| Flash (内蔵) | プログラムコード、定数データ | 読み取り専用 (実行時)、書き込みはフラッシュコントローラ経由 |
| SRAM | 汎用データ、スタック、ヒープ | 読み書き可能、DMA アクセス可能 |
| CCM/TCM | コア結合メモリ | 低レイテンシだが DMA アクセス不可 (MCU 依存) |
| Backup SRAM | バッテリバックアップ付き SRAM | 電源断時もデータ保持 |
| OTP | One-Time Programmable | 一度だけ書き込み可能 (デバイス固有設定) |

### 2.2 特殊メモリ領域

| 領域 | 概要 | 対象プラットフォーム |
|------|------|-------------------|
| Bit-Band | ビット単位のアトミックアクセス用エイリアス空間 | Cortex-M3/M4 |
| XIP (Execute In Place) | 外部 Flash を直接実行 | RP2040/RP2350, i.MX RT |
| PSRAM | 外部 SPI/OPI RAM | ESP32-S3, ESP32-P4 |
| RTC SRAM | Deep Sleep 時もデータ保持 | ESP32 シリーズ |
| FlexRAM | ITCM/DTCM/OCRAM に動的分割可能 | i.MX RT |

### 2.3 ペリフェラル空間

| 領域 | 概要 |
|------|------|
| APB1/APB2 | 低速ペリフェラルバス |
| AHB1/AHB2/AHB3 | 高速ペリフェラルバス |
| SCS (System Control Space) | コアペリフェラル (0xE000_0000 - 0xE00F_FFFF) |

---

## 3. プラットフォーム差異

| 項目 | STM32F407 | STM32H743 | RP2040 | RP2350 | ESP32-S3 | ESP32-P4 | i.MX RT1062 |
|------|-----------|-----------|--------|--------|----------|----------|------------|
| Flash | 1MB (内蔵) | 2MB (内蔵) | なし (XIP) | なし (XIP) | なし (XIP) | なし (XIP) | なし (XIP) |
| 主 SRAM | 112+16 KB | 512+288 KB | 264 KB (6バンク) | 520 KB | 512 KB | 768 KB HP + 32 KB LP | FlexRAM 512 KB |
| 特殊 SRAM | CCM 64 KB | AXI SRAM, ITCM, DTCM | -- | -- | RTC 8 KB | RTC 16 KB | ITCM/DTCM (FlexRAM) |
| 外部メモリ | FSMC | FMC + QSPI | -- | -- | PSRAM 8 MB | PSRAM 32 MB | SEMC (SDRAM) + FlexSPI |
| Bit-Band | あり | なし (M7) | なし (M0+) | なし | なし | なし | なし (M7) |
| XIP | なし | QSPI XIP | QSPI XIP | QSPI XIP | SPI/OPI XIP | SPI/OPI XIP | FlexSPI XIP |
| バス構造 | AHB/APB | AXI/AHB/APB | AHB-Lite | AHB-Lite | -- | -- | AXI/AHB/APB |

---

## 4. i.MX RT FlexRAM 特記事項

i.MX RT シリーズの内蔵 SRAM は FlexRAM として実装されており、
複数の 32KB バンクを ITCM / DTCM / OCRAM に動的に割り当て可能。

```
FlexRAM (i.MX RT1062): 16 banks x 32KB = 512KB total

デフォルト構成 (eFuse / IOMUXC_GPR で設定):
  ITCM: 128 KB (4 banks)  — 命令用 TCM (0x00000000)
  DTCM: 128 KB (4 banks)  — データ用 TCM (0x20000000)
  OCRAM: 256 KB (8 banks)  — 汎用 SRAM (0x20200000)

カスタム構成例 (オーディオ処理向け):
  ITCM: 64 KB (2 banks)   — 最小限のコード
  DTCM: 256 KB (8 banks)  — オーディオバッファ (低レイテンシ)
  OCRAM: 192 KB (6 banks)  — 汎用データ
```

PAL の memory.hh はデフォルト構成を定義し、FlexRAM の再構成はドライバレイヤ (umihal) で対応する。

---

## 5. ESP32 HP/LP メモリ分離 特記事項

### 5.1 ESP32-S3

ESP32-S3 のメモリは内蔵 SRAM と外部 PSRAM で構成される:

```
内蔵メモリ:
  Internal SRAM 0: 32 KB  (data/instruction, cacheable)
  Internal SRAM 1: 384 KB (data/instruction, cacheable)
  Internal SRAM 2: 64 KB  (data only, non-cacheable via DMA)
  RTC FAST: 8 KB (ULP コプロセッサからもアクセス可能)
  RTC SLOW: 8 KB (Deep Sleep 時もデータ保持)

外部メモリ:
  PSRAM: 最大 8 MB (SPI/OPI 接続, キャッシュ経由でアクセス)
```

### 5.2 ESP32-P4 (HP/LP 分離)

ESP32-P4 は HP (High Performance) サブシステムと LP (Low Power) サブシステムで
異なるメモリ空間を持つ:

```
HP サブシステム:
  HP SRAM: 768 KB (HP CPU からのみアクセス可能)
  PSRAM: 最大 32 MB (OPI 接続)

LP サブシステム:
  LP SRAM: 32 KB (LP CPU + HP CPU 双方からアクセス可能)
  RTC SRAM: 16 KB (Deep Sleep 時もデータ保持)

共有メモリ:
  LP SRAM は HP/LP 間のコミュニケーション領域として使用
```

---

## 6. 生成ヘッダのコード例

### 6.1 STM32F407VG

```cpp
// pal/device/stm32f407vg/memory.hh
#pragma once
#include <cstdint>

namespace umi::pal::stm32f407::memory {

// --- Flash ---
constexpr uint32_t flash_base = 0x0800'0000;
constexpr uint32_t flash_size = 0x10'0000; // 1024 KB

// Flash sectors (STM32F407 has mixed-size sectors)
constexpr uint32_t flash_sector_0_base = 0x0800'0000; // 16 KB
constexpr uint32_t flash_sector_1_base = 0x0800'4000; // 16 KB
constexpr uint32_t flash_sector_2_base = 0x0800'8000; // 16 KB
constexpr uint32_t flash_sector_3_base = 0x0800'C000; // 16 KB
constexpr uint32_t flash_sector_4_base = 0x0801'0000; // 64 KB
constexpr uint32_t flash_sector_5_base = 0x0802'0000; // 128 KB
// sectors 6-11: each 128 KB

// System memory (bootloader ROM)
constexpr uint32_t system_memory_base = 0x1FFF'0000;
constexpr uint32_t system_memory_size = 0x7800; // 30 KB

// --- SRAM ---
constexpr uint32_t sram1_base = 0x2000'0000;
constexpr uint32_t sram1_size = 0x1'C000; // 112 KB

constexpr uint32_t sram2_base = 0x2001'C000;
constexpr uint32_t sram2_size = 0x4000; // 16 KB

// Total contiguous SRAM
constexpr uint32_t sram_base = sram1_base;
constexpr uint32_t sram_size = sram1_size + sram2_size; // 128 KB

// --- CCM (Core Coupled Memory) ---
// Direct connection to CPU, no DMA access, no bit-banding
constexpr uint32_t ccm_base = 0x1000'0000;
constexpr uint32_t ccm_size = 0x1'0000; // 64 KB

// --- Backup SRAM ---
// Battery-backed, accessible via RTC domain
constexpr uint32_t bkpsram_base = 0x4002'4000;
constexpr uint32_t bkpsram_size = 0x1000; // 4 KB

// --- OTP ---
constexpr uint32_t otp_base = 0x1FFF'7800;
constexpr uint32_t otp_size = 528;

// --- Bit-Band regions (Cortex-M3/M4 only) ---
namespace bitband {
    constexpr uint32_t sram_region_base  = 0x2000'0000;
    constexpr uint32_t sram_region_end   = 0x200F'FFFF;
    constexpr uint32_t sram_alias_base   = 0x2200'0000;

    constexpr uint32_t periph_region_base = 0x4000'0000;
    constexpr uint32_t periph_region_end  = 0x400F'FFFF;
    constexpr uint32_t periph_alias_base  = 0x4200'0000;

    /// @brief Compute bit-band alias address for a given address and bit
    /// @tparam Addr Address within bit-band region (SRAM or peripheral)
    /// @tparam Bit Bit number (0-31)
    /// @return Bit-band alias address (word-aligned, bit 0 maps to target bit)
    template<uint32_t Addr, uint32_t Bit>
    consteval uint32_t alias() {
        static_assert(Bit < 32, "Bit number must be 0-31");
        if constexpr (Addr >= sram_region_base && Addr <= sram_region_end) {
            return sram_alias_base + ((Addr - sram_region_base) * 32) + (Bit * 4);
        } else if constexpr (Addr >= periph_region_base && Addr <= periph_region_end) {
            return periph_alias_base + ((Addr - periph_region_base) * 32) + (Bit * 4);
        } else {
            static_assert(Addr != Addr, "Address not in bit-band region");
        }
    }
}

// --- Peripheral base addresses ---
constexpr uint32_t periph_base = 0x4000'0000;

// APB1 peripherals
constexpr uint32_t apb1_base   = periph_base;
constexpr uint32_t tim2_base   = apb1_base + 0x0000;
constexpr uint32_t tim3_base   = apb1_base + 0x0400;
constexpr uint32_t tim4_base   = apb1_base + 0x0800;
constexpr uint32_t tim5_base   = apb1_base + 0x0C00;
constexpr uint32_t spi2_base   = apb1_base + 0x3800;
constexpr uint32_t spi3_base   = apb1_base + 0x3C00;
constexpr uint32_t usart2_base = apb1_base + 0x4400;
constexpr uint32_t usart3_base = apb1_base + 0x4800;
constexpr uint32_t i2c1_base   = apb1_base + 0x5400;
constexpr uint32_t i2c2_base   = apb1_base + 0x5800;
constexpr uint32_t i2c3_base   = apb1_base + 0x5C00;

// APB2 peripherals
constexpr uint32_t apb2_base   = periph_base + 0x1'0000;
constexpr uint32_t tim1_base   = apb2_base + 0x0000;
constexpr uint32_t usart1_base = apb2_base + 0x1000;
constexpr uint32_t usart6_base = apb2_base + 0x1400;
constexpr uint32_t adc1_base   = apb2_base + 0x2000;
constexpr uint32_t spi1_base   = apb2_base + 0x3000;
constexpr uint32_t syscfg_base = apb2_base + 0x3800;

// AHB1 peripherals
constexpr uint32_t ahb1_base   = periph_base + 0x2'0000;
constexpr uint32_t gpioa_base  = ahb1_base + 0x0000;
constexpr uint32_t gpiob_base  = ahb1_base + 0x0400;
constexpr uint32_t gpioc_base  = ahb1_base + 0x0800;
constexpr uint32_t gpiod_base  = ahb1_base + 0x0C00;
constexpr uint32_t gpioe_base  = ahb1_base + 0x1000;
constexpr uint32_t rcc_base    = ahb1_base + 0x3800;
constexpr uint32_t dma1_base   = ahb1_base + 0x6000;
constexpr uint32_t dma2_base   = ahb1_base + 0x6400;

// AHB2 peripherals
constexpr uint32_t ahb2_base   = periph_base + 0x1000'0000;
constexpr uint32_t usb_otg_fs_base = ahb2_base + 0x0000;

} // namespace umi::pal::stm32f407::memory
```

### 6.2 RP2040 (XIP + SRAM バンク)

```cpp
// pal/device/rp2040/memory.hh
#pragma once
#include <cstdint>

namespace umi::pal::rp2040::memory {

// --- ROM (Boot ROM) ---
constexpr uint32_t rom_base = 0x0000'0000;
constexpr uint32_t rom_size = 0x4000; // 16 KB

// --- XIP (Execute In Place) ---
// External QSPI flash accessed via XIP cache
constexpr uint32_t xip_base     = 0x1000'0000;
constexpr uint32_t xip_max_size = 0x100'0000; // 16 MB (max addressable)
// Actual flash size depends on external chip (typical: 2MB, 4MB, 16MB)

// XIP cache control
constexpr uint32_t xip_ctrl_base = 0x1400'0000;

// XIP uncached/streaming aliases
constexpr uint32_t xip_noalloc_base = 0x1100'0000; // No cache allocation
constexpr uint32_t xip_nocache_base = 0x1200'0000; // Cache bypass
constexpr uint32_t xip_stream_base  = 0x1500'0000; // Streaming (DMA-friendly)

// --- SRAM ---
// 264 KB total, organized as 6 banks for interleaved access
// Banks 0-3: striped (round-robin per word for concurrent access)
constexpr uint32_t sram_base  = 0x2000'0000;
constexpr uint32_t sram_size  = 0x4'2000; // 264 KB total

constexpr uint32_t sram_bank0_base = 0x2100'0000; // 64 KB (non-striped alias)
constexpr uint32_t sram_bank1_base = 0x2101'0000; // 64 KB
constexpr uint32_t sram_bank2_base = 0x2102'0000; // 64 KB
constexpr uint32_t sram_bank3_base = 0x2103'0000; // 64 KB
constexpr uint32_t sram_bank4_base = 0x2104'0000; // 4 KB
constexpr uint32_t sram_bank5_base = 0x2104'1000; // 4 KB

constexpr uint32_t sram_bank_size_main = 0x1'0000; // 64 KB per main bank
constexpr uint32_t sram_bank_size_aux  = 0x1000;   // 4 KB per aux bank

// --- Peripheral base addresses ---
constexpr uint32_t sio_base        = 0xD000'0000; // Single-cycle I/O
constexpr uint32_t io_bank0_base   = 0x4001'4000;
constexpr uint32_t pads_bank0_base = 0x4001'C000;
constexpr uint32_t io_qspi_base    = 0x4001'8000;
constexpr uint32_t pads_qspi_base  = 0x4002'0000;
constexpr uint32_t uart0_base      = 0x4003'4000;
constexpr uint32_t uart1_base      = 0x4003'8000;
constexpr uint32_t spi0_base       = 0x4003'C000;
constexpr uint32_t spi1_base       = 0x4004'0000;
constexpr uint32_t i2c0_base       = 0x4004'4000;
constexpr uint32_t i2c1_base       = 0x4004'8000;
constexpr uint32_t pio0_base       = 0x5020'0000;
constexpr uint32_t pio1_base       = 0x5030'0000;
constexpr uint32_t timer_base      = 0x4005'4000;
constexpr uint32_t dma_base        = 0x5000'0000;
constexpr uint32_t pwm_base        = 0x4005'0000;
constexpr uint32_t adc_base        = 0x4004'C000;

// Multicore-specific
constexpr uint32_t ppb_base        = 0xE000'0000; // Private Peripheral Bus (per-core NVIC, etc.)

} // namespace umi::pal::rp2040::memory
```

### 6.3 ESP32-S3 (HP/LP SRAM + RTC SRAM + PSRAM)

```cpp
// pal/device/esp32s3/memory.hh
#pragma once
#include <cstdint>

namespace umi::pal::esp32s3::memory {

// --- Internal SRAM ---
// Instruction bus access (cacheable)
constexpr uint32_t iram_base = 0x4037'0000;
constexpr uint32_t iram_size = 0x8'0000; // 512 KB (shared with DRAM, cache-split)

// Data bus access (cacheable)
constexpr uint32_t dram_base = 0x3FC8'8000;
constexpr uint32_t dram_size = 0x7'8000; // 480 KB (remaining after cache allocation)

// SRAM (non-cacheable, DMA-accessible)
constexpr uint32_t sram0_base = 0x4037'0000;
constexpr uint32_t sram0_size = 0x8000;  // 32 KB (instruction/data)
constexpr uint32_t sram1_base = 0x3FC8'8000;
constexpr uint32_t sram1_size = 0x6'0000; // 384 KB
constexpr uint32_t sram2_base = 0x3FCE'8000;
constexpr uint32_t sram2_size = 0x1'0000; // 64 KB (DMA-only, non-cacheable)

// --- RTC Memory ---
// RTC FAST: accessible from main CPU (and ULP coprocessor in some modes)
constexpr uint32_t rtc_fast_base = 0x600F'E000;
constexpr uint32_t rtc_fast_size = 0x2000; // 8 KB

// RTC SLOW: persists in Deep Sleep, accessible from ULP coprocessor
constexpr uint32_t rtc_slow_base = 0x5000'0000;
constexpr uint32_t rtc_slow_size = 0x2000; // 8 KB

// --- External PSRAM ---
// Accessed via cache, mapped to instruction/data bus
// Actual size depends on external chip (typical: 2MB, 4MB, 8MB)
constexpr uint32_t psram_data_base = 0x3C00'0000; // Data bus (cacheable)
constexpr uint32_t psram_inst_base = 0x4200'0000; // Instruction bus (cacheable)
constexpr uint32_t psram_max_size  = 0x80'0000;   // 8 MB (max for SPI/OPI)

// --- Internal ROM ---
constexpr uint32_t rom_base = 0x4000'0000;
constexpr uint32_t rom_size = 0x6'0000; // 384 KB (bootloader + crypto libs)

// --- Peripheral base addresses ---
constexpr uint32_t periph_base   = 0x6000'0000;
constexpr uint32_t uart0_base    = periph_base + 0x0000;
constexpr uint32_t uart1_base    = periph_base + 0x1'0000;
constexpr uint32_t uart2_base    = periph_base + 0x2'0000;
constexpr uint32_t spi2_base     = periph_base + 0x2'4000;
constexpr uint32_t spi3_base     = periph_base + 0x2'5000;
constexpr uint32_t i2c0_base     = periph_base + 0x1'3000;
constexpr uint32_t i2c1_base     = periph_base + 0x2'7000;
constexpr uint32_t i2s0_base     = periph_base + 0x2'D000;
constexpr uint32_t i2s1_base     = periph_base + 0x2'E000;
constexpr uint32_t gpio_base     = periph_base + 0x4000;
constexpr uint32_t timer0_base   = periph_base + 0x1'F000;
constexpr uint32_t timer1_base   = periph_base + 0x2'0000;
constexpr uint32_t gdma_base     = periph_base + 0x3'F000;
constexpr uint32_t intmatrix_base= periph_base + 0x6'0000; // Interrupt Matrix

} // namespace umi::pal::esp32s3::memory
```

### 6.4 i.MX RT1062 (FlexRAM + ITCM/DTCM/OCRAM)

```cpp
// pal/device/imxrt1062/memory.hh
#pragma once
#include <cstdint>

namespace umi::pal::imxrt1062::memory {

// --- FlexRAM (total 512 KB, configurable as ITCM/DTCM/OCRAM) ---
// Default configuration (can be changed via IOMUXC_GPR registers)

// ITCM (Instruction TCM) -- zero-wait-state instruction access
constexpr uint32_t itcm_base = 0x0000'0000;
constexpr uint32_t itcm_default_size = 0x2'0000; // 128 KB (4 banks)

// DTCM (Data TCM) -- zero-wait-state data access
constexpr uint32_t dtcm_base = 0x2000'0000;
constexpr uint32_t dtcm_default_size = 0x2'0000; // 128 KB (4 banks)

// OCRAM (On-Chip RAM) -- general-purpose, DMA-accessible
constexpr uint32_t ocram_base = 0x2020'0000;
constexpr uint32_t ocram_default_size = 0x4'0000; // 256 KB (8 banks)

// FlexRAM bank parameters
constexpr uint32_t flexram_total_banks = 16;
constexpr uint32_t flexram_bank_size   = 0x8000; // 32 KB per bank
constexpr uint32_t flexram_total_size  = flexram_total_banks * flexram_bank_size; // 512 KB

/// @brief FlexRAM bank type configuration
enum class FlexRamBankType : uint8_t {
    OCRAM = 0b00,
    DTCM  = 0b01,
    ITCM  = 0b10,
    // 0b11 reserved
};

// --- External Memory (FlexSPI) ---
// FlexSPI1: typically connected to QSPI/HyperFlash for XIP boot
constexpr uint32_t flexspi1_base = 0x6000'0000;
constexpr uint32_t flexspi1_max_size = 0x800'0000; // 128 MB (max addressable)

// FlexSPI2: optional second flash/PSRAM
constexpr uint32_t flexspi2_base = 0x7000'0000;
constexpr uint32_t flexspi2_max_size = 0x1000'0000; // 256 MB (max addressable)

// SEMC (SDRAM/NOR/NAND external memory controller)
constexpr uint32_t semc_base     = 0x8000'0000;
constexpr uint32_t semc_max_size = 0x6000'0000; // 1.5 GB (max addressable)

// --- Boot ROM ---
constexpr uint32_t rom_base = 0x0020'0000;
constexpr uint32_t rom_size = 0x1'8000; // 96 KB

// --- Peripheral base addresses ---
constexpr uint32_t aips1_base = 0x4000'0000; // AIPS-1 (64 KB per slot)
constexpr uint32_t aips2_base = 0x4010'0000; // AIPS-2
constexpr uint32_t aips3_base = 0x4020'0000; // AIPS-3
constexpr uint32_t aips4_base = 0x4030'0000; // AIPS-4

// Key peripheral addresses
constexpr uint32_t iomuxc_base   = 0x401F'8000;
constexpr uint32_t iomuxc_gpr_base = 0x400A'C000;
constexpr uint32_t gpio1_base    = 0x401B'8000;
constexpr uint32_t gpio2_base    = 0x401B'C000;
constexpr uint32_t gpio3_base    = 0x401C'0000;
constexpr uint32_t gpio4_base    = 0x401C'4000;
constexpr uint32_t gpio5_base    = 0x400C'0000;
constexpr uint32_t lpuart1_base  = 0x4018'4000;
constexpr uint32_t lpuart2_base  = 0x4018'8000;
constexpr uint32_t lpspi1_base   = 0x4039'4000;
constexpr uint32_t lpspi2_base   = 0x4039'8000;
constexpr uint32_t lpi2c1_base   = 0x403F'0000;
constexpr uint32_t lpi2c2_base   = 0x403F'4000;
constexpr uint32_t ccm_base      = 0x400F'C000;
constexpr uint32_t edma_base     = 0x400E'8000;

// --- Cache-related constants ---
// Cortex-M7 has I-Cache and D-Cache, affecting DMA buffer alignment
constexpr uint32_t cache_line_size = 32; // bytes

/// @brief Align an address to cache line boundary (for DMA coherence)
consteval uint32_t cache_align(uint32_t addr) {
    return (addr + cache_line_size - 1) & ~(cache_line_size - 1);
}

} // namespace umi::pal::imxrt1062::memory
```

**使用例 (リンカスクリプト生成への入力)**:

```cpp
// This data drives linker script generation:
//
// MEMORY {
//     FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
//     SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
//     CCM   (rwx) : ORIGIN = 0x10000000, LENGTH = 64K
// }
//
// Generated from:
//   flash_base / flash_size
//   sram_base / sram_size
//   ccm_base / ccm_size
```

---

## 7. データソース

| 情報 | ソース | 備考 |
|------|--------|------|
| STM32F407 メモリマップ | STM32F407 Reference Manual (RM0090), Section 2.3 | L3 + L4 |
| STM32F407 Flash セクタ | RM0090, Section 3.4 | バリアント依存 (VG: 1MB, VE: 512KB) |
| RP2040 メモリマップ | RP2040 Datasheet, Section 2.2 | SRAM バンク構造 |
| RP2350 メモリマップ | RP2350 Datasheet, Section 2.6 | 520KB SRAM |
| ESP32-S3 メモリマップ | ESP32-S3 TRM, Chapter: System and Memory | HP/cache 分割 |
| ESP32-P4 メモリマップ | ESP32-P4 TRM, Chapter: System and Memory | HP/LP 分離 |
| i.MX RT1062 メモリマップ | i.MX RT1060 Reference Manual, Chapter 2 | FlexRAM 構成 |
| i.MX RT FlexRAM 詳細 | i.MX RT1060 RM, Chapter 30 (FlexRAM) | バンク割り当て |
| Bit-Band 仕様 | ARMv7-M Architecture Reference Manual, Section B3.1 | M3/M4 のみ |
| SVD ファイル | ベンダー提供 SVD のペリフェラルベースアドレス | コード生成入力 |

**注記**: メモリサイズはデバイスバリアント (L4) で変化するが、
ベースアドレスとメモリの種類は MCU ファミリ (L3) で固定される。
コード生成パイプラインでは SVD のペリフェラルベースアドレスに加え、
データシートから Flash/SRAM サイズを抽出してバリアント別ヘッダを生成する。
