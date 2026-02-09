umiport アーキテクチャ設計書1. 概要と基本方針本プロジェクトは、組み込みシステムにおける「ハードウェア依存の分離」を徹底し、移植性とテスト容易性を最大化することを目的とする。コア哲学ソースコードへのハードウェア依存混入の禁止アプリケーションコード内に #ifdef STM32F4 や #if defined(BOARD_DISCO) といったプリプロセッサ分岐を禁止する。#include <umiport/mcu/stm32f4/gpio.hh> のような、ハードウェア実装へのフルパスインクルードを禁止する。依存性の注入（Dependency Injection via Build System）実装の切り替えは、C++のコード（#ifdef）ではなく、ビルドシステム（xmake）のインクルードパス解決によって行う。すべてのボード/プラットフォームは、同一のファイル名（platform.hh）で異なる実装を提供する。ゼロ・オーバーヘッド実行時の仮想関数（vtable）は使用せず、C++20 Concept と静的ポリモーフィズムを用いて、コンパイル時に解決する。2. ディレクトリ構成責務の分離を明確にするため、以下の階層構造を採用する。特に umiport は純粋なシステム接着層とし、アーキテクチャ依存コードは umiport-arch へ配置する。lib/
├── umihal/                     # [Interface] Concept定義のみ（完全独立）
│   └── include/umihal/
│       └── concept/            # GpioPort, Uart, Timer などの要件定義
│
├── umiport/                    # [Common] OS/Runtime 接着層（HW非依存）
│   └── src/
│       ├── syscalls_newlib.cc  # _write 等の newlib スタブ
│       └── main_wrapper.cc     # 必要に応じた main 前後のフック
│
├── umiport-arch/               # [Architecture] CPUコア依存の実装
│   └── include/arch/
│       ├── cortex-m/           # ARM Cortex-M 共通
│       │   ├── dwt.hh          # サイクルカウンタ
│       │   ├── nvic.hh         # 割り込み制御
│       │   ├── scb.hh          # システム制御
│       │   └── context.hh      # コンテキストスイッチ
│       ├── riscv/              # RISC-V 共通
│       └── xtensa/             # Xtensa 共通
│
├── umiport-stm32/              # [Family] STM32ファミリー共通 (IP定義)
│   └── include/stm32/
│       ├── ip/                 # IPブロックごとの定義（バージョン管理）
│       │   ├── usart_v1.hh
│       │   └── usart_v2.hh
│       └── gpio_common.hh
│
├── umiport-stm32f4/            # [Series] STM32F4シリーズ固有定義
│   ├── include/stm32f4/
│   │   ├── rcc.hh              # クロックツリー定義
│   │   ├── memory_map.hh       # ベースアドレス定義
│   │   └── uart.hh             # シリーズ特有のエイリアス定義
│   └── src/
│       └── startup.cc          # ベクタテーブル、リセットハンドラ
│
├── umiport-boards/             # [Integration] ボード定義（BSP）
│   └── include/board/
│       ├── stm32f4-renode/     # 仮想ボード: Renodeエミュレーション用
│       │   ├── platform.hh     #   統合定義 (Platform struct)
│       │   └── board.hh        #   ボード固有定数
│       ├── stm32f4-disco/      # 実機: STM32F4 Discovery
│       │   ├── platform.hh
│       │   └── board.hh
│       └── host/               # ホストPC (Linux/macOS/Windows)
│           └── platform.hh
│
└── umirtm/                     # [Library] HW非依存ライブラリ
    ├── umirtm/                 #   Monitor, Printf
    ├── umibench/               #   Benchmark Runner
    └── umimmio/                #   Register Access Abstraction
3. レイヤー構造と依存関係依存関係は厳格に一方向（上から下）とする。上位レイヤーが下位レイヤーの詳細を知ることはない。レイヤーパッケージ例役割依存先App / Testtests/, examples/アプリケーションロジックumirtm, umiport-boardsIntegrationumiport-boardsハードウェア実装の統合と設定umiport-*Hardware Implumiport-stm32f4umiport-archレジスタ操作、CPU命令発行umihal, umimmio, stm32System Glueumiportsyscalls, runtime hooksなし (or umimmio)AbstractionumihalConcept (Interface) 定義なし (0依存)Utilitiesumimmioヘルパーライブラリなし4. 出力経路の解決（Output Routing）umirtm などのライブラリは、出力先（UART, RTT, stdout）を知らない。標準的な _write syscall を通じて解決される。フロー図graph TD
    UserCode["rt::println('Hello')"] --> Lib["rt::printf(fmt, args)"]
    Lib --> Syscall["_write(fd, buf, len) <br> (in umiport/src/syscalls.cc)"]
    Syscall --> Platform["umi::port::Platform::Output::putc(c) <br> (defined in platform.hh)"]
    
    Platform --> Implementation{Which Board?}
    
    Implementation -- "stm32f4-renode" --> UART["STM32F4 UART Register"]
    Implementation -- "stm32f4-disco" --> RTT["SEGGER RTT Buffer"]
    Implementation -- "host" --> Stdout["Host OS stdout"]
コード上の実現ライブラリ (umiport/src/syscalls_newlib.cc)#include <platform.hh> // xmakeが解決するパス上のヘッダ

extern "C" int _write(int file, char* ptr, int len) {
    // ... (stdout check)
    for (int i = 0; i < len; i++) {
        umi::port::Platform::Output::putc(ptr[i]);
    }
    return len;
}
ボード定義 (umiport-boards/include/board/stm32f4-disco/platform.hh)#pragma once
#include <stm32f4/uart.hh>
#include <board/stm32f4-disco/board.hh>

namespace umi::port {
struct Platform {
    // BoardTraitsから設定値を注入
    using Output = stm32f4::UartOutput<
        USART1, 
        Board::Pin::ConsoleTx, 
        Board::Pin::ConsoleRx
    >;

    static void init() {
        Output::init();
    }
};
}
5. ビルドシステム (xmake) の設定ハードウェアの切り替えは xmake.lua でターゲットごとに includedirs を切り替えることで実現する。-- xmake.lua (例)

-- ライブラリ定義
target("umirtm")
    set_kind("static")
    add_files("lib/umirtm/src/*.cc")
    add_includedirs("lib/umirtm/include")

-- Renode用サンプルのビルドターゲット
target("example_stm32f4_renode")
    add_deps("umirtm", "umiport")
    
    -- ボード固有のヘッダパスを追加（ここが重要）
    add_includedirs("lib/umiport-boards/include/board/stm32f4-renode")
    
    -- 依存パッケージのインクルード
    add_includedirs("lib/umiport-stm32f4/include")
    add_includedirs("lib/umiport-stm32/include")
    add_includedirs("lib/umiport-arch/include")
6. 開発・拡張ガイド新しいボードを追加する場合umiport-boards/include/board/<new_board_name>/ ディレクトリを作成する。board.hh: ピン定義、クロック周波数などの定数を定義する。platform.hh: umi::port::Platform 構造体を定義し、Output 型と init() 関数を提供する。新しいMCUを追加する場合umiport-<arch>: 該当アーキテクチャ（RISC-V等）がなければ追加。umiport-<family>: 周辺機能（IP）の共通定義を作成。umiport-<series>: メモリマップ、レジスタ定義、startupコードを作成。アプリケーションを書く場合ハードウェアを意識せず、機能に集中する。// アプリケーションコード (main.cc)
#include <umirtm/print.hh>

int main() {
    // どのボードで動いているか知る必要はない
    rt::println("System initialized.");
    
    while (true) {
        // ...
    }
}
