# C13: リンカ / スタートアップ

---

## 1. 概要

リンカスクリプトとスタートアップコードは、MCU のメモリマップに基づいてプログラムの配置と
初期化を行う低レイヤコンポーネントである。デバイスごとに異なるフラッシュ / SRAM アドレスと
サイズ、ブートシーケンス、ベクターテーブルの配置を定義する。

PAL レイヤではこれらの情報を constexpr メタデータとして定義し、
テンプレートからリンカスクリプトとスタートアップコードを生成する。

**PAL レイヤとの対応**:

| レイヤ | 含まれる定義 |
|--------|------------|
| L1 (アーキテクチャ共通) | C ランタイム初期化 (.data コピー, .bss ゼロクリア) |
| L2 (コアプロファイル固有) | ベクターテーブル構造、スタックポインタ初期化 |
| L3 (MCU 固有) | ブートシーケンス (Boot2, ROM bootloader 等) |
| L4 (デバイス固有) | メモリリージョン定義 (アドレス、サイズ) |

---

## 2. 構成要素

### 2.1 メモリリージョン定義

リンカスクリプトの `MEMORY` ブロックで定義されるメモリ領域。
各リージョンのアドレス、サイズ、アクセス属性 (rx, rw, rwx) を指定する。

| リージョン | 用途 | 属性 |
|-----------|------|------|
| FLASH | プログラムコード、定数データ | rx |
| SRAM | 変数、スタック、ヒープ | rwx |
| CCM / DTCM / ITCM | 高速メモリ (CPU 直結) | rw / rwx |
| Backup SRAM | スリープ中も保持される領域 | rw |

### 2.2 セクション配置

リンカスクリプトの `SECTIONS` ブロックで定義されるセクション配置規則。

| セクション | 内容 | 配置先 |
|-----------|------|--------|
| .isr_vector | 割り込みベクターテーブル | FLASH 先頭 |
| .text | プログラムコード | FLASH |
| .rodata | 読み取り専用データ、定数 | FLASH |
| .data | 初期値付きグローバル変数 | SRAM (FLASH からコピー) |
| .bss | ゼロ初期化グローバル変数 | SRAM |
| .stack | スタック領域 | SRAM 末尾 |
| .heap | ヒープ領域 | SRAM (.bss と .stack の間) |

### 2.3 スタック / ヒープ

スタックサイズとヒープサイズはデバイスの SRAM 容量とアプリケーション要件に応じて設定する。
組み込みオーディオでは、割り込みスタックとメインスタックを分離する場合がある。

### 2.4 C ランタイム初期化

スタートアップコードは以下の処理を実行する:

1. スタックポインタの初期化 (ベクターテーブル先頭から自動ロード)
2. .data セクションを FLASH から SRAM へコピー
3. .bss セクションをゼロクリア
4. FPU の有効化 (FPU 搭載コアの場合)
5. `__libc_init_array()` 呼び出し (グローバルコンストラクタ)
6. `main()` 呼び出し

### 2.5 ベクターテーブル配置

Cortex-M ではベクターテーブルの先頭エントリがスタックポインタ初期値、
2 番目がリセットハンドラのアドレスである。VTOR レジスタでテーブル位置を変更可能。

---

## 3. プラットフォーム差異

| プラットフォーム | ブートシーケンス | メモリ特性 | 特記事項 |
|----------------|----------------|-----------|---------|
| STM32F4 | ベクターテーブル→Reset_Handler→C 初期化→main | FLASH + SRAM + CCM | BOOT0/BOOT1 ピンでブートソース選択 |
| STM32H7 | ベクターテーブル→Reset_Handler→C 初期化→main | FLASH + DTCM + ITCM + SRAM (複数) | FlexRAM 的な DTCM/ITCM + AXI SRAM |
| RP2040 | ROM bootloader→Boot2 (256B)→Flash XIP→ベクターテーブル | 外部 Flash (XIP) + SRAM (6 バンク) | Boot2 が QSPI フラッシュを XIP 用に初期化 |
| RP2350 | ROM bootloader→Boot2→Flash/SRAM→ベクターテーブル | 外部 Flash (XIP) + SRAM + PSRAM | ARM/RISC-V デュアルアーキ、OTP でブート設定 |
| ESP32-S3 | ROM bootloader→2nd stage bootloader→app | 外部 Flash (XIP) + 内部 SRAM + PSRAM | パーティションテーブルベースのマルチ app 管理 |
| ESP32-P4 | ROM bootloader→2nd stage bootloader→app | HP SRAM + LP SRAM + PSRAM | HP/LP コア独立ブート |
| i.MX RT | ROM bootloader→FlexSPI boot→IVT→app | FlexSPI Flash (XIP) + FlexRAM | IVT (Image Vector Table) + DCD (Device Configuration Data) |

---

## 4. 生成ヘッダのコード例

### 4.1 STM32F407VG リンカスクリプト

デバイスメタデータ (C11) から自動生成されるリンカスクリプト。

```ld
/* pal/device/stm32f407vg/linker.ld -- C11 メタデータから生成 */
MEMORY {
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    SRAM1 (rwx) : ORIGIN = 0x20000000, LENGTH = 112K
    SRAM2 (rwx) : ORIGIN = 0x2001C000, LENGTH = 16K
    CCM   (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
    BKPSRAM (rw): ORIGIN = 0x40024000, LENGTH = 4K
}

/* スタック / ヒープサイズ (カスタマイズ可能) */
_stack_size = 8K;
_heap_size  = 4K;

/* エントリポイント */
ENTRY(Reset_Handler)

SECTIONS {
    /* ベクターテーブル -- FLASH 先頭に配置 */
    .isr_vector : {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /* プログラムコード */
    .text : {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* 読み取り専用データ */
    .rodata : {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* C++ コンストラクタ / デストラクタテーブル */
    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH
    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    _sidata = LOADADDR(.data);

    /* 初期値付きデータ -- SRAM に配置、FLASH から実行時コピー */
    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM1 AT> FLASH

    /* ゼロ初期化データ */
    .bss : {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > SRAM1

    /* CCM -- DMA アクセス不可、CPU 専用高速メモリ */
    .ccm (NOLOAD) : {
        . = ALIGN(4);
        *(.ccm)
        *(.ccm*)
        . = ALIGN(4);
    } > CCM

    /* スタック -- SRAM1 末尾 */
    ._stack (NOLOAD) : {
        . = ALIGN(8);
        _end_stack = . + _stack_size;
    } > SRAM1
}
```

### 4.2 STM32 Cortex-M4F スタートアップコード

```cpp
// pal/arch/arm/cortex_m4f/startup.cc -- 生成テンプレート
#include <cstdint>

// リンカスクリプトから供給されるシンボル
extern uint32_t _sidata;  // .data LMA (FLASH 上のアドレス)
extern uint32_t _sdata;   // .data VMA 開始 (SRAM)
extern uint32_t _edata;   // .data VMA 終了
extern uint32_t _sbss;    // .bss 開始
extern uint32_t _ebss;    // .bss 終了
extern uint32_t _end_stack; // スタック末尾

// C++ グローバルコンストラクタ
extern "C" void __libc_init_array();
// ユーザーエントリポイント
extern "C" int main();

/// @brief リセットハンドラ -- 電源投入 / リセット時に最初に実行される関数
extern "C" [[noreturn]] void Reset_Handler() {
    // 1. FPU 有効化 (Cortex-M4F: CP10/CP11 をフルアクセスに設定)
    // SCB->CPACR |= (0xF << 20)
    *reinterpret_cast<volatile uint32_t*>(0xE000'ED88) |= (0xFU << 20);

    // 2. .data セクションを FLASH から SRAM へコピー
    {
        uint32_t* src = &_sidata;
        uint32_t* dst = &_sdata;
        while (dst < &_edata) {
            *dst++ = *src++;
        }
    }

    // 3. .bss セクションをゼロクリア
    {
        uint32_t* dst = &_sbss;
        while (dst < &_ebss) {
            *dst++ = 0;
        }
    }

    // 4. C++ グローバルコンストラクタ呼び出し
    __libc_init_array();

    // 5. main() 呼び出し
    main();

    // main() から戻った場合は無限ループ
    while (true) {
        __asm__ volatile("wfi");
    }
}

/// @brief デフォルト割り込みハンドラ (弱シンボル)
extern "C" [[noreturn]] void Default_Handler() {
    while (true) {
        __asm__ volatile("bkpt #0");
    }
}

// ベクターテーブル (FLASH 先頭に配置)
// 実際のベクターテーブルは C03 (割り込みテーブル) で定義
```

### 4.3 RP2040 Boot Stage 2

RP2040 では、ROM ブートローダが QSPI フラッシュの先頭 256 バイト (Boot2) を
SRAM にコピーして実行する。Boot2 は QSPI フラッシュを XIP (Execute-In-Place) 用に初期化する。

```cpp
// pal/mcu/rp2040/boot2.hh
#pragma once
#include <cstdint>

namespace umi::pal::rp2040::boot2 {

/// @brief Boot Stage 2 のメタデータ
constexpr uint32_t boot2_size = 256;           // 256 バイト固定
constexpr uint32_t boot2_entry_offset = 0x00;  // エントリポイントオフセット

/// @brief Boot2 末尾の CRC32 チェック
/// ROM ブートローダが Boot2 の CRC32 を検証し、一致しなければ USB ブートモードに入る
constexpr uint32_t crc32_offset = 252;         // 最終 4 バイト

/// @brief Boot2 が実行される SRAM アドレス
constexpr uint32_t sram_exec_addr = 0x2000'0000;

/// @brief Boot2 実行後、アプリケーションのベクターテーブルは flash + 256 に配置
constexpr uint32_t app_vector_table_offset = 256;

/// @brief XIP (Execute-In-Place) ベースアドレス
/// Boot2 が QSPI フラッシュを XIP 用に設定した後、このアドレスからフラッシュを読める
constexpr uint32_t xip_base = 0x1000'0000;

/// @brief サポートされるフラッシュ種別
/// Boot2 の実装はフラッシュチップに依存する (W25Q, IS25LP 等)
/// Pico SDK では boot2_w25q080.S がデフォルト

} // namespace umi::pal::rp2040::boot2
```

### 4.4 RP2040 リンカスクリプト (メモリ定義)

```ld
/* pal/device/rp2040/linker.ld -- RP2040 メモリ定義 */
MEMORY {
    /* Boot2 領域 (256B) -- QSPI フラッシュ先頭 */
    BOOT2 (rx) : ORIGIN = 0x10000000, LENGTH = 256

    /* アプリケーション FLASH (XIP) -- Boot2 直後 */
    FLASH (rx)  : ORIGIN = 0x10000100, LENGTH = 2048K - 256

    /* SRAM -- 6 バンク、264KB 合計 */
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 264K

    /* SRAM Bank 4 & 5 -- スクラッチメモリ (コア間で分離可能) */
    SCRATCH_X (rwx) : ORIGIN = 0x20040000, LENGTH = 4K
    SCRATCH_Y (rwx) : ORIGIN = 0x20041000, LENGTH = 4K
}
```

### 4.5 ESP32-S3 ブートシーケンス (概要)

ESP32-S3 は ROM ブートローダ + 2nd stage bootloader の 2 段階ブートを使用する。
リンカスクリプトは ESP-IDF のビルドシステムが自動生成するため、
PAL では参照情報としてメモリマップを定義する。

```cpp
// pal/mcu/esp32s3/boot.hh
#pragma once
#include <cstdint>

namespace umi::pal::esp32s3::boot {

/// @brief ブートシーケンス
/// 1. ROM bootloader (マスク ROM、変更不可)
///    - eFuse / GPIO ストラッピングピンからブートモード判定
///    - フラッシュから 2nd stage bootloader をロード
/// 2. 2nd stage bootloader (フラッシュ上、ESP-IDF 提供)
///    - パーティションテーブル読み込み
///    - OTA (Over-The-Air) スロット選択
///    - アプリケーションイメージ検証 (Secure Boot 有効時)
///    - アプリケーションをロードして実行
/// 3. Application startup
///    - ESP-IDF の初期化 (メモリ、キャッシュ、ペリフェラル)
///    - app_main() 呼び出し

/// @brief メモリマップ (アプリケーションから見たアドレス空間)
namespace memory_map {
    constexpr uint32_t irom_base  = 0x4200'0000; // 命令キャッシュ経由のフラッシュ
    constexpr uint32_t drom_base  = 0x3C00'0000; // データキャッシュ経由のフラッシュ
    constexpr uint32_t iram_base  = 0x4037'0000; // 内部命令 SRAM
    constexpr uint32_t dram_base  = 0x3FC8'0000; // 内部データ SRAM
    constexpr uint32_t rtc_fast   = 0x600F'E000; // RTC FAST メモリ
    constexpr uint32_t rtc_slow   = 0x5000'0000; // RTC SLOW メモリ
}

/// @brief パーティションテーブル
/// フラッシュ上のデータ配置を定義 (bootloader, app, OTA, NVS, eFuse 等)
constexpr uint32_t partition_table_offset = 0x8000;  // デフォルトオフセット

} // namespace umi::pal::esp32s3::boot
```

### 4.6 i.MX RT ブート構造

```cpp
// pal/mcu/imxrt/boot.hh
#pragma once
#include <cstdint>

namespace umi::pal::imxrt::boot {

/// @brief i.MX RT ブート構造
/// ROM bootloader が IVT (Image Vector Table) を読み込み、アプリケーションを起動する
/// IVT → Boot Data → DCD (Device Configuration Data) → Application
namespace ivt {
    constexpr uint32_t header_tag = 0xD1;      // IVT ヘッダ識別子
    constexpr uint32_t ivt_offset = 0x1000;    // FlexSPI NOR: IVT は 4KB オフセット
    constexpr uint32_t xip_base = 0x6000'0000; // FlexSPI flash ベースアドレス

    /// @brief IVT 構造体レイアウト
    /// header (4B) → entry (4B) → reserved (4B) → dcd (4B) →
    /// boot_data (4B) → self (4B) → csf (4B) → reserved (4B)
}

/// @brief FlexRAM 設定
/// i.MX RT は FlexRAM バンクを ITCM / DTCM / OCRAM に動的に割り当て可能
namespace flexram {
    constexpr uint32_t bank_size = 32 * 1024;  // 各バンク 32KB
    constexpr uint8_t bank_count = 16;         // i.MX RT1060: 16 バンク (512KB 合計)
    // デフォルト: ITCM 128KB + DTCM 128KB + OCRAM 256KB
}

} // namespace umi::pal::imxrt::boot
```

---

## 5. データソース

| プラットフォーム | ドキュメント | 入手先 |
|----------------|------------|--------|
| STM32F4 | STM32F4xx Reference Manual (RM0090) — Chapter: Flash, Boot configuration | st.com |
| STM32F4 | STM32F4xx Programming Manual (PM0214) — Cortex-M4 startup sequence | st.com |
| STM32H7 | STM32H7xx Reference Manual — Boot, FlexRAM equivalent | st.com |
| RP2040 | RP2040 Datasheet — Chapter 2.8: Boot sequence, SSI/XIP | raspberrypi.com |
| RP2350 | RP2350 Datasheet — Boot sequence, OTP boot flags | raspberrypi.com |
| ESP32-S3 | ESP32-S3 Technical Reference Manual — Chapter: Memory map, System | espressif.com |
| ESP32-S3 | ESP-IDF Bootloader Documentation — Partition table, Boot sequence | docs.espressif.com |
| ESP32-P4 | ESP32-P4 Technical Reference Manual — Boot, Memory map | espressif.com |
| i.MX RT | i.MX RT1060 Reference Manual — Chapter: System Boot, FlexRAM | nxp.com |
| i.MX RT | AN12238: i.MX RT Boot Process | nxp.com |

**注記**: リンカスクリプトとスタートアップコードはデバイス固有の情報 (メモリアドレス、サイズ) と
アーキテクチャ共通の処理 (C ランタイム初期化) を組み合わせたものである。
PAL ジェネレータでは C11 (デバイスメタデータ) のメモリ情報と
L2 (コアプロファイル) のスタートアップテンプレートから最終的なファイルを生成する。
