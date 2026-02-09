# C2: コア特殊レジスタ・命令イントリンシクス (非 MMIO)

---

## 1. 概要

本カテゴリは **メモリマップされない** CPU コア固有のレジスタおよび命令を扱う。
これらは専用命令（ARM: `MSR`/`MRS`、RISC-V: `csrr`/`csrw`、Xtensa: `rsr`/`wsr`）
でのみアクセス可能であり、umimmio の型システムでは表現できない。

C1 (コアペリフェラルレジスタ) が MMIO 空間のレジスタを対象とするのに対し、
本カテゴリは CPU 命令レベルのプリミティブを対象とする。

**PAL での表現方針**:
- constexpr メタデータ (レジスタアドレス / ビットフィールド定義)
- inline assembly ラッパー関数
- コンパイラ組み込み関数 (GCC/Clang ビルトイン) の活用

**PAL レイヤとの対応**:

| レイヤ | 含まれる定義 |
|--------|------------|
| L1 (アーキテクチャ共通) | メモリバリア、割り込み制御、パワー管理命令 — 全コアで共通 |
| L2 (コアプロファイル固有) | FPU レジスタ、DSP/SIMD 命令、TrustZone 命令、キャッシュ操作 — コアバリアントで有無が変化 |

---

## 2. ARM Cortex-M

### 2.1 特殊レジスタ (MSR/MRS アクセス)

ARM Cortex-M の特殊レジスタは `MSR` (Move to Special Register) / `MRS` (Move from Special Register) 命令でアクセスする。MMIO アドレス空間には存在しない。

| レジスタ | アクセス | レイヤ | 概要 | コア対応 |
|---------|---------|--------|------|---------|
| PRIMASK | MSR/MRS | L1 | 割り込みマスク (bit 0: 1=全割り込み禁止) | 全 Cortex-M |
| BASEPRI | MSR/MRS | L1 | 優先度ベースの割り込みマスク | M3/M4/M7/M33 (M0/M0+ には無い) |
| FAULTMASK | MSR/MRS | L1 | HardFault 以外の全例外をマスク | M3/M4/M7/M33 |
| CONTROL | MSR/MRS | L1 | スタック選択 (MSP/PSP)、特権レベル、FPU コンテキスト | 全 Cortex-M |
| PSP | MSR/MRS | L1 | プロセススタックポインタ | 全 Cortex-M |
| MSP | MSR/MRS | L1 | メインスタックポインタ | 全 Cortex-M |
| xPSR | MRS | L1 | プログラムステータスレジスタ (APSR+IPSR+EPSR) | 全 Cortex-M |
| FPSCR | VMRS/VMSR | L2 | 浮動小数点ステータス/制御レジスタ | M4F/M7/M33 (FPU 搭載のみ) |
| MSPLIM | MSR/MRS | L2 | MSP スタックリミット | M33 (ARMv8-M Mainline) |
| PSPLIM | MSR/MRS | L2 | PSP スタックリミット | M33 (ARMv8-M Mainline) |

### 2.2 メモリバリア命令

データの順序整合性を保証する命令。RTOS のコンテキストスイッチ、DMA 操作、
マルチコア同期等で必須。

| 命令 | エンコーディング | レイヤ | 概要 |
|------|---------------|--------|------|
| DMB | `dmb` | L1 | Data Memory Barrier — メモリアクセスの順序を保証 |
| DSB | `dsb` | L1 | Data Synchronization Barrier — 全メモリアクセスの完了を保証 |
| ISB | `isb` | L1 | Instruction Synchronization Barrier — パイプラインフラッシュ |

### 2.3 割り込み制御命令

| 命令 | エンコーディング | レイヤ | 概要 |
|------|---------------|--------|------|
| CPSID I | `cpsid i` | L1 | PRIMASK=1 (割り込み禁止) |
| CPSIE I | `cpsie i` | L1 | PRIMASK=0 (割り込み許可) |
| CPSID F | `cpsid f` | L1 | FAULTMASK=1 (HardFault 以外の全例外禁止) |
| CPSIE F | `cpsie f` | L1 | FAULTMASK=0 |
| SVC #imm | `svc #n` | L1 | SuperVisor Call — RTOS システムコール |
| BKPT #imm | `bkpt #n` | L1 | ブレークポイント — デバッガ停止 |

### 2.4 パワー管理命令

| 命令 | エンコーディング | レイヤ | 概要 |
|------|---------------|--------|------|
| WFI | `wfi` | L1 | Wait For Interrupt — 割り込みまで CPU 停止 |
| WFE | `wfe` | L1 | Wait For Event — イベントまで CPU 停止 (スピンロック解放) |
| SEV | `sev` | L1 | Send Event — 他コアへのイベント送信 |

### 2.5 ビット操作命令

| 命令 | エンコーディング | レイヤ | 概要 |
|------|---------------|--------|------|
| CLZ | `clz Rd, Rm` | L1 | Count Leading Zeros — 先頭ゼロビット数 |
| RBIT | `rbit Rd, Rm` | L1 | Reverse Bits — ビット反転 (M3+) |
| REV | `rev Rd, Rm` | L1 | Byte Reverse Word — エンディアン変換 |
| REV16 | `rev16 Rd, Rm` | L1 | Byte Reverse Half-Word |
| REVSH | `revsh Rd, Rm` | L1 | Byte Reverse Signed Half-Word |

### 2.6 飽和演算命令 (DSP 拡張)

| 命令 | エンコーディング | レイヤ | 概要 | コア対応 |
|------|---------------|--------|------|---------|
| SSAT | `ssat Rd, #imm, Rm` | L2 | 符号付き飽和 | M4/M7/M33 |
| USAT | `usat Rd, #imm, Rm` | L2 | 符号無し飽和 | M4/M7/M33 |
| QADD | `qadd Rd, Rm, Rn` | L2 | 飽和加算 (32-bit) | M4/M7/M33 |
| QSUB | `qsub Rd, Rm, Rn` | L2 | 飽和減算 (32-bit) | M4/M7/M33 |
| SMLAD | `smlad Rd, Rm, Rs, Rn` | L2 | Dual 16-bit 乗加算 | M4/M7 |
| SMULBB/BT/TB/TT | — | L2 | 16×16 乗算 (上位/下位半ワード選択) | M4/M7 |
| SMMUL | `smmul Rd, Rm, Rs` | L2 | 32×32→上位32bit 乗算 | M4/M7 |

### 2.7 SIMD 命令 (ARMv7E-M DSP 拡張)

| 命令 | レイヤ | 概要 | コア対応 |
|------|--------|------|---------|
| SADD16/SSUB16 | L2 | packed 16-bit 加減算 | M4/M7 |
| SADD8/SSUB8 | L2 | packed 8-bit 加減算 | M4/M7 |
| SHADD16/SHSUB16 | L2 | packed halving 加減算 | M4/M7 |
| UXTB16/SXTB16 | L2 | packed 8→16 拡張 | M4/M7 |
| SEL | L2 | GE フラグに基づくバイト選択 | M4/M7 |
| USAD8/USADA8 | L2 | 絶対差の合計 (Sum of Absolute Differences) | M4/M7 |

### 2.8 排他アクセス命令 (アトミック操作)

マルチコア / RTOS 環境でのロックフリーデータ構造、mutex 実装に必須。

| 命令 | エンコーディング | レイヤ | 概要 | コア対応 |
|------|---------------|--------|------|---------|
| LDREX | `ldrex Rt, [Rn]` | L1 | 排他ロード (32-bit) | M3/M4/M7/M33 |
| STREX | `strex Rd, Rt, [Rn]` | L1 | 排他ストア (32-bit, 成功=0/失敗=1) | M3/M4/M7/M33 |
| LDREXB | `ldrexb Rt, [Rn]` | L1 | 排他ロード (8-bit) | M3/M4/M7/M33 |
| STREXB | `strexb Rd, Rt, [Rn]` | L1 | 排他ストア (8-bit) | M3/M4/M7/M33 |
| LDREXH | `ldrexh Rt, [Rn]` | L1 | 排他ロード (16-bit) | M3/M4/M7/M33 |
| STREXH | `strexh Rd, Rt, [Rn]` | L1 | 排他ストア (16-bit) | M3/M4/M7/M33 |
| CLREX | `clrex` | L1 | 排他モニタクリア | M3/M4/M7/M33 |

**注記**: Cortex-M0/M0+ は排他アクセス命令をサポートしない（シングルコアかつシンプルな割り込みモデル）。

### 2.9 TrustZone 命令 (ARMv8-M)

| 命令 | レイヤ | 概要 | コア対応 |
|------|--------|------|---------|
| TT / TTT | L2 | Test Target — アドレスのセキュリティ属性を調査 | M23/M33 |
| BXNS | L2 | Branch with Exchange to Non-Secure — NS 遷移 | M23/M33 |
| BLXNS | L2 | Branch with Link and Exchange to Non-Secure | M23/M33 |
| SG | L2 | Secure Gateway — NS→S 遷移のエントリポイント | M23/M33 |

### 2.10 キャッシュ操作命令 (Cortex-M7)

| 命令 | レイヤ | 概要 | コア対応 |
|------|--------|------|---------|
| I-Cache Invalidate All | L2 | 命令キャッシュ全無効化 | M7 |
| D-Cache Invalidate by Address | L2 | データキャッシュ行無効化 (アドレス指定) | M7 |
| D-Cache Clean by Address | L2 | データキャッシュ行ライトバック | M7 |
| D-Cache Clean and Invalidate | L2 | データキャッシュ行 ライトバック+無効化 | M7 |

**注記**: キャッシュ操作は SCB のキャッシュ保守レジスタ (0xE000EF50–) への MMIO 書き込みで
トリガーするが、操作前後のバリア命令 (DSB/ISB) との組み合わせがイントリンシクスとして必要。
CMSIS では `SCB_InvalidateDCache_by_Addr()` 等の関数として提供される。

### 2.11 コア別対応マトリクス

| 機能グループ | M0 | M0+ | M3 | M4 | M4F | M7 | M23 | M33 |
|------------|----|----|----|----|-----|----|----|-----|
| PRIMASK | yes | yes | yes | yes | yes | yes | yes | yes |
| BASEPRI | -- | -- | yes | yes | yes | yes | -- | yes |
| FAULTMASK | -- | -- | yes | yes | yes | yes | -- | yes |
| FPSCR | -- | -- | -- | -- | yes | yes | -- | yes |
| MSPLIM/PSPLIM | -- | -- | -- | -- | -- | -- | yes | yes |
| メモリバリア (DMB/DSB/ISB) | yes | yes | yes | yes | yes | yes | yes | yes |
| CPSID/CPSIE | yes | yes | yes | yes | yes | yes | yes | yes |
| WFI/WFE/SEV | yes | yes | yes | yes | yes | yes | yes | yes |
| CLZ/RBIT/REV | -- | -- | yes | yes | yes | yes | yes | yes |
| 飽和演算 (SSAT/USAT) | -- | -- | -- | yes | yes | yes | -- | opt |
| SIMD (SADD/SSUB) | -- | -- | -- | yes | yes | yes | -- | -- |
| LDREX/STREX | -- | -- | yes | yes | yes | yes | -- | yes |
| TT (TrustZone) | -- | -- | -- | -- | -- | -- | yes | yes |
| キャッシュ操作 | -- | -- | -- | -- | -- | yes | -- | -- |

---

## 3. RISC-V (RP2350 Hazard3, ESP32-C3/C6, ESP32-P4)

### 3.1 CSR (Control and Status Registers)

CSR は `csrr` / `csrw` / `csrs` / `csrc` 命令でアクセスする専用レジスタ空間であり、
メモリマップされない。PAL では constexpr アドレスとビットフィールドマスクとして定義する。

| CSR | アドレス | 概要 | 用途 |
|-----|--------|------|------|
| mstatus | 0x300 | Machine Status | 割り込み有効化、特権モード |
| misa | 0x301 | Machine ISA | ISA 拡張情報 |
| mie | 0x304 | Machine Interrupt Enable | 個別割り込みの有効/無効 |
| mtvec | 0x305 | Machine Trap-Vector Base | トラップベクターアドレス |
| mscratch | 0x340 | Machine Scratch | トラップハンドラ用一時保存 |
| mepc | 0x341 | Machine Exception PC | 例外発生時の戻りアドレス |
| mcause | 0x342 | Machine Cause | トラップ原因コード |
| mtval | 0x343 | Machine Trap Value | トラップ付加情報 |
| mip | 0x344 | Machine Interrupt Pending | ペンディング中の割り込み |
| mcycle | 0xB00 | Machine Cycle Counter | サイクルカウンタ (DWT CYCCNT 相当) |
| minstret | 0xB02 | Machine Instructions Retired | 実行済み命令数 |

### 3.2 PMP (Physical Memory Protection) CSR

| CSR | アドレス | 概要 |
|-----|--------|------|
| pmpcfg0–3 | 0x3A0–0x3A3 | PMP 設定レジスタ (各 4 エントリ分) |
| pmpaddr0–15 | 0x3B0–0x3BF | PMP アドレスレジスタ |

### 3.3 メモリバリア・同期命令

| 命令 | 概要 |
|------|------|
| FENCE | メモリフェンス (DMB/DSB 相当) |
| FENCE.I | 命令フェンス (ISB 相当) |

### 3.4 割り込み・電力管理命令

| 命令 | 概要 |
|------|------|
| ECALL | Environment Call (SVC 相当) |
| EBREAK | ブレークポイント (BKPT 相当) |
| MRET | Machine Return from Trap |
| WFI | Wait For Interrupt |

### 3.5 アトミック命令 (A 拡張)

RV32A / RV64A 拡張が有効な場合に利用可能。

| 命令 | 概要 |
|------|------|
| LR.W | Load-Reserved Word (LDREX 相当) |
| SC.W | Store-Conditional Word (STREX 相当) |
| AMOSWAP.W | Atomic Swap |
| AMOADD.W | Atomic Add |
| AMOAND.W | Atomic AND |
| AMOOR.W | Atomic OR |
| AMOXOR.W | Atomic XOR |
| AMOMIN.W / AMOMAX.W | Atomic Min/Max (符号付き) |
| AMOMINU.W / AMOMAXU.W | Atomic Min/Max (符号無し) |

### 3.6 Hazard3 固有 CSR (RP2350)

| CSR | 概要 |
|-----|------|
| カスタム meiea | 外部割り込み配列イネーブル |
| カスタム meipa | 外部割り込み配列ペンディング |
| カスタム meipra | 外部割り込み優先度配列 |
| カスタム meinext | 次の保留割り込み |

---

## 4. Xtensa (ESP32-S3)

### 4.1 Special Registers (rsr/wsr/xsr アクセス)

Xtensa の Special Registers は `rsr` / `wsr` / `xsr` 命令でアクセスする。

| レジスタ | 番号 | 概要 |
|---------|------|------|
| PS (Processor State) | 230 | 割り込みレベル、特権モード |
| EPC1–EPC7 | 177–183 | 例外発生時の PC |
| EXCSAVE1–EXCSAVE7 | 209–215 | 例外保存レジスタ |
| CCOUNT | 234 | サイクルカウンタ |
| CCOMPARE0–2 | 240–242 | サイクルコンパレータ (タイマ) |
| INTENABLE | 228 | 割り込み有効化マスク |
| INTERRUPT | 226 | ペンディング割り込み |
| DEPC | 192 | ダブル例外 PC |
| EXCCAUSE | 232 | 例外原因コード |
| EXCVADDR | 238 | 例外仮想アドレス |
| SAR | 3 | シフトアマウントレジスタ |

### 4.2 Window Registers

Xtensa LX7 はレジスタウィンドウ方式を使用する。
ウィンドウ管理レジスタは Special Register であり、MMIO ではない。

| レジスタ | 番号 | 概要 |
|---------|------|------|
| WINDOWBASE | 72 | 現在のウィンドウベース |
| WINDOWSTART | 73 | アクティブウィンドウのビットマップ |

### 4.3 同期・電力管理命令

| 命令 | 概要 |
|------|------|
| MEMW | Memory Wait — メモリバリア |
| EXTW | External Wait — 外部バス操作完了待ち |
| DSYNC | Load/Store 同期 |
| ISYNC | 命令同期 (ISB 相当) |
| WAITI level | Wait for Interrupt at level |
| SYSCALL | システムコール (SVC 相当) |
| BREAK | ブレークポイント (BKPT 相当) |

### 4.4 MAC16 命令 (DSP)

Xtensa LX7 には 40-bit MAC (Multiply-Accumulate) ユニットがある。

| 命令グループ | 概要 |
|------------|------|
| MUL.AA.* | 16×16 乗算 |
| MULA.AA.* | 16×16 乗加算 |
| MULS.AA.* | 16×16 乗減算 |
| UMUL.AA.* | 16×16 符号無し乗算 |

### 4.5 ESP32-S3 PIE (Processor Instruction Extension)

ESP32-S3 の LX7 は Espressif 独自の SIMD 拡張 (PIE) を持つ。

| 機能 | 概要 |
|------|------|
| 128-bit SIMD | 16×8-bit / 8×16-bit / 4×32-bit 並列演算 |
| ベクタロード/ストア | 128-bit 一括転送 |
| ベクタ MAC | ベクタ乗加算 |

---

## 5. 生成ヘッダのコード例

### 5.1 ARM Cortex-M 特殊レジスタアクセス

```cpp
// pal/arch/arm/cortex_m/intrinsics.hh
#pragma once
#include <cstdint>

namespace umi::pal::arm::cortex_m {

/// @brief ARM Cortex-M core intrinsics (non-MMIO, accessed via MSR/MRS instructions)
namespace intrinsics {

    // --- 割り込み制御 ---

    /// @brief 割り込み禁止 (PRIMASK = 1)
    inline void disable_irq() {
        asm volatile("cpsid i" ::: "memory");
    }

    /// @brief 割り込み許可 (PRIMASK = 0)
    inline void enable_irq() {
        asm volatile("cpsie i" ::: "memory");
    }

    /// @brief PRIMASK 読み出し
    inline uint32_t get_primask() {
        uint32_t r;
        asm volatile("mrs %0, primask" : "=r"(r));
        return r;
    }

    /// @brief PRIMASK 書き込み
    inline void set_primask(uint32_t value) {
        asm volatile("msr primask, %0" :: "r"(value) : "memory");
    }

    /// @brief BASEPRI 読み出し (M3/M4/M7/M33 only)
    inline uint32_t get_basepri() {
        uint32_t r;
        asm volatile("mrs %0, basepri" : "=r"(r));
        return r;
    }

    /// @brief BASEPRI 書き込み (M3/M4/M7/M33 only)
    inline void set_basepri(uint32_t value) {
        asm volatile("msr basepri, %0" :: "r"(value) : "memory");
    }

    /// @brief FAULTMASK 読み出し (M3/M4/M7/M33 only)
    inline uint32_t get_faultmask() {
        uint32_t r;
        asm volatile("mrs %0, faultmask" : "=r"(r));
        return r;
    }

    /// @brief FAULTMASK 書き込み (M3/M4/M7/M33 only)
    inline void set_faultmask(uint32_t value) {
        asm volatile("msr faultmask, %0" :: "r"(value) : "memory");
    }

    /// @brief CONTROL レジスタ読み出し
    inline uint32_t get_control() {
        uint32_t r;
        asm volatile("mrs %0, control" : "=r"(r));
        return r;
    }

    /// @brief CONTROL レジスタ書き込み
    inline void set_control(uint32_t value) {
        asm volatile("msr control, %0" :: "r"(value) : "memory");
    }

    /// @brief IPSR 読み出し (割り込み番号を取得)
    inline uint32_t get_ipsr() {
        uint32_t r;
        asm volatile("mrs %0, ipsr" : "=r"(r));
        return r & 0xFF;
    }

    /// @brief ISR コンテキスト内かどうか判定
    inline bool in_isr() { return get_ipsr() != 0; }

    // --- スタックポインタ ---

    /// @brief プロセススタックポインタ読み出し
    inline uint32_t get_psp() {
        uint32_t r;
        asm volatile("mrs %0, psp" : "=r"(r));
        return r;
    }

    /// @brief プロセススタックポインタ書き込み
    inline void set_psp(uint32_t value) {
        asm volatile("msr psp, %0" :: "r"(value) : "memory");
    }

    /// @brief メインスタックポインタ読み出し
    inline uint32_t get_msp() {
        uint32_t r;
        asm volatile("mrs %0, msp" : "=r"(r));
        return r;
    }

    /// @brief メインスタックポインタ書き込み
    inline void set_msp(uint32_t value) {
        asm volatile("msr msp, %0" :: "r"(value) : "memory");
    }

    // --- メモリバリア ---

    /// @brief Data Memory Barrier
    inline void dmb() { asm volatile("dmb" ::: "memory"); }

    /// @brief Data Synchronization Barrier
    inline void dsb() { asm volatile("dsb" ::: "memory"); }

    /// @brief Instruction Synchronization Barrier
    inline void isb() { asm volatile("isb" ::: "memory"); }

    // --- パワー管理 ---

    /// @brief Wait For Interrupt
    inline void wfi() { asm volatile("wfi"); }

    /// @brief Wait For Event
    inline void wfe() { asm volatile("wfe"); }

    /// @brief Send Event
    inline void sev() { asm volatile("sev"); }

    /// @brief No Operation
    inline void nop() { asm volatile("nop"); }

    // --- ビット操作 ---

    /// @brief Count Leading Zeros
    inline uint32_t clz(uint32_t value) {
        return __builtin_clz(value);
    }

    /// @brief Reverse Bits (M3+ only)
    inline uint32_t rbit(uint32_t value) {
        uint32_t result;
        asm volatile("rbit %0, %1" : "=r"(result) : "r"(value));
        return result;
    }

    /// @brief Byte Reverse Word
    inline uint32_t rev(uint32_t value) {
        return __builtin_bswap32(value);
    }

    /// @brief Byte Reverse Half-Word
    inline uint16_t rev16(uint16_t value) {
        return __builtin_bswap16(value);
    }

    // --- 排他アクセス (M3+ only) ---

    /// @brief Exclusive Load (32-bit)
    inline uint32_t ldrex(volatile uint32_t* addr) {
        uint32_t result;
        asm volatile("ldrex %0, [%1]" : "=r"(result) : "r"(addr) : "memory");
        return result;
    }

    /// @brief Exclusive Store (32-bit), returns 0 on success
    inline uint32_t strex(uint32_t value, volatile uint32_t* addr) {
        uint32_t result;
        asm volatile("strex %0, %1, [%2]" : "=&r"(result) : "r"(value), "r"(addr) : "memory");
        return result;
    }

    /// @brief Clear Exclusive monitor
    inline void clrex() {
        asm volatile("clrex" ::: "memory");
    }

    // --- 飽和演算 (M4/M7/M33 only) ---

    /// @brief Signed Saturate
    inline int32_t ssat(int32_t value, uint32_t bit) {
        int32_t result;
        asm volatile("ssat %0, %1, %2" : "=r"(result) : "I"(bit), "r"(value));
        return result;
    }

    /// @brief Unsigned Saturate
    inline uint32_t usat(int32_t value, uint32_t bit) {
        uint32_t result;
        asm volatile("usat %0, %1, %2" : "=r"(result) : "I"(bit), "r"(value));
        return result;
    }
}

/// @brief CONTROL register field definitions
namespace control {
    constexpr uint32_t NPRIV_BIT  = 1U << 0;  // 0=privileged, 1=unprivileged (thread mode)
    constexpr uint32_t SPSEL_BIT  = 1U << 1;  // 0=MSP, 1=PSP (thread mode)
    constexpr uint32_t FPCA_BIT   = 1U << 2;  // FP context active (M4F/M7/M33)
}

/// @brief xPSR register field definitions
namespace xpsr {
    constexpr uint32_t N_BIT = 1U << 31;   // Negative
    constexpr uint32_t Z_BIT = 1U << 30;   // Zero
    constexpr uint32_t C_BIT = 1U << 29;   // Carry
    constexpr uint32_t V_BIT = 1U << 28;   // Overflow
    constexpr uint32_t Q_BIT = 1U << 27;   // Saturation (DSP)
    constexpr uint32_t GE_MASK = 0xFU << 16; // Greater-Equal flags (DSP)
    constexpr uint32_t ISR_NUMBER_MASK = 0x1FF;
}

} // namespace umi::pal::arm::cortex_m
```

### 5.2 RISC-V CSR アクセス

```cpp
// pal/arch/riscv/intrinsics.hh
#pragma once
#include <cstdint>

namespace umi::pal::riscv {

/// @brief RISC-V CSR addresses
namespace csr {
    constexpr uint16_t MSTATUS   = 0x300;
    constexpr uint16_t MISA      = 0x301;
    constexpr uint16_t MIE       = 0x304;
    constexpr uint16_t MTVEC     = 0x305;
    constexpr uint16_t MSCRATCH  = 0x340;
    constexpr uint16_t MEPC      = 0x341;
    constexpr uint16_t MCAUSE    = 0x342;
    constexpr uint16_t MTVAL     = 0x343;
    constexpr uint16_t MIP       = 0x344;
    constexpr uint16_t MCYCLE    = 0xB00;
    constexpr uint16_t MINSTRET  = 0xB02;
    constexpr uint16_t PMPCFG0   = 0x3A0;
    constexpr uint16_t PMPADDR0  = 0x3B0;
}

/// @brief MSTATUS field definitions
namespace mstatus {
    constexpr uint32_t MIE_BIT   = 1U << 3;
    constexpr uint32_t MPIE_BIT  = 1U << 7;
    constexpr uint32_t MPP_MASK  = 0x3U << 11;
    constexpr uint32_t MPRV_BIT  = 1U << 17;
}

/// @brief MCAUSE field definitions
namespace mcause {
    constexpr uint32_t INTERRUPT_BIT = 1U << 31;
    constexpr uint32_t CODE_MASK     = 0x7FFF'FFFF;
}

/// @brief MIE / MIP field definitions
namespace mie {
    constexpr uint32_t MSIE_BIT = 1U << 3;
    constexpr uint32_t MTIE_BIT = 1U << 7;
    constexpr uint32_t MEIE_BIT = 1U << 11;
}

/// @brief CSR access helpers
namespace intrinsics {
    inline uint32_t csr_read(uint16_t csr_addr) {
        uint32_t value;
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

    /// @brief FENCE (memory ordering)
    inline void fence() { asm volatile("fence" ::: "memory"); }

    /// @brief FENCE.I (instruction fence)
    inline void fence_i() { asm volatile("fence.i" ::: "memory"); }

    /// @brief WFI
    inline void wfi() { asm volatile("wfi"); }
}

} // namespace umi::pal::riscv
```

### 5.3 Xtensa Special Register アクセス

```cpp
// pal/arch/xtensa/intrinsics.hh
#pragma once
#include <cstdint>

namespace umi::pal::xtensa {

/// @brief Xtensa Special Register numbers
namespace sr {
    constexpr uint8_t SAR         = 3;
    constexpr uint8_t WINDOWBASE  = 72;
    constexpr uint8_t WINDOWSTART = 73;
    constexpr uint8_t EPC1        = 177;
    constexpr uint8_t DEPC        = 192;
    constexpr uint8_t EXCSAVE1    = 209;
    constexpr uint8_t INTERRUPT   = 226;
    constexpr uint8_t INTENABLE   = 228;
    constexpr uint8_t PS          = 230;
    constexpr uint8_t EXCCAUSE    = 232;
    constexpr uint8_t CCOUNT      = 234;
    constexpr uint8_t EXCVADDR    = 238;
    constexpr uint8_t CCOMPARE0   = 240;
}

/// @brief PS (Processor State) register field definitions
namespace ps {
    constexpr uint32_t INTLEVEL_MASK = 0xFU;       // bits 0-3
    constexpr uint32_t EXCM_BIT      = 1U << 4;    // Exception mode
    constexpr uint32_t UM_BIT        = 1U << 5;    // User mode
    constexpr uint32_t RING_MASK     = 0x3U << 6;  // Ring level
    constexpr uint32_t OWB_MASK      = 0xFU << 8;  // Old Window Base
    constexpr uint32_t CALLINC_MASK  = 0x3U << 16; // Call increment
    constexpr uint32_t WOE_BIT       = 1U << 18;   // Window Overflow Enable
}

/// @brief Special Register access and intrinsics
namespace intrinsics {
    /// @brief Read Special Register
    /// @note Actual implementation requires compile-time SR number
    inline uint32_t rsr(uint8_t sr_num) {
        uint32_t value;
        asm volatile("rsr %0, %1" : "=r"(value) : "i"(sr_num));
        return value;
    }

    /// @brief Write Special Register
    inline void wsr(uint8_t sr_num, uint32_t value) {
        asm volatile("wsr %0, %1" :: "r"(value), "i"(sr_num));
    }

    /// @brief Exchange Special Register (read old, write new)
    inline uint32_t xsr(uint8_t sr_num, uint32_t value) {
        asm volatile("xsr %0, %1" : "+r"(value) : "i"(sr_num));
        return value;
    }

    /// @brief Memory Wait (memory barrier)
    inline void memw() { asm volatile("memw" ::: "memory"); }

    /// @brief External Wait
    inline void extw() { asm volatile("extw" ::: "memory"); }

    /// @brief Data synchronization
    inline void dsync() { asm volatile("dsync" ::: "memory"); }

    /// @brief Instruction synchronization
    inline void isync() { asm volatile("isync" ::: "memory"); }

    /// @brief Wait for Interrupt at specified level
    inline void waiti(uint32_t level) {
        asm volatile("waiti %0" :: "i"(level));
    }

    /// @brief No Operation
    inline void nop() { asm volatile("nop"); }
}

} // namespace umi::pal::xtensa
```

---

## 6. C1 との境界

C1 (コアペリフェラルレジスタ) と C2 (本カテゴリ) の区別は以下の基準に従う:

| 基準 | C1 コアペリフェラル | C2 コアイントリンシクス |
|------|-------------------|---------------------|
| アクセス方法 | **MMIO** (メモリマップド I/O) | **専用命令** (MSR/MRS, csrr/csrw, rsr/wsr) |
| umimmio 表現 | Device/Block/Register/Field テンプレート | constexpr メタデータ + inline asm ラッパー |
| 代表例 (ARM) | NVIC, SCB, SysTick, DWT, ITM | PRIMASK, BASEPRI, DSB/ISB, WFI, LDREX/STREX |
| 代表例 (RISC-V) | PLIC, CLIC, mtime/mtimecmp | mstatus, mie, mcause, FENCE, LR/SC |
| 代表例 (Xtensa) | Interrupt Matrix (ESP32) | PS, CCOUNT, INTENABLE, MEMW, WAITI |

**重要**: 一部の機能は C1 と C2 の両方にまたがる。例えば、キャッシュ操作は
SCB の MMIO レジスタ (C1) への書き込みとバリア命令 (C2) の組み合わせで実現される。
このような場合、MMIO レジスタ定義は C1 に、命令ラッパーは C2 に配置し、
ドライバ層で組み合わせて使用する。

---

## 7. データソース

| アーキテクチャ | ドキュメント | 入手先 |
|-------------|------------|--------|
| ARM Cortex-M | ARMv6-M / ARMv7-M / ARMv8-M Architecture Reference Manual | ARM Developer |
| ARM Cortex-M | Cortex-M4 Technical Reference Manual (TRM) | ARM Developer |
| ARM Cortex-M | ARM Compiler armasm Reference Guide | ARM Developer |
| ARM Cortex-M | CMSIS-Core `cmsis_gcc.h` / `core_cm4.h` | github.com/ARM-software/CMSIS_5 |
| RISC-V | RISC-V Privileged Architecture Specification | riscv.org |
| RISC-V | RISC-V ISA Manual (Unprivileged) — A 拡張 | riscv.org |
| RISC-V (RP2350) | RP2350 Datasheet, Chapter 3 (Hazard3 Processor) | raspberrypi.com |
| Xtensa | Xtensa Instruction Set Architecture Reference Manual | Cadence |
| Xtensa (ESP32-S3) | ESP32-S3 TRM + PIE Programming Guide | espressif.com |
| 既存実装 | `lib/umi/port/arch/cm4/arch/cortex_m4.hh` | 本リポジトリ |
