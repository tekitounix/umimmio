# C1: コアペリフェラルレジスタ

---

## 1. 概要

コアペリフェラルとは、CPU コア自体に統合されたハードウェアブロックを指す。
MCU ベンダーが実装するペリフェラル (GPIO, UART 等) とは異なり、
アーキテクチャ仕様 (ARM Architecture Reference Manual, RISC-V ISA Manual 等) で定義される。

**PAL レイヤとの対応**:

| レイヤ | 含まれるコアペリフェラル |
|--------|----------------------|
| L1 (アーキテクチャ共通) | NVIC (基本構造), SCB, SysTick — 全 Cortex-M 共通 |
| L2 (コアプロファイル固有) | MPU, FPU, DWT, ITM, TPI, CoreDebug, SAU — コアバリアントで有無が変化 |

コアペリフェラルは原則としてメモリマップドであり、umimmio の型システムで表現可能。
ただし RISC-V の CSR (Control and Status Registers) は専用命令 (`csrr`/`csrw`) でアクセスするため、
umimmio 型ではなく constexpr メタデータとして定義する。

---

## 2. ARM Cortex-M コアペリフェラル

ARM Cortex-M のコアペリフェラルは System Control Space (SCS, 0xE000E000–0xE000EFFF) に配置される。

| ペリフェラル | ベースアドレス | レイヤ | 主要レジスタ | コア対応 |
|------------|-------------|--------|------------|---------|
| SysTick | 0xE000E010 | L1 | CSR, RVR, CVR, CALIB | 全 Cortex-M |
| NVIC | 0xE000E100 | L1 | ISER[n], ICER[n], ISPR[n], ICPR[n], IABR[n], IP[n] | 全 Cortex-M (レジスタ数はコア依存) |
| SCB | 0xE000ED00 | L1 | CPUID, ICSR, VTOR, AIRCR, SCR, CCR, SHPR[n], SHCSR | 全 Cortex-M |
| MPU | 0xE000ED90 | L2 | TYPE, CTRL, RNR, RBAR, RASR | M3/M4/M7 (ARMv6-M MPU), M23/M33 (ARMv8-M MPU) |
| FPU | 0xE000EF30 | L2 | FPCCR, FPCAR, FPDSCR | M4F, M7, M33 (FPU 搭載コア) |
| DWT | 0xE0001000 | L2 | CTRL, CYCCNT, CPICNT, EXCCNT, SLEEPCNT, LSUCNT, FOLDCNT, COMP[n], MASK[n], FUNCTION[n] | M3/M4/M7 (フル版), M0+ (簡易版: CYCCNT なし) |
| ITM | 0xE0000000 | L2 | STIM[0..31], TER, TPR, TCR, LAR | M3/M4/M7/M33 (M0/M0+ には存在しない) |
| TPI | 0xE0040000 | L2 | SSPSR, CSPSR, ACPR, SPPR, FFSR, FFCR | M3/M4/M7/M33 |
| CoreDebug | 0xE000EDF0 | L2 | DHCSR, DCRSR, DCRDR, DEMCR | 全 Cortex-M |
| SAU | 0xE000EDD0 | L2 | CTRL, TYPE, RNR, RBAR, RLAR | M23/M33 (TrustZone 搭載コアのみ) |

### コア別 L2 ペリフェラル有無

| ペリフェラル | M0 | M0+ | M3 | M4 | M4F | M7 | M23 | M33 |
|------------|----|----|----|----|-----|----|----|-----|
| MPU | -- | opt | opt | opt | opt | yes | opt | yes |
| FPU | -- | -- | -- | -- | SP | DP | -- | SP |
| DWT (CYCCNT) | -- | -- | yes | yes | yes | yes | -- | yes |
| DWT (簡易) | -- | opt | -- | -- | -- | -- | opt | -- |
| ITM | -- | -- | yes | yes | yes | yes | -- | yes |
| SAU | -- | -- | -- | -- | -- | -- | opt | yes |
| Cache | -- | -- | -- | -- | -- | yes | -- | -- |

(opt = オプション実装, SP = 単精度, DP = 倍精度)

---

## 3. RISC-V (RP2350 Hazard3, ESP32-C3/C6, ESP32-P4)

RISC-V アーキテクチャではコアペリフェラル相当の機能が CSR と MMIO の 2 つの方法で提供される。

### 3.1 CSR (Control and Status Registers) — 非 MMIO

CSR は `csrr` / `csrw` 命令でアクセスする専用レジスタ空間であり、メモリマップされない。
PAL では umimmio 型ではなく、constexpr アドレスとビットフィールドマスクとして定義する。

| CSR | アドレス | 概要 | 用途 |
|-----|--------|------|------|
| mstatus | 0x300 | Machine Status | 割り込み有効化、特権モード |
| mie | 0x304 | Machine Interrupt Enable | 個別割り込みの有効/無効 |
| mtvec | 0x305 | Machine Trap-Vector Base | トラップベクターアドレス |
| mepc | 0x341 | Machine Exception PC | 例外発生時の戻りアドレス |
| mcause | 0x342 | Machine Cause | トラップ原因コード |
| mtval | 0x343 | Machine Trap Value | トラップ付加情報 |
| mip | 0x344 | Machine Interrupt Pending | ペンディング中の割り込み |
| mcycle | 0xB00 | Machine Cycle Counter | サイクルカウンタ (DWT CYCCNT 相当) |
| minstret | 0xB02 | Machine Instructions Retired | 実行済み命令数 |

### 3.2 PLIC / CLIC — MMIO

| ペリフェラル | 方式 | 採用プラットフォーム | 概要 |
|------------|------|-------------------|------|
| PLIC (Platform-Level Interrupt Controller) | MMIO | ESP32-C3/C6 | 外部割り込みの優先度・イネーブル制御 |
| CLIC (Core-Local Interrupt Controller) | MMIO | ESP32-P4 | NVIC ライクなベクター割り込み |
| RP2350 Hazard3 | MMIO + CSR | RP2350 (RISC-V mode) | カスタム割り込みコントローラ |

### 3.3 mtime / mtimecmp — MMIO

RISC-V の標準タイマは MMIO でアクセスする (SysTick 相当)。
ただし実装はプラットフォーム依存であり、ベースアドレスが異なる。

---

## 4. Xtensa (ESP32-S3)

Xtensa アーキテクチャは ARM / RISC-V とは大きく異なるアプローチを採用する。

### 4.1 Special Registers — 非 MMIO

Xtensa の Special Registers は `rsr` / `wsr` / `xsr` 命令でアクセスする。

| レジスタ | 番号 | 概要 |
|---------|------|------|
| PS (Processor State) | 230 | 割り込みレベル、特権モード |
| EPC1–EPC7 | 177–183 | 例外発生時の PC |
| CCOUNT | 234 | サイクルカウンタ |
| CCOMPARE0–2 | 240–242 | サイクルコンパレータ (タイマ) |
| INTENABLE | 228 | 割り込み有効化マスク |
| INTERRUPT | 226 | ペンディング割り込み |

### 4.2 Window Registers

Xtensa LX7 はレジスタウィンドウ方式を使用する。
ウィンドウ管理レジスタ (WINDOWBASE, WINDOWSTART) は Special Register であり、MMIO ではない。

### 4.3 Interrupt Matrix — ESP32 固有 MMIO

ESP32 シリーズの割り込みルーティングは Interrupt Matrix で行う。
これは Xtensa アーキテクチャ標準ではなく、Espressif のベンダー実装 (L3) である。
ただし「コアへの割り込み配線」という役割上、本カテゴリでも言及する。

| 項目 | 値 |
|------|-----|
| 外部割り込みソース数 | 99 (ESP32-S3) |
| CPU 割り込みライン数 | 32 (各コア) |
| マッピング | ソフトウェア設定可能 (any-to-any) |
| レジスタ | PRO_*_MAP_REG / APP_*_MAP_REG |

---

## 5. 生成ヘッダのコード例

### 5.1 SysTick (L1, 全 Cortex-M 共通)

```cpp
// pal/arch/arm/cortex_m/systick.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::arm::cortex_m {

namespace mm = umi::mmio;

/// @brief SysTick Timer (all Cortex-M cores)
struct SysTick : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0xE000'E010;

    struct CSR : mm::Register<SysTick, 0x00, 32> {
        struct ENABLE    : mm::Field<CSR, 0,  1> {};
        struct TICKINT   : mm::Field<CSR, 1,  1> {};
        struct CLKSOURCE : mm::Field<CSR, 2,  1> {};
        struct COUNTFLAG : mm::Field<CSR, 16, 1, mm::RO> {};
    };
    struct RVR : mm::Register<SysTick, 0x04, 32> {
        struct RELOAD : mm::Field<RVR, 0, 24> {};
    };
    struct CVR : mm::Register<SysTick, 0x08, 32> {
        struct CURRENT : mm::Field<CVR, 0, 24> {};
    };
    struct CALIB : mm::Register<SysTick, 0x0C, 32, mm::RO> {
        struct TENMS : mm::Field<CALIB, 0,  24> {};
        struct SKEW  : mm::Field<CALIB, 30, 1> {};
        struct NOREF : mm::Field<CALIB, 31, 1> {};
    };
};

} // namespace umi::pal::arm::cortex_m
```

**使用例**:

```cpp
#include <pal/arch/arm/cortex_m/systick.hh>
#include <umimmio/transport/direct.hh>

using namespace umi::pal::arm::cortex_m;
mm::DirectTransport transport;

// SysTick を 1ms (168MHz, RELOAD = 168000 - 1) で設定
transport.write(SysTick::RVR::RELOAD::value(168000 - 1));
transport.modify(SysTick::CSR::ENABLE::Set{},
                 SysTick::CSR::TICKINT::Set{},
                 SysTick::CSR::CLKSOURCE::Set{});
```

### 5.2 NVIC (L1, 全 Cortex-M, レジスタ数はコア依存)

```cpp
// pal/arch/arm/cortex_m/nvic.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::arm::cortex_m {

namespace mm = umi::mmio;

/// @brief NVIC -- Nested Vectored Interrupt Controller
/// @note Priority bits are device-specific (L4), exposed as template parameter
/// @tparam PrioBits NVIC priority bits (3-8, device-dependent)
template<unsigned PrioBits = 4>
struct Nvic : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0xE000'E100;

    // ISER[0..7] -- Interrupt Set-Enable Registers
    struct ISER0 : mm::Register<Nvic, 0x000, 32> {};
    struct ISER1 : mm::Register<Nvic, 0x004, 32> {};
    struct ISER2 : mm::Register<Nvic, 0x008, 32> {};
    struct ISER3 : mm::Register<Nvic, 0x00C, 32> {};
    struct ISER4 : mm::Register<Nvic, 0x010, 32> {};
    struct ISER5 : mm::Register<Nvic, 0x014, 32> {};
    struct ISER6 : mm::Register<Nvic, 0x018, 32> {};
    struct ISER7 : mm::Register<Nvic, 0x01C, 32> {};

    // ICER[0..7] -- Interrupt Clear-Enable Registers
    struct ICER0 : mm::Register<Nvic, 0x080, 32> {};
    struct ICER1 : mm::Register<Nvic, 0x084, 32> {};
    // ... ICER2-ICER7

    // ISPR[0..7] -- Interrupt Set-Pending Registers
    struct ISPR0 : mm::Register<Nvic, 0x100, 32> {};
    struct ISPR1 : mm::Register<Nvic, 0x104, 32> {};
    // ... ISPR2-ISPR7

    // ICPR[0..7] -- Interrupt Clear-Pending Registers
    struct ICPR0 : mm::Register<Nvic, 0x180, 32> {};
    struct ICPR1 : mm::Register<Nvic, 0x184, 32> {};
    // ... ICPR2-ICPR7

    // IABR[0..7] -- Interrupt Active Bit Registers (read-only)
    struct IABR0 : mm::Register<Nvic, 0x200, 32, mm::RO> {};

    // IP[0..59] -- Interrupt Priority Registers (8-bit per interrupt, packed into 32-bit)
    struct IP0 : mm::Register<Nvic, 0x300, 32> {
        struct PRI_0 : mm::Field<IP0, 0,  8> {};
        struct PRI_1 : mm::Field<IP0, 8,  8> {};
        struct PRI_2 : mm::Field<IP0, 16, 8> {};
        struct PRI_3 : mm::Field<IP0, 24, 8> {};
    };
    struct IP1 : mm::Register<Nvic, 0x304, 32> {};
    // ... IP2-IP59

    static constexpr unsigned priority_bits = PrioBits;
};

} // namespace umi::pal::arm::cortex_m
```

### 5.3 SCB (L1/L2, Cortex-M, 一部レジスタはコアプロファイル依存)

```cpp
// pal/arch/arm/cortex_m/scb.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::arm::cortex_m {

namespace mm = umi::mmio;

/// @brief System Control Block
struct Scb : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0xE000'ED00;

    struct CPUID : mm::Register<Scb, 0x00, 32, mm::RO> {
        struct REVISION    : mm::Field<CPUID, 0,  4> {};
        struct PARTNO      : mm::Field<CPUID, 4,  12> {};
        struct ARCHITECTURE: mm::Field<CPUID, 16, 4> {};
        struct VARIANT     : mm::Field<CPUID, 20, 4> {};
        struct IMPLEMENTER : mm::Field<CPUID, 24, 8> {};
    };
    struct ICSR : mm::Register<Scb, 0x04, 32> {
        struct VECTACTIVE  : mm::Field<ICSR, 0,  9, mm::RO> {};
        struct VECTPENDING : mm::Field<ICSR, 12, 9, mm::RO> {};
        struct ISRPENDING  : mm::Field<ICSR, 22, 1, mm::RO> {};
        struct PENDSTCLR   : mm::Field<ICSR, 25, 1> {};
        struct PENDSTSET   : mm::Field<ICSR, 26, 1> {};
        struct PENDSVCLR   : mm::Field<ICSR, 27, 1> {};
        struct PENDSVSET   : mm::Field<ICSR, 28, 1> {};
        struct NMIPENDSET  : mm::Field<ICSR, 31, 1> {};
    };
    struct VTOR : mm::Register<Scb, 0x08, 32> {
        struct TBLOFF : mm::Field<VTOR, 7, 25> {};
    };
    struct AIRCR : mm::Register<Scb, 0x0C, 32, mm::RW, 0xFA050000> {
        struct VECTRESET   : mm::Field<AIRCR, 0,  1> {};
        struct VECTCLRACTIVE : mm::Field<AIRCR, 1,  1> {};
        struct SYSRESETREQ : mm::Field<AIRCR, 2,  1> {};
        struct PRIGROUP    : mm::Field<AIRCR, 8,  3> {};
        struct ENDIANESS   : mm::Field<AIRCR, 15, 1, mm::RO> {};
        struct VECTKEY     : mm::Field<AIRCR, 16, 16> {};
    };
    struct SCR : mm::Register<Scb, 0x10, 32> {
        struct SLEEPONEXIT : mm::Field<SCR, 1, 1> {};
        struct SLEEPDEEP   : mm::Field<SCR, 2, 1> {};
        struct SEVONPEND   : mm::Field<SCR, 4, 1> {};
    };
    struct CCR : mm::Register<Scb, 0x14, 32> {
        struct NONBASETHRDENA : mm::Field<CCR, 0, 1> {};
        struct USERSETMPEND   : mm::Field<CCR, 1, 1> {};
        struct UNALIGN_TRP    : mm::Field<CCR, 3, 1> {};
        struct DIV_0_TRP      : mm::Field<CCR, 4, 1> {};
        struct BFHFNMIGN      : mm::Field<CCR, 8, 1> {};
        struct STKALIGN       : mm::Field<CCR, 9, 1> {};
    };
};

} // namespace umi::pal::arm::cortex_m
```

### 5.4 RISC-V CSR (非 MMIO -- constexpr メタデータ)

CSR はメモリマップされないため、umimmio 型ではなく constexpr 定義を使用する。
ドライバ層は `__asm__ volatile("csrr ...")` または GCC ビルトインを介してアクセスする。

```cpp
// pal/arch/riscv/csr.hh -- CSRs use special instructions, not MMIO
#pragma once
#include <cstdint>

namespace umi::pal::riscv {

/// @brief RISC-V CSR addresses (accessed via csrr/csrw instructions, not MMIO)
namespace csr {
    // Machine-level CSRs
    constexpr uint16_t MSTATUS   = 0x300;
    constexpr uint16_t MISA      = 0x301;
    constexpr uint16_t MIE       = 0x304;
    constexpr uint16_t MTVEC     = 0x305;
    constexpr uint16_t MSCRATCH  = 0x340;
    constexpr uint16_t MEPC      = 0x341;
    constexpr uint16_t MCAUSE    = 0x342;
    constexpr uint16_t MTVAL     = 0x343;
    constexpr uint16_t MIP       = 0x344;

    // Machine counters
    constexpr uint16_t MCYCLE    = 0xB00;
    constexpr uint16_t MINSTRET  = 0xB02;

    // PMP (Physical Memory Protection)
    constexpr uint16_t PMPCFG0   = 0x3A0;
    constexpr uint16_t PMPCFG1   = 0x3A1;
    constexpr uint16_t PMPADDR0  = 0x3B0;
    constexpr uint16_t PMPADDR1  = 0x3B1;
}

/// @brief MSTATUS register field definitions
namespace mstatus {
    constexpr uint32_t MIE_BIT   = 1U << 3;    // Machine Interrupt Enable
    constexpr uint32_t MPIE_BIT  = 1U << 7;    // Machine Previous Interrupt Enable
    constexpr uint32_t MPP_MASK  = 0x3U << 11;  // Machine Previous Privilege
    constexpr uint32_t MPRV_BIT  = 1U << 17;   // Modify Privilege
}

/// @brief MCAUSE register field definitions
namespace mcause {
    constexpr uint32_t INTERRUPT_BIT = 1U << 31; // Interrupt (1) vs Exception (0)
    constexpr uint32_t CODE_MASK     = 0x7FFF'FFFF;
}

/// @brief MIE / MIP register field definitions
namespace mie {
    constexpr uint32_t MSIE_BIT = 1U << 3;  // Machine Software Interrupt Enable
    constexpr uint32_t MTIE_BIT = 1U << 7;  // Machine Timer Interrupt Enable
    constexpr uint32_t MEIE_BIT = 1U << 11; // Machine External Interrupt Enable
}

/// @brief CSR access helpers (inline assembly wrappers)
/// @note These are architecture-level primitives, not device-specific
inline uint32_t csr_read(uint16_t csr_addr) {
    uint32_t value;
    // Actual implementation uses compile-time CSR address in asm
    // This is a placeholder; real code uses macros or templates with immediate operands
    asm volatile("csrr %0, %1" : "=r"(value) : "i"(csr_addr));
    return value;
}

inline void csr_write(uint16_t csr_addr, uint32_t value) {
    asm volatile("csrw %0, %1" :: "i"(csr_addr), "r"(value));
}

inline void csr_set_bits(uint16_t csr_addr, uint32_t mask) {
    asm volatile("csrs %0, %1" :: "i"(csr_addr), "r"(mask));
}

inline void csr_clear_bits(uint16_t csr_addr, uint32_t mask) {
    asm volatile("csrc %0, %1" :: "i"(csr_addr), "r"(mask));
}

} // namespace umi::pal::riscv
```

### 5.5 RISC-V mtime (MMIO タイマ -- SysTick 相当)

```cpp
// pal/arch/riscv/mtime.hh -- RISC-V machine timer (MMIO, platform-specific base address)
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::riscv {

namespace mm = umi::mmio;

/// @brief RISC-V Machine Timer (mtime/mtimecmp)
/// @note Base address is platform-specific; bind via template parameter at device level
template <mm::Addr BaseAddr>
struct MachineTimerRegs : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    struct MTIME_LO   : mm::Register<MachineTimerRegs, 0x00, 32, mm::RO> {};
    struct MTIME_HI   : mm::Register<MachineTimerRegs, 0x04, 32, mm::RO> {};
    struct MTIMECMP_LO: mm::Register<MachineTimerRegs, 0x08, 32, mm::RW, 0xFFFFFFFF> {};
    struct MTIMECMP_HI: mm::Register<MachineTimerRegs, 0x0C, 32, mm::RW, 0xFFFFFFFF> {};
};

// Base address is bound at device level, e.g.:
// using MachineTimer = MachineTimerRegs<0x4000'8000>; // RP2350

} // namespace umi::pal::riscv
```

### 5.6 DWT (L2, Cortex-M3/M4/M7 フル版)

```cpp
// pal/core/cortex_m4f/dwt.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::arm::cortex_m4f {

namespace mm = umi::mmio;

/// @brief Data Watchpoint and Trace unit (full version for M3/M4/M7)
struct Dwt : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0xE000'1000;

    struct CTRL : mm::Register<Dwt, 0x000, 32> {
        struct CYCCNTENA    : mm::Field<CTRL, 0,  1> {};
        struct POSTPRESET   : mm::Field<CTRL, 1,  4> {};
        struct POSTCNT      : mm::Field<CTRL, 5,  4> {};
        struct CYCTAP       : mm::Field<CTRL, 9,  1> {};
        struct SYNCTAP      : mm::Field<CTRL, 10, 2> {};
        struct PCSAMPLENA   : mm::Field<CTRL, 12, 1> {};
        struct EXCTRCENA    : mm::Field<CTRL, 16, 1> {};
        struct CPIEVTENA    : mm::Field<CTRL, 17, 1> {};
        struct EXCEVTENA    : mm::Field<CTRL, 18, 1> {};
        struct SLEEPEVTENA  : mm::Field<CTRL, 19, 1> {};
        struct LSUEVTENA    : mm::Field<CTRL, 20, 1> {};
        struct FOLDEVTENA   : mm::Field<CTRL, 21, 1> {};
        struct CYCEVTENA    : mm::Field<CTRL, 22, 1> {};
        struct NUMCOMP      : mm::Field<CTRL, 28, 4, mm::RO> {};
    };
    struct CYCCNT  : mm::Register<Dwt, 0x004, 32> {};
    struct CPICNT  : mm::Register<Dwt, 0x008, 32> {};
    struct EXCCNT  : mm::Register<Dwt, 0x00C, 32> {};
    struct SLEEPCNT: mm::Register<Dwt, 0x010, 32> {};
    struct LSUCNT  : mm::Register<Dwt, 0x014, 32> {};
    struct FOLDCNT : mm::Register<Dwt, 0x018, 32> {};
    struct PCSR    : mm::Register<Dwt, 0x01C, 32, mm::RO> {};
    struct COMP0   : mm::Register<Dwt, 0x020, 32> {};
    struct MASK0   : mm::Register<Dwt, 0x024, 32> {};
    struct FUNCTION0: mm::Register<Dwt, 0x028, 32> {};
};

} // namespace umi::pal::arm::cortex_m4f
```

---

## 6. データソース

| アーキテクチャ | ドキュメント | 入手先 |
|-------------|------------|--------|
| ARM Cortex-M | ARMv6-M / ARMv7-M / ARMv8-M Architecture Reference Manual | ARM Developer |
| ARM Cortex-M | Cortex-M4 Technical Reference Manual (TRM) | ARM Developer |
| ARM Cortex-M | Cortex-M7 TRM | ARM Developer |
| ARM Cortex-M | Cortex-M33 TRM | ARM Developer |
| RISC-V | RISC-V Privileged Architecture Specification | riscv.org |
| RISC-V (RP2350) | RP2350 Datasheet, Chapter 3 (Hazard3 Processor) | raspberrypi.com |
| RISC-V (ESP32) | ESP32-C3 / ESP32-P4 Technical Reference Manual | espressif.com |
| Xtensa | Xtensa Instruction Set Architecture Reference Manual | Cadence |
| Xtensa (ESP32-S3) | ESP32-S3 Technical Reference Manual, Chapter: Interrupt Matrix | espressif.com |

**注記**: コアペリフェラルレジスタはアーキテクチャ仕様で固定されているため、
SVD ファイルからの自動生成ではなく、仕様書からの手動定義またはアーキテクチャ別の一括生成が適切である。
MCU ベンダーの SVD にはコアペリフェラルが含まれない、あるいは不正確な場合がある。
