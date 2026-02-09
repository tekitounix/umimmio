# C4: 割り込み / 例外ベクター

---

## 1. 概要

割り込みベクターは、ハードウェアイベント発生時に CPU が実行するハンドラ関数のアドレステーブルである。
PAL ではこのテーブルを構成するために以下の 3 要素を提供する:

1. **IRQ 番号定義** (`IRQn` enum) -- 各割り込みソースの識別番号
2. **ベクターテーブル構造体** -- メモリ上に配置されるハンドラ関数ポインタの配列
3. **デフォルトハンドラ** -- 未登録割り込みのフォールバック処理

**PAL レイヤとの対応**:

| レイヤ | 含まれる定義 |
|--------|------------|
| L1 (アーキテクチャ共通) | コア例外 (Reset, NMI, HardFault 等) -- 全 Cortex-M 共通 |
| L3 (MCU ファミリ固有) | デバイス固有 IRQ (WWDG, USART1, DMA1_Stream0 等) |
| L4 (デバイスバリアント固有) | 実際の IRQ 数、ベクターテーブルサイズ |

---

## 2. 構成要素

### 2.1 コア例外 (Core Exceptions)

ARM Cortex-M のコア例外はアーキテクチャで固定されており、全チップ共通 (L1)。
負の IRQ 番号で表現される。

| 例外番号 | IRQ 番号 | 名称 | 概要 |
|---------|----------|------|------|
| 1 | - | Reset | リセットハンドラ (エントリポイント) |
| 2 | -14 | NMI | Non-Maskable Interrupt |
| 3 | -13 | HardFault | ハードフォルト |
| 4 | -12 | MemManage | メモリ管理フォルト (M3+ のみ) |
| 5 | -11 | BusFault | バスフォルト (M3+ のみ) |
| 6 | -10 | UsageFault | 使用フォルト (M3+ のみ) |
| 7-10 | - | Reserved | 予約済み |
| 11 | -5 | SVCall | Supervisor Call |
| 12 | -4 | DebugMonitor | デバッグモニタ (M3+ のみ) |
| 13 | - | Reserved | 予約済み |
| 14 | -2 | PendSV | Pendable Service Request |
| 15 | -1 | SysTick | SysTick タイマ |

**M0/M0+ の差異**: MemManage, BusFault, UsageFault, DebugMonitor は存在しない。
これらのフォルトは全て HardFault にエスカレートされる。

### 2.2 デバイス固有 IRQ (Device IRQs)

IRQ 番号 0 以降はベンダーが定義するペリフェラル割り込み。
IRQ の数はデバイスファミリによって大きく異なる。

### 2.3 ベクターテーブル構造

Cortex-M のベクターテーブルは Flash の先頭 (通常 0x0800_0000) に配置される:

```
Offset 0x00: Initial Stack Pointer
Offset 0x04: Reset Handler
Offset 0x08: NMI Handler
Offset 0x0C: HardFault Handler
  ...
Offset 0x40: IRQ0 Handler (device-specific start)
Offset 0x44: IRQ1 Handler
  ...
```

---

## 3. プラットフォーム差異

| 項目 | STM32F407 | STM32H743 | RP2040 | RP2350 (ARM) | RP2350 (RISC-V) | ESP32-S3 | ESP32-P4 | i.MX RT1062 |
|------|-----------|-----------|--------|-------------|----------------|----------|----------|------------|
| アーキテクチャ | Cortex-M4 | Cortex-M7 | Cortex-M0+ | Cortex-M33 | Hazard3 RV32 | Xtensa LX7 | RISC-V RV32 | Cortex-M7 |
| 割り込みモデル | NVIC | NVIC | NVIC | NVIC | PLIC相当 | Interrupt Matrix | CLIC | NVIC |
| デバイス IRQ 数 | 82 | 150 | 26 | 52 | 52 | 99 | 128 (HP) + 64 (LP) | 160 |
| コア例外 | 16 | 16 | 16 | 16 | N/A (trap) | N/A | N/A | 16 |
| ベクターテーブル配置 | Flash (VTOR) | Flash (VTOR) | Flash / SRAM | Flash (VTOR) | mtvec CSR | 固定 ROM | mtvec | Flash (VTOR) |
| マルチコア | single | single | dual (独立NVIC) | dual (独立NVIC) | dual | dual (独立) | dual HP + single LP | single |
| ネスト | NVIC 自動 | NVIC 自動 | NVIC 自動 | NVIC 自動 | SW 管理 | レベル式 | CLIC 自動 | NVIC 自動 |

---

## 4. ESP32 Interrupt Matrix 特記事項

ESP32 シリーズ (S3, S2, C3, C6, P4) は他のプラットフォームと根本的に異なる割り込みモデルを採用する。

### 4.1 概念

```
ペリフェラル割り込みソース (99個)
        ↓ Interrupt Matrix (ソフトウェア設定)
CPU 割り込みライン (32本/コア)
        ↓ ハードウェア
CPU 割り込みハンドラ
```

- **割り込みソース** (Peripheral Interrupt Source): UART, SPI, Timer 等のペリフェラルが発生する割り込み信号
- **CPU 割り込みライン**: 各 CPU コアには 32 本の割り込み入力がある
- **Interrupt Matrix**: ソースからラインへの任意マッピングをソフトウェアで設定可能

### 4.2 PAL での表現

Interrupt Matrix の設定は C6 (ペリフェラルレジスタ) で MMIO として定義するが、
割り込みソース番号は本カテゴリ (C4) で IRQn 相当として定義する。

```
ESP32-S3:
  PAL C4: 割り込みソース番号 (0-98) の enum
  PAL C6: INTERRUPT_CORE0/1_*_MAP_REG レジスタ (MMIO)
```

### 4.3 RISC-V ベース ESP32 (C3, C6, P4)

ESP32-C3/C6 は RISC-V ベースであり、PLIC または CLIC を使用する。
ESP32-P4 は CLIC を採用し、NVIC に近い自動ネスト機能を提供する。

---

## 5. 生成ヘッダのコード例

### 5.1 STM32F407 -- IRQn 列挙体

```cpp
// pal/device/stm32f407vg/irqn.hh
#pragma once
#include <cstdint>

namespace umi::pal::stm32f407 {

/// @brief IRQ numbers for STM32F407
/// @note Negative values are Cortex-M core exceptions (L1)
/// @note Non-negative values are device-specific IRQs (L3)
enum class IRQn : int16_t {
    // --- Cortex-M core exceptions (L1: architecture-universal) ---
    NON_MASKABLE_INT   = -14,
    HARD_FAULT         = -13,
    MEM_MANAGE         = -12,
    BUS_FAULT          = -11,
    USAGE_FAULT        = -10,
    SVCALL             = -5,
    DEBUG_MONITOR      = -4,
    PENDSV             = -2,
    SYSTICK            = -1,

    // --- STM32F407 device-specific IRQs (L3: MCU family) ---
    WWDG               = 0,
    PVD                = 1,
    TAMP_STAMP         = 2,
    RTC_WKUP           = 3,
    FLASH              = 4,
    RCC                = 5,
    EXTI0              = 6,
    EXTI1              = 7,
    EXTI2              = 8,
    EXTI3              = 9,
    EXTI4              = 10,
    DMA1_STREAM0       = 11,
    DMA1_STREAM1       = 12,
    DMA1_STREAM2       = 13,
    DMA1_STREAM3       = 14,
    DMA1_STREAM4       = 15,
    DMA1_STREAM5       = 16,
    DMA1_STREAM6       = 17,
    ADC                = 18,
    CAN1_TX            = 19,
    CAN1_RX0           = 20,
    CAN1_RX1           = 21,
    CAN1_SCE           = 22,
    EXTI9_5            = 23,
    TIM1_BRK_TIM9     = 24,
    TIM1_UP_TIM10     = 25,
    TIM1_TRG_COM_TIM11= 26,
    TIM1_CC            = 27,
    TIM2               = 28,
    TIM3               = 29,
    TIM4               = 30,
    I2C1_EV            = 31,
    I2C1_ER            = 32,
    I2C2_EV            = 33,
    I2C2_ER            = 34,
    SPI1               = 35,
    SPI2               = 36,
    USART1             = 37,
    USART2             = 38,
    USART3             = 39,
    EXTI15_10          = 40,
    RTC_ALARM          = 41,
    OTG_FS_WKUP       = 42,
    TIM8_BRK_TIM12    = 43,
    TIM8_UP_TIM13     = 44,
    TIM8_TRG_COM_TIM14= 45,
    TIM8_CC            = 46,
    DMA1_STREAM7       = 47,
    FSMC               = 48,
    SDIO               = 49,
    TIM5               = 50,
    SPI3               = 51,
    UART4              = 52,
    UART5              = 53,
    TIM6_DAC           = 54,
    TIM7               = 55,
    DMA2_STREAM0       = 56,
    DMA2_STREAM1       = 57,
    DMA2_STREAM2       = 58,
    DMA2_STREAM3       = 59,
    DMA2_STREAM4       = 60,
    ETH                = 61,
    ETH_WKUP           = 62,
    CAN2_TX            = 63,
    CAN2_RX0           = 64,
    CAN2_RX1           = 65,
    CAN2_SCE           = 66,
    OTG_FS             = 67,
    DMA2_STREAM5       = 68,
    DMA2_STREAM6       = 69,
    DMA2_STREAM7       = 70,
    USART6             = 71,
    I2C3_EV            = 72,
    I2C3_ER            = 73,
    OTG_HS_EP1_OUT    = 74,
    OTG_HS_EP1_IN     = 75,
    OTG_HS_WKUP       = 76,
    OTG_HS             = 77,
    DCMI               = 78,
    HASH_RNG           = 80,
    FPU                = 81,
};

/// @brief Total number of device-specific IRQs
constexpr int16_t irq_count = 82;

/// @brief Helper: convert IRQn to vector table index
consteval uint32_t irq_to_vector_index(IRQn irq) {
    return static_cast<uint32_t>(static_cast<int16_t>(irq) + 16);
}

} // namespace umi::pal::stm32f407
```

### 5.2 STM32F407 -- ベクターテーブル

```cpp
// pal/device/stm32f407vg/vectors.hh
#pragma once
#include <cstdint>
#include "irqn.hh"

namespace umi::pal::stm32f407 {

/// @brief Interrupt handler function pointer type
using IrqHandler = void(*)();

/// @brief Default handler -- infinite loop for unhandled interrupts
/// @note Declared weak so user code can override
[[noreturn]] inline void default_handler() {
    while (true) {
        __asm__ volatile("bkpt #0");
    }
}

/// @brief Cortex-M vector table structure for STM32F407
/// @note Must be placed at the beginning of Flash (0x08000000)
/// @note Aligned to power-of-2 boundary >= table size (required by VTOR)
struct VectorTable {
    uint32_t   initial_sp;                // Initial stack pointer value
    IrqHandler reset;                     // Reset handler (entry point)

    // Cortex-M core exceptions
    IrqHandler nmi;                       // -14: Non-Maskable Interrupt
    IrqHandler hard_fault;                // -13: Hard Fault
    IrqHandler mem_manage;                // -12: Memory Management Fault
    IrqHandler bus_fault;                 // -11: Bus Fault
    IrqHandler usage_fault;               // -10: Usage Fault
    uint32_t   reserved0[4];              // Reserved
    IrqHandler svcall;                    // -5:  SVCall
    IrqHandler debug_monitor;             // -4:  Debug Monitor
    uint32_t   reserved1;                 // Reserved
    IrqHandler pendsv;                    // -2:  PendSV
    IrqHandler systick;                   // -1:  SysTick

    // Device-specific IRQ handlers (IRQ 0-81)
    IrqHandler device_irq[irq_count];
};

/// @brief Total entries in vector table (core exceptions + device IRQs)
constexpr uint32_t vector_table_entries = 16 + irq_count; // 98

} // namespace umi::pal::stm32f407
```

### 5.3 RP2040 -- IRQn 列挙体

```cpp
// pal/device/rp2040/irqn.hh
#pragma once
#include <cstdint>

namespace umi::pal::rp2040 {

/// @brief IRQ numbers for RP2040 (Cortex-M0+ dual-core)
/// @note Each core has its own NVIC; this enum defines shared IRQ numbering
enum class IRQn : int16_t {
    // Cortex-M0+ core exceptions (simplified: no MemManage/BusFault/UsageFault)
    NON_MASKABLE_INT   = -14,
    HARD_FAULT         = -13,
    SVCALL             = -5,
    PENDSV             = -2,
    SYSTICK            = -1,

    // RP2040 device-specific IRQs
    TIMER_IRQ_0        = 0,
    TIMER_IRQ_1        = 1,
    TIMER_IRQ_2        = 2,
    TIMER_IRQ_3        = 3,
    PWM_IRQ_WRAP       = 4,
    USBCTRL_IRQ        = 5,
    XIP_IRQ            = 6,
    PIO0_IRQ_0         = 7,
    PIO0_IRQ_1         = 8,
    PIO1_IRQ_0         = 9,
    PIO1_IRQ_1         = 10,
    DMA_IRQ_0          = 11,
    DMA_IRQ_1          = 12,
    IO_IRQ_BANK0       = 13,
    IO_IRQ_QSPI        = 14,
    SIO_IRQ_PROC0      = 15,
    SIO_IRQ_PROC1      = 16,
    CLOCKS_IRQ         = 17,
    SPI0_IRQ           = 18,
    SPI1_IRQ           = 19,
    UART0_IRQ          = 20,
    UART1_IRQ          = 21,
    ADC_IRQ_FIFO       = 22,
    I2C0_IRQ           = 23,
    I2C1_IRQ           = 24,
    RTC_IRQ            = 25,
};

constexpr int16_t irq_count = 26;

} // namespace umi::pal::rp2040
```

### 5.4 RP2040 -- ベクターテーブル

```cpp
// pal/device/rp2040/vectors.hh
#pragma once
#include <cstdint>
#include "irqn.hh"

namespace umi::pal::rp2040 {

using IrqHandler = void(*)();

[[noreturn]] inline void default_handler() {
    while (true) {
        __asm__ volatile("bkpt #0");
    }
}

/// @brief Cortex-M0+ vector table for RP2040
/// @note M0+ has no MemManage/BusFault/UsageFault/DebugMonitor
/// @note Vector entries at those positions are reserved (zero)
struct VectorTable {
    uint32_t   initial_sp;
    IrqHandler reset;
    IrqHandler nmi;
    IrqHandler hard_fault;
    uint32_t   reserved0[7];  // MemManage through DebugMonitor -- reserved on M0+
    IrqHandler svcall;
    uint32_t   reserved1[2];
    IrqHandler pendsv;
    IrqHandler systick;

    // Device-specific IRQ handlers (IRQ 0-25)
    IrqHandler device_irq[irq_count];
};

constexpr uint32_t vector_table_entries = 16 + irq_count; // 42

} // namespace umi::pal::rp2040
```

### 5.5 ESP32-S3 -- 割り込みソース定義 (Interrupt Matrix モデル)

```cpp
// pal/device/esp32s3/interrupt_source.hh
#pragma once
#include <cstdint>

namespace umi::pal::esp32s3 {

/// @brief ESP32-S3 peripheral interrupt source numbers
/// @note These are NOT CPU interrupt lines; they are routed via Interrupt Matrix
/// @note Mapping: source -> CPU interrupt line is configured at runtime
enum class InterruptSource : uint8_t {
    // Sources 0-98 (peripheral interrupts)
    WIFI_MAC           = 0,
    WIFI_MAC_NMI       = 1,
    WIFI_PWR           = 2,
    WIFI_BB            = 3,
    BT_MAC             = 4,
    BT_BB              = 5,
    BT_BB_NMI          = 6,
    LP_TIMER           = 7,
    COEX               = 8,
    BLE                = 9,
    BLE_TIMER          = 10,
    // ...
    UART0              = 34,
    UART1              = 35,
    UART2              = 36,
    LEDC               = 45,
    // ...
    TG0_T0             = 50,
    TG0_T1             = 51,
    TG0_WDT            = 52,
    TG1_T0             = 53,
    TG1_T1             = 54,
    TG1_WDT            = 55,
    // ...
    SPI2_DMA           = 64,
    SPI3_DMA           = 65,
    I2S0               = 68,
    I2S1               = 69,
    // ...
    GPIO               = 72,
    GPIO_NMI           = 73,
    // ...
    DMA_IN_CH0         = 81,
    DMA_IN_CH1         = 82,
    DMA_IN_CH2         = 83,
    DMA_IN_CH3         = 84,
    DMA_IN_CH4         = 85,
    DMA_OUT_CH0        = 86,
    DMA_OUT_CH1        = 87,
    DMA_OUT_CH2        = 88,
    DMA_OUT_CH3        = 89,
    DMA_OUT_CH4        = 90,
    // ...
};

/// @brief CPU interrupt line numbers (0-31 per core)
enum class CpuInterruptLine : uint8_t {
    LINE_0  = 0,
    LINE_1  = 1,
    // ... LINE_2 through LINE_30
    LINE_31 = 31,
};

/// @brief Interrupt type (level-triggered or edge-triggered)
enum class InterruptType : uint8_t {
    LEVEL = 0,
    EDGE  = 1,
};

/// @brief Interrupt priority levels for Xtensa
/// @note Priority 0 = disabled, 1 = lowest, 7 = NMI
enum class InterruptPriority : uint8_t {
    DISABLED = 0,
    LEVEL_1  = 1,
    LEVEL_2  = 2,
    LEVEL_3  = 3,
    LEVEL_4  = 4,
    LEVEL_5  = 5,
    LEVEL_6  = 6,   // Debug level
    NMI      = 7,
};

constexpr uint8_t interrupt_source_count = 99;
constexpr uint8_t cpu_interrupt_lines = 32;

} // namespace umi::pal::esp32s3
```

### 5.6 RISC-V ベクター (RP2350 RISC-V mode / ESP32-P4)

```cpp
// pal/arch/riscv/trap.hh -- RISC-V trap handling metadata
#pragma once
#include <cstdint>

namespace umi::pal::riscv {

/// @brief RISC-V standard exception causes (mcause values when interrupt bit = 0)
enum class ExceptionCause : uint32_t {
    INSTRUCTION_MISALIGNED  = 0,
    INSTRUCTION_ACCESS      = 1,
    ILLEGAL_INSTRUCTION     = 2,
    BREAKPOINT              = 3,
    LOAD_MISALIGNED         = 4,
    LOAD_ACCESS             = 5,
    STORE_MISALIGNED        = 6,
    STORE_ACCESS             = 7,
    ECALL_U_MODE            = 8,
    ECALL_S_MODE            = 9,
    ECALL_M_MODE            = 11,
    INSTRUCTION_PAGE_FAULT  = 12,
    LOAD_PAGE_FAULT         = 13,
    STORE_PAGE_FAULT        = 15,
};

/// @brief RISC-V standard interrupt causes (mcause values when interrupt bit = 1)
enum class InterruptCause : uint32_t {
    MACHINE_SOFTWARE        = 3,
    MACHINE_TIMER           = 7,
    MACHINE_EXTERNAL        = 11,
};

/// @brief mtvec mode encoding
enum class TrapVectorMode : uint32_t {
    DIRECT   = 0,  // All traps go to BASE
    VECTORED = 1,  // Interrupts go to BASE + 4*cause
};

} // namespace umi::pal::riscv
```

---

## 6. データソース

| 情報 | ソース | 備考 |
|------|--------|------|
| Cortex-M コア例外 | ARMv6-M / ARMv7-M / ARMv8-M Architecture Reference Manual | L1: 全コア共通 |
| STM32F407 IRQ テーブル | STM32F407 Reference Manual (RM0090), Table 61 | L3: ファミリ共通 |
| STM32F407 IRQ 数 | STM32F407 Datasheet, NVIC section | L4: バリアント依存 |
| RP2040 IRQ テーブル | RP2040 Datasheet, Section 2.3.2 | 26 IRQs |
| RP2350 IRQ テーブル | RP2350 Datasheet | 52 IRQs (ARM/RISC-V 共通) |
| ESP32-S3 Interrupt Sources | ESP32-S3 TRM, Chapter: Interrupt Matrix | 99 sources |
| ESP32-P4 CLIC | ESP32-P4 TRM, Chapter: Interrupt | HP 128 + LP 64 |
| i.MX RT1062 IRQ テーブル | i.MX RT1060 Reference Manual, Chapter 3 | 160 IRQs |
| RISC-V trap causes | RISC-V Privileged ISA Specification, Table 3.6 | 標準例外コード |

**注記**: SVD ファイルには割り込みの `<interrupt>` セクションが含まれており、
IRQ 番号と名称の自動抽出が可能。ただしハンドラ名の命名規則はベンダーにより異なるため、
PAL の命名規則 (`UPPER_CASE` enum) への変換ルールをコード生成パイプラインで定義する必要がある。
