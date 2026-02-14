# C14: デバッグ / トレース

---

## 1. 概要

デバッグ / トレースカテゴリは、MCU が提供するハードウェアデバッグ機能とトレース機能を定義する。
デバッグインターフェース (SWD, JTAG)、データウォッチポイント (DWT)、
命令トレース (ITM, ETM)、トレース出力 (SWO, TPIU) などが含まれる。

組み込みオーディオ開発では、リアルタイム処理のプロファイリング (DWT サイクルカウンタ)、
非侵入型ログ出力 (ITM/RTT)、低レイテンシのデバッグが重要な要件である。
PAL レイヤでは、これらのデバッグハードウェアを umimmio の型システムで表現する。

**PAL レイヤとの対応**:

| レイヤ | 含まれる定義 |
|--------|------------|
| L2 (コアプロファイル固有) | DWT, ITM, TPIU, CoreDebug, ETM — コアバリアントで有無が変化 |
| L3 (MCU 固有) | デバッグピン割り当て、SWO クロック設定、トレースバッファ |
| L4 (デバイス固有) | デバッグインターフェースの物理ピン配置 |

---

## 2. 構成要素

### 2.1 デバッグインターフェース

| インターフェース | プロトコル | ピン数 | 概要 |
|----------------|----------|--------|------|
| JTAG | IEEE 1149.1 | 5 (TCK, TMS, TDI, TDO, TRST) | 標準デバッグ、デイジーチェーン対応 |
| SWD | ARM Serial Wire Debug | 2 (SWCLK, SWDIO) | ARM 専用、省ピン、SWO 追加可能 |
| SWD + SWO | SWD + Serial Wire Output | 3 (SWCLK, SWDIO, SWO) | SWD + トレース出力 |
| cJTAG | IEEE 1149.7 | 2 | 2 ピン JTAG (一部 MCU) |
| USB-Serial-JTAG | USB CDC + JTAG | USB D+/D- | ESP32 シリーズ内蔵 |

### 2.2 DWT (Data Watchpoint and Trace)

ARM Cortex-M3/M4/M7/M33 に搭載されるデータウォッチポイントとトレースユニット。

| 機能 | 概要 | 主な用途 |
|------|------|---------|
| CYCCNT | CPU サイクルカウンタ (32-bit) | 処理時間計測、プロファイリング |
| CPICNT / EXCCNT / SLEEPCNT / LSUCNT / FOLDCNT | パフォーマンスカウンタ | 詳細プロファイリング |
| Comparator + Watchpoint | データアドレス監視 | メモリ破壊検出、変数監視 |
| PC sampling | PC サンプリング (ITM 経由で出力) | 統計的プロファイリング |

### 2.3 ITM (Instrumentation Trace Macrocell)

ソフトウェア計装によるトレース出力。Stimulus Port にデータを書き込むと SWO から出力される。

| 機能 | 概要 |
|------|------|
| Stimulus Ports 0-31 | 32 個の出力チャネル (各 32-bit) |
| Hardware trace | DWT イベント、例外トレースの自動出力 |
| Timestamps | タイムスタンプパケット |

### 2.4 TPIU (Trace Port Interface Unit)

トレースデータを外部に出力するインターフェース。SWO (1 ピン) または
Trace Port (4-bit パラレル + クロック) で出力する。

### 2.5 ETM (Embedded Trace Macrocell)

命令フローのハードウェアトレース。プログラムの実行パスをリアルタイムで記録する。
一部の高性能コア (Cortex-M7, M33) に搭載。

### 2.6 CoreDebug

デバッガとの通信を制御するレジスタ群 (DHCSR, DCRSR, DCRDR, DEMCR)。
ブレークポイント、シングルステップ、デバッグ例外の制御を行う。

---

## 3. プラットフォーム差異

| プラットフォーム | デバッグ IF | DWT (CYCCNT) | ITM | ETM | SWO | 特記事項 |
|----------------|-----------|-------------|-----|-----|-----|---------|
| STM32F4 (M4F) | SWD + JTAG | あり (フル版) | あり | あり (一部) | あり | SWO は PB3 (デフォルト) |
| STM32H7 (M7) | SWD + JTAG | あり (フル版) | あり | あり | あり | ETB (Embedded Trace Buffer) 搭載 |
| RP2040 (M0+) | SWD (各コア独立) | 簡易版のみ (CYCCNT なし) | なし | なし | なし | デュアルコア各コアに SWD AP |
| RP2350 (M33/Hazard3) | SWD (ARM) / JTAG (RISC-V) | あり (ARM M33) | あり (ARM M33) | なし | あり | ARM/RISC-V でデバッグ体験が異なる |
| ESP32-S3 (Xtensa) | USB-Serial-JTAG / JTAG | なし (CCOUNT で代替) | なし | なし | なし | USB-Serial-JTAG が内蔵、OpenOCD 対応 |
| ESP32-P4 (RISC-V) | USB-Serial-JTAG / JTAG | なし (mcycle CSR で代替) | なし | なし | なし | RISC-V Debug Module |
| i.MX RT (M7) | SWD + JTAG | あり (フル版) | あり | あり | あり | SWJ-DP (SWD/JTAG 兼用ポート) |

---

## 4. 生成ヘッダのコード例

### 4.1 DWT (Cortex-M4F フル版)

```cpp
// pal/core/cortex_m4f/dwt.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::arm::cortex_m4f {

namespace mm = umi::mmio;

/// @brief Data Watchpoint and Trace unit (フル版: M3/M4/M7)
/// @note サイクルカウンタ (CYCCNT) によるマイクロ秒精度の処理時間計測が可能
struct Dwt : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0xE000'1000;

    /// @brief DWT 制御レジスタ
    struct CTRL : mm::Register<Dwt, 0x00, 32> {
        struct CYCCNTENA   : mm::Field<CTRL, 0,  1> {};  // サイクルカウンタ有効化
        struct POSTPRESET  : mm::Field<CTRL, 1,  4> {};  // ポストスケーラプリセット
        struct POSTCNT     : mm::Field<CTRL, 5,  4> {};  // ポストスケーラカウンタ
        struct CYCTAP      : mm::Field<CTRL, 9,  1> {};  // サイクルタップ選択
        struct SYNCTAP     : mm::Field<CTRL, 10, 2> {};  // 同期タップ選択
        struct PCSAMPLENA  : mm::Field<CTRL, 12, 1> {};  // PC サンプリング有効化
        struct EXCTRCENA   : mm::Field<CTRL, 16, 1> {};  // 例外トレース有効化
        struct CPIEVTENA   : mm::Field<CTRL, 17, 1> {};  // CPI イベント有効化
        struct EXCEVTENA   : mm::Field<CTRL, 18, 1> {};  // 例外オーバーヘッド有効化
        struct SLEEPEVTENA : mm::Field<CTRL, 19, 1> {};  // スリープイベント有効化
        struct LSUEVTENA   : mm::Field<CTRL, 20, 1> {};  // LSU イベント有効化
        struct FOLDEVTENA  : mm::Field<CTRL, 21, 1> {};  // 命令フォールド有効化
        struct CYCEVTENA   : mm::Field<CTRL, 22, 1> {};  // サイクルイベント有効化
        struct NUMCOMP     : mm::Field<CTRL, 28, 4, mm::RO> {};  // コンパレータ数 (読み取り専用)
    };

    /// @brief サイクルカウンタ (32-bit、オーバーフローでラップ)
    struct CYCCNT : mm::Register<Dwt, 0x04, 32> {};

    /// @brief パフォーマンスカウンタ (各 8-bit、オーバーフローで ITM に出力)
    struct CPICNT  : mm::Register<Dwt, 0x08, 32> {};
    struct EXCCNT  : mm::Register<Dwt, 0x0C, 32> {};
    struct SLEEPCNT: mm::Register<Dwt, 0x10, 32> {};
    struct LSUCNT  : mm::Register<Dwt, 0x14, 32> {};
    struct FOLDCNT : mm::Register<Dwt, 0x18, 32> {};

    /// @brief PC サンプルレジスタ (読み取り専用)
    struct PCSR : mm::Register<Dwt, 0x1C, 32, mm::RO> {};

    /// @brief コンパレータ 0 (データウォッチポイント)
    struct COMP0     : mm::Register<Dwt, 0x20, 32> {};
    struct MASK0     : mm::Register<Dwt, 0x24, 32> {};
    struct FUNCTION0 : mm::Register<Dwt, 0x28, 32> {
        struct FUNCTION  : mm::Field<FUNCTION0, 0, 4> {};
        struct EMITRANGE : mm::Field<FUNCTION0, 5, 1> {};
        struct DATAVMATCH: mm::Field<FUNCTION0, 8, 1> {};
        struct MATCHED   : mm::Field<FUNCTION0, 24, 1, mm::RO> {};
    };

    /// @brief コンパレータ 1
    struct COMP1     : mm::Register<Dwt, 0x30, 32> {};
    struct MASK1     : mm::Register<Dwt, 0x34, 32> {};
    struct FUNCTION1 : mm::Register<Dwt, 0x38, 32> {};

    /// @brief コンパレータ 2
    struct COMP2     : mm::Register<Dwt, 0x40, 32> {};
    struct MASK2     : mm::Register<Dwt, 0x44, 32> {};
    struct FUNCTION2 : mm::Register<Dwt, 0x48, 32> {};

    /// @brief コンパレータ 3
    struct COMP3     : mm::Register<Dwt, 0x50, 32> {};
    struct MASK3     : mm::Register<Dwt, 0x54, 32> {};
    struct FUNCTION3 : mm::Register<Dwt, 0x58, 32> {};
};

/// @brief DWT メモリブロック (0xE000'1000)
using DWT = Dwt;

} // namespace umi::pal::arm::cortex_m4f
```

### 4.2 ITM (Instrumentation Trace Macrocell)

```cpp
// pal/core/cortex_m4f/itm.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::arm::cortex_m4f {

namespace mm = umi::mmio;

/// @brief Instrumentation Trace Macrocell
/// @note ソフトウェアトレースの出力先。Stimulus Port に書き込むと SWO から出力される
struct Itm : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0xE000'0000;

    // ---- Stimulus Ports 0-31 (書き込みでトレースデータ送出) ----
    struct STIM0  : mm::Register<Itm, 0x000, 32, mm::WO> {};
    struct STIM1  : mm::Register<Itm, 0x004, 32, mm::WO> {};
    struct STIM2  : mm::Register<Itm, 0x008, 32, mm::WO> {};
    struct STIM3  : mm::Register<Itm, 0x00C, 32, mm::WO> {};
    struct STIM4  : mm::Register<Itm, 0x010, 32, mm::WO> {};
    struct STIM5  : mm::Register<Itm, 0x014, 32, mm::WO> {};
    struct STIM6  : mm::Register<Itm, 0x018, 32, mm::WO> {};
    struct STIM7  : mm::Register<Itm, 0x01C, 32, mm::WO> {};
    // STIM8-STIM31 は同様のパターンで 0x020-0x07C に配置

    // ---- 制御レジスタ ----

    /// @brief Trace Enable Register -- 各 Stimulus Port の有効/無効 (ビットマスク)
    struct TER : mm::Register<Itm, 0xE00, 32> {};

    /// @brief Trace Privilege Register -- 非特権モードからのアクセス許可
    struct TPR : mm::Register<Itm, 0xE40, 32> {
        struct PRIVMASK : mm::Field<TPR, 0, 4> {}; // ポートグループ 0-3 の特権設定
    };

    /// @brief Trace Control Register -- ITM 全体の有効化と設定
    struct TCR : mm::Register<Itm, 0xE80, 32> {
        struct ITMENA    : mm::Field<TCR, 0,  1> {};  // ITM 有効化
        struct TSENA     : mm::Field<TCR, 1,  1> {};  // タイムスタンプ有効化
        struct SYNCENA   : mm::Field<TCR, 2,  1> {};  // 同期パケット有効化
        struct DWTENA    : mm::Field<TCR, 3,  1> {};  // DWT パケットフォワード有効化
        struct SWOENA    : mm::Field<TCR, 4,  1> {};  // SWO 出力有効化
        struct TSPRESCALE: mm::Field<TCR, 8,  2> {};  // タイムスタンププリスケーラ
        struct ATBID     : mm::Field<TCR, 16, 7> {};  // ATB ID (トレースバス識別子)
        struct BUSY      : mm::Field<TCR, 23, 1, mm::RO> {};  // ITM ビジーフラグ
    };

    /// @brief Lock Access Register -- ロック解除キーの書き込み先
    struct LAR : mm::Register<Itm, 0xFB0, 32, mm::WO> {};

    /// @brief Lock Status Register
    struct LSR : mm::Register<Itm, 0xFB4, 32, mm::RO> {
        struct PRESENT   : mm::Field<LSR, 0, 1> {}; // ロック機構の有無
        struct ACCESS    : mm::Field<LSR, 1, 1> {}; // 書き込みアクセス許可
        struct BYTEACC   : mm::Field<LSR, 2, 1> {}; // バイトアクセス対応
    };
};

/// @brief ITM メモリブロック (0xE000'0000)
using ITM = Itm;

/// @brief ITM ロック解除キー
/// LAR にこの値を書き込むとレジスタへの書き込みが許可される
constexpr uint32_t itm_unlock_key = 0xC5AC'CE55;

} // namespace umi::pal::arm::cortex_m4f
```

### 4.3 TPIU (Trace Port Interface Unit)

```cpp
// pal/core/cortex_m4f/tpiu.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::arm::cortex_m4f {

namespace mm = umi::mmio;

/// @brief Trace Port Interface Unit -- トレースデータの外部出力制御
struct Tpiu : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0xE004'0000;

    /// @brief Supported Port Sizes Register
    struct SSPSR : mm::Register<Tpiu, 0x000, 32, mm::RO> {};

    /// @brief Current Port Size Register
    struct CSPSR : mm::Register<Tpiu, 0x004, 32> {};

    /// @brief Async Clock Prescaler Register -- SWO ボーレート設定
    struct ACPR : mm::Register<Tpiu, 0x010, 32> {
        struct PRESCALER : mm::Field<ACPR, 0, 13> {};
        // SWO frequency = TRACECLK / (PRESCALER + 1)
    };

    /// @brief Selected Pin Protocol Register -- 出力プロトコル選択
    struct SPPR : mm::Register<Tpiu, 0x0F0, 32> {
        struct TXMODE : mm::Field<SPPR, 0, 2> {};
        // 0: Sync Trace Port (パラレル)
        // 1: Manchester encoding (SWO)
        // 2: NRZ encoding (SWO, UART 互換)
    };

    /// @brief Formatter and Flush Status Register
    struct FFSR : mm::Register<Tpiu, 0x300, 32, mm::RO> {};

    /// @brief Formatter and Flush Control Register
    struct FFCR : mm::Register<Tpiu, 0x304, 32> {
        struct ENFCONT  : mm::Field<FFCR, 1, 1> {}; // 連続フォーマッタ有効化
        struct TRIGIN   : mm::Field<FFCR, 8, 1> {}; // トリガ入力有効化
    };
};

/// @brief TPIU メモリブロック (0xE004'0000)
using TPIU = Tpiu;

} // namespace umi::pal::arm::cortex_m4f
```

### 4.4 CoreDebug

```cpp
// pal/core/cortex_m4f/core_debug.hh
#pragma once
#include <umimmio/register.hh>

namespace umi::pal::arm::cortex_m4f {

namespace mm = umi::mmio;

/// @brief Core Debug レジスタ -- デバッガ制御
struct CoreDbg : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0xE000'EDF0;

    /// @brief Debug Halting Control and Status Register
    struct DHCSR : mm::Register<CoreDbg, 0x00, 32> {
        struct C_DEBUGEN  : mm::Field<DHCSR, 0,  1> {}; // デバッグ有効化
        struct C_HALT     : mm::Field<DHCSR, 1,  1> {}; // CPU 停止
        struct C_STEP     : mm::Field<DHCSR, 2,  1> {}; // シングルステップ
        struct C_MASKINTS : mm::Field<DHCSR, 3,  1> {}; // 割り込みマスク
        struct S_REGRDY   : mm::Field<DHCSR, 16, 1, mm::RO> {};  // レジスタ転送完了
        struct S_HALT     : mm::Field<DHCSR, 17, 1, mm::RO> {};  // 停止状態
        struct S_SLEEP    : mm::Field<DHCSR, 18, 1, mm::RO> {};  // スリープ状態
        struct S_LOCKUP   : mm::Field<DHCSR, 19, 1, mm::RO> {};  // ロックアップ状態
        struct S_RETIRE_ST: mm::Field<DHCSR, 24, 1, mm::RO> {};  // 命令完了
        struct S_RESET_ST : mm::Field<DHCSR, 25, 1, mm::RO> {};  // リセット発生
        struct DBGKEY     : mm::Field<DHCSR, 16, 16> {}; // 書き込みキー (0xA05F)
    };

    /// @brief Debug Core Register Selector Register
    struct DCRSR : mm::Register<CoreDbg, 0x04, 32, mm::WO> {};

    /// @brief Debug Core Register Data Register
    struct DCRDR : mm::Register<CoreDbg, 0x08, 32> {};

    /// @brief Debug Exception and Monitor Control Register
    struct DEMCR : mm::Register<CoreDbg, 0x0C, 32> {
        struct VC_CORERESET : mm::Field<DEMCR, 0,  1> {};  // リセットベクターキャッチ
        struct VC_MMERR     : mm::Field<DEMCR, 4,  1> {};  // MemManage fault キャッチ
        struct VC_NOCPERR   : mm::Field<DEMCR, 5,  1> {};  // コプロセッサ fault キャッチ
        struct VC_CHKERR    : mm::Field<DEMCR, 6,  1> {};  // チェック fault キャッチ
        struct VC_STATERR   : mm::Field<DEMCR, 7,  1> {};  // ステート fault キャッチ
        struct VC_BUSERR    : mm::Field<DEMCR, 8,  1> {};  // バス fault キャッチ
        struct VC_INTERR    : mm::Field<DEMCR, 9,  1> {};  // 割り込み fault キャッチ
        struct VC_HARDERR   : mm::Field<DEMCR, 10, 1> {};  // HardFault キャッチ
        struct MON_EN       : mm::Field<DEMCR, 16, 1> {};  // デバッグモニタ有効化
        struct MON_PEND     : mm::Field<DEMCR, 17, 1> {};  // モニタ保留
        struct MON_STEP     : mm::Field<DEMCR, 18, 1> {};  // モニタステップ
        struct MON_REQ      : mm::Field<DEMCR, 19, 1> {};  // モニタリクエスト
        struct TRCENA       : mm::Field<DEMCR, 24, 1> {};  // トレース有効化 (DWT/ITM/TPIU)
    };
};

/// @brief CoreDebug メモリブロック (0xE000'EDF0)
using COREDEBUG = CoreDbg;

/// @brief DHCSR 書き込みキー (上位 16 ビットに設定)
constexpr uint32_t dbg_key = 0xA05F'0000;

} // namespace umi::pal::arm::cortex_m4f
```

### 4.5 RP2040 デバッグ (SWD デュアルコア)

RP2040 はデュアルコア Cortex-M0+ であり、各コアに独立した SWD Access Port が存在する。
DWT は簡易版 (CYCCNT なし) であり、ITM/ETM は非搭載。

```cpp
// pal/mcu/rp2040/debug.hh
#pragma once
#include <cstdint>

namespace umi::pal::rp2040::debug {

/// @brief SWD 構成 -- デュアルコア対応
namespace swd {
    constexpr uint8_t access_port_count = 2;  // Core 0 AP + Core 1 AP
    constexpr uint8_t core0_ap = 0;           // Access Port 0 → Core 0
    constexpr uint8_t core1_ap = 1;           // Access Port 1 → Core 1
    constexpr uint8_t rescue_dp = 1;          // Rescue DP (ロックアウト復旧用)
}

/// @brief DWT 簡易版 (Cortex-M0+)
/// @note CYCCNT は非搭載。コンパレータのみ利用可能 (実装依存)
namespace dwt {
    constexpr bool has_cyccnt = false;        // M0+ には CYCCNT がない
    constexpr uint8_t comparator_count = 2;   // RP2040: 2 コンパレータ (ウォッチポイント)
}

/// @brief デバッグピン (デフォルト割り当て)
namespace pins {
    constexpr uint8_t swclk = 0; // GPIO0 (専用ピン、GPIO 兼用ではない)
    constexpr uint8_t swdio = 0; // GPIO0 (専用ピン)
    // RP2040 の SWD ピンは専用端子であり GPIO と共有されない
}

/// @brief サイクルカウント代替手段
/// RP2040 では SysTick または DMA pacing timer を代替として使用
namespace profiling {
    constexpr bool systick_based = true;  // SysTick でおおよその時間計測が可能
}

} // namespace umi::pal::rp2040::debug
```

### 4.6 ESP32-S3 デバッグ (USB-Serial-JTAG)

ESP32-S3 は USB-Serial-JTAG コントローラを内蔵し、USB ケーブルのみでデバッグ可能。
ARM のトレースインフラ (DWT/ITM/SWO) は存在しないため、代替手段を使用する。

```cpp
// pal/mcu/esp32s3/debug.hh
#pragma once
#include <cstdint>

namespace umi::pal::esp32s3::debug {

/// @brief USB-Serial-JTAG コントローラ
/// @note 内蔵 USB PHY を使用し、追加ハードウェアなしでデバッグ可能
namespace usb_serial_jtag {
    constexpr bool available = true;
    constexpr uint32_t base_addr = 0x6004'3000;
    // USB CDC (シリアルコンソール) と JTAG を同時に提供
    // OpenOCD 経由でデバッグ可能
}

/// @brief 外部 JTAG (USB-Serial-JTAG と排他)
namespace jtag {
    constexpr uint8_t tms_gpio = 42;
    constexpr uint8_t tdi_gpio = 41;
    constexpr uint8_t tdo_gpio = 40;
    constexpr uint8_t tck_gpio = 39;
}

/// @brief サイクルカウント (Xtensa CCOUNT レジスタ)
/// @note ARM DWT CYCCNT の代替。Special Register 234 を rsr 命令で読み取る
namespace profiling {
    constexpr uint16_t ccount_sr = 234;      // Xtensa CCOUNT special register
    constexpr uint16_t ccompare0_sr = 240;   // Cycle comparator 0
    constexpr uint16_t ccompare1_sr = 241;
    constexpr uint16_t ccompare2_sr = 242;
    // CCOUNT は CPU クロックでインクリメントされる 32-bit カウンタ
}

/// @brief トレース代替手段
/// ESP32-S3 には ITM/SWO がないため、以下の方法でトレースを行う:
/// - App Trace (JTAG 経由の TRAX トレース、ESP-IDF 提供)
/// - SystemView (SEGGER SystemView 対応、FreeRTOS トレース)
/// - Log output (UART / USB-CDC 経由のログ出力)

} // namespace umi::pal::esp32s3::debug
```

---

## 5. データソース

| プラットフォーム | ドキュメント | 入手先 |
|----------------|------------|--------|
| ARM Cortex-M | ARMv7-M Architecture Reference Manual — Chapter: Debug architecture | ARM Developer |
| ARM Cortex-M | Cortex-M4 Technical Reference Manual — DWT, ITM, TPIU, CoreDebug | ARM Developer |
| ARM Cortex-M | Cortex-M7 TRM — ETM, ETB (Embedded Trace Buffer) | ARM Developer |
| ARM Cortex-M | CoreSight Architecture Specification — Trace components | ARM Developer |
| STM32F4 | STM32F4xx Reference Manual (RM0090) — Chapter: Debug support (DBG) | st.com |
| STM32F4 | AN4989: STM32 microcontroller debug toolbox | st.com |
| RP2040 | RP2040 Datasheet — Chapter 2.3.6: Debug, SWD | raspberrypi.com |
| RP2350 | RP2350 Datasheet — Debug chapter | raspberrypi.com |
| ESP32-S3 | ESP32-S3 Technical Reference Manual — Chapter: USB-Serial-JTAG Controller | espressif.com |
| ESP32-S3 | ESP-IDF JTAG Debugging Guide | docs.espressif.com |
| ESP32-P4 | ESP32-P4 Technical Reference Manual — Debug chapter | espressif.com |
| i.MX RT | i.MX RT1060 Reference Manual — Chapter: Debug | nxp.com |

**注記**: ARM のデバッグ / トレースインフラは CoreSight アーキテクチャに基づいており、
コアペリフェラル (C01) と重複する部分がある。本カテゴリではデバッグ用途に特化した
レジスタ定義と使用方法に焦点を当て、C01 ではコアペリフェラルとしての分類と
アーキテクチャ差異に焦点を当てる。

DWT のサイクルカウンタは umibench (ベンチマークライブラリ) の基盤であり、
プラットフォーム間で CYCCNT / CCOUNT / mcycle の差異を吸収する抽象化が
HAL 層で必要となる。
