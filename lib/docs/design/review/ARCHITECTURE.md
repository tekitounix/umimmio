# umiport アーキテクチャ設計

## 基本方針

**「ソースにハード依存を漏らさない」**ことを最優先とする。

- `#ifdef STM32F4` などの条件分岐を禁止
- フルパスインクルード `#include <umiport/mcu/stm32f4/gpio.hh>` を禁止
- 同名ヘッダ・xmake includedirs による実装切り替え

## 全体構成

```
lib/
├── umihal/                     # Concept定義のみ（完全独立）
│   └── include/umihal/
│       └── concept/
│           ├── gpio.hh
│           ├── uart.hh
│           ├── timer.hh
│           └── dma.hh
│
├── umiport/                    # 共通インフラ（MCU非依存の共有コード）
│   ├── include/umiport/
│   │   └── arm/cortex-m/
│   │       ├── cortex_m_mmio.hh  # CoreSight DWT/CoreDebug レジスタ
│   │       └── dwt.hh            # DWT サイクルカウンタ
│   └── src/stm32f4/
│       ├── startup.cc           # ベクタテーブル、Platform::init() 呼び出し
│       ├── syscalls.cc          # _write() → Platform::Output::putc()
│       └── linker.ld            # メモリレイアウト
│
├── umiport-arch/               # アーキテクチャ層
│   └── include/arch/
│       ├── cm4/
│       │   ├── cache.hh
│       │   ├── fpu.hh
│       │   └── context.hh
│       ├── cm7/
│       ├── cm33/
│       ├── ca53/
│       ├── rv32/
│       └── xtensa_lx7/
│
├── umiport-stm32/              # STM32ファミリー共通
│   └── include/stm32/
│       ├── rcc_common.hh
│       └── gpio_common.hh
│
├── umiport-stm32f4/            # STM32F4シリーズ（レジスタ操作）
│   └── include/stm32f4/
│       ├── rcc.hh
│       ├── gpio.hh
│       ├── uart.hh             # UartOutput（USART レジスタ操作）
│       └── i2s.hh
│
├── umiport-stm32h7/            # STM32H7シリーズ
│   └── include/stm32h7/
│       ├── rcc.hh
│       ├── sai.hh
│       └── mdma.hh
│
├── umiport-boards/             # ボード定義（BSP + platform 統合）
│   └── include/board/
│       ├── stm32f4-renode/      # Renode 仮想ボード
│       │   ├── platform.hh      #   Output = UartOutput (USART1)
│       │   └── board.hh         #   メモリマップ、Renode 固有設定
│       ├── stm32f4-disco/       # STM32F4 Discovery 実機
│       │   ├── platform.hh      #   Output = RttOutput (Monitor::write)
│       │   └── board.hh         #   ピン配置、クロック設定
│       ├── daisy_seed/
│       │   └── board.hh
│       └── rp2040_pico/
│           └── board.hh
│
├── umirtm/                     # 純粋HW非依存
│   └── include/umirtm/
│       ├── rtm.hh               # Monitor リングバッファ
│       ├── print.hh             # rt::println() — {} プレースホルダ
│       └── printf.hh            # rt::printf()  — C スタイルフォーマット
│
├── umibench/                   # 純粋HW非依存
│   └── include/umibench/
│       └── bench.hh             # Runner<Timer>、report<Platform>()
│
└── umimmio/                    # 純粋HW非依存
    └── include/umimmio/
        └── mmio.hh              # レジスタ抽象化
```

## 依存関係

```
umihal (0 deps)
    ↑
umiport-arch (deps: umihal)
    ↑
umiport-stm32 (deps: umihal, umiport-arch)
    ↑
umiport-stm32f4 (deps: umihal, umiport-arch, umiport-stm32, umimmio)
    ↑
umiport-boards (deps: umiport-stm32f4 等)
    ↑
application / tests / examples
    ↑
umirtm (0 deps), umibench (0 deps), umimmio (0 deps)
```

## 出力経路の設計

umirtm の出力はライブラリ自体が HW を知らない。
`_write()` newlib syscall を経由してボードの Output に到達する。

```
rt::println("Hello {}", 42)
    ↓
rt::printf(fmt, args...)        ← umirtm（HW 非依存）
    ↓
::_write(1, buf, len)           ← newlib syscall（umiport/src/*/syscalls.cc）
    ↓
umi::port::Platform::Output::putc(c)  ← platform.hh で切り替え
    ↓
┌─ stm32f4-renode:  USART1 レジスタ書き込み（UartOutput）
├─ stm32f4-disco:   RTT リングバッファ（Monitor::write）
└─ ホスト:          stdout
```

### startup.cc / syscalls.cc の役割

`umiport/src/stm32f4/` にある startup.cc と syscalls.cc は：

- `#include "platform.hh"` で**同名ヘッダ**をインクルード
- どの `platform.hh` が使われるかは **xmake の includedirs** で決まる
- ボードごとの `platform.hh` が `umi::port::Platform` 型を定義

これにより、ソースを一切変更せずにボードを切り替えられる。

## 原則

### 1. 各パッケージの責務

| パッケージ | 内容 | 依存 | 禁止事項 |
|-----------|------|------|---------|
| **umihal** | Concept定義（GpioPort, Uart） | なし | 実装を含めない |
| **umiport** | 共通インフラ（startup, syscalls, Cortex-M共通） | umimmio | MCU固有コード |
| **umiport-arch** | アーキ固有（コンテキストスイッチ等） | umihal | SoC依存コード |
| **umiport-stm32f4** | レジスタ操作のみ | umihal, arch, stm32, umimmio | ボード依存コード |
| **umiport-boards** | ボード定義（platform.hh, board.hh） | umiport-* | ライブラリ固有コード |
| **umirtm** | printf/print/Monitor | なし | HW依存コード |
| **umibench** | ベンチマーク Runner/Report | なし | HW依存コード |
| **umimmio** | レジスタ抽象化 | なし | HW依存コード |

### 2. ソースコード規約

```cpp
// 禁止：条件分岐
#ifdef STM32F4
    // ...
#endif

// 禁止：フルパスインクルード
#include <umiport/mcu/stm32f4/gpio.hh>

// 禁止：ライブラリがumiportを直接参照
namespace rt {
    void init() { umi::stm32f4::UART::init(); }  // エラー！
}

// 正解：同名ヘッダ（xmakeがパスを解決）
#include <stm32f4/gpio.hh>
#include <arch/cm4/context.hh>
#include <board/stm32f4-disco/board.hh>

// 正解：ライブラリは HW を知らない
rt::println("Hello {}", 42);  // _write() syscall 経由で Output に到達
```

### 3. 統合はボード層（platform.hh）で

```cpp
// umiport-boards/include/board/stm32f4-renode/platform.hh
#pragma once
#include <stm32f4/uart.hh>  // umiport-stm32f4

namespace umi::port {
struct Platform {
    using Output = stm32f4::UartOutput;  // USART1 経由
    static void init() { Output::init(); }
};
}
```

```cpp
// umiport-boards/include/board/stm32f4-disco/platform.hh
#pragma once
#include <umirtm/rtm.hh>

namespace umi::port {
struct Platform {
    struct Output {
        static void init() { rtm::init("RT MONITOR"); }
        static void putc(char c) {
            auto byte = std::byte{static_cast<unsigned char>(c)};
            rtm::write(std::span{&byte, 1});
        }
    };
    static void init() { Output::init(); }
};
}
```

### 4. アプリケーションのソースはボード非依存

```cpp
// lib/umirtm/examples/print_demo.cc
#include <umirtm/print.hh>

int main() {
    rt::println("integer: {}", 42);
    rt::println("float: {}", 3.14);
    return 0;
}
```

ソースに `#include "platform.hh"` は不要。
`_write()` syscall が自動的にボードの Output にルーティングする。

## umiport-boards の役割

`umiport-boards` は**ボードごとの統合層**を提供する。

### ボードが定義するもの

| ファイル | 内容 |
|---------|------|
| `platform.hh` | `umi::port::Platform` — Output 型、init() |
| `board.hh` | ピン配置、クロック定数、メモリマップ |

### Renode は仮想ボード

Renode エミュレータは「仮想ボード」として扱う。
同じ MCU（STM32F4）でも出力経路が異なるため、別ボードとして定義：

| ボード | Output | デバッグ手段 |
|--------|--------|------------|
| `stm32f4-renode` | USART1（UART） | Renode UART モニタ |
| `stm32f4-disco` | RTT リングバッファ | J-Link/OpenOCD RTT |

### xmake.lua でのボード選択

```lua
-- xmake.lua でボードを指定
target("umirtm_example_stm32f4_renode")
    add_deps("umirtm", "umiport-boards")
    add_includedirs("$(board_include)/stm32f4-renode")  -- platform.hh 選択

target("umirtm_example_stm32f4_disco")
    add_deps("umirtm", "umiport-boards")
    add_includedirs("$(board_include)/stm32f4-disco")   -- platform.hh 選択
```

同じソース（`print_demo.cc`）が両方のターゲットでビルドできる。

## テストとサンプルのターゲット配置

ターゲット定義は**使用側**に置く。

```
lib/umirtm/
├── include/umirtm/      # ライブラリ本体（HW 非依存）
├── tests/
│   ├── xmake.lua         # ホスト・ARM・WASM テストターゲット
│   └── test_*.cc
└── examples/
    ├── xmake.lua         # ARM example ターゲット
    └── print_demo.cc
```

## 命名規則

| 対象 | 命名 | 例 |
|-----|------|-----|
| Concept | CamelCase | GpioPort, Uart |
| アーキパッケージ | umiport-\<arch\> | umiport-arch, umiport-rv64 |
| MCUファミリー | umiport-\<family\> | umiport-stm32 |
| MCUシリーズ | umiport-\<series\> | umiport-stm32h7, umiport-rp2040 |
| ボード | board/\<name\>/ | board/stm32f4-renode/, board/daisy_seed/ |
| 実装型 | ベース+サフィックス | DwtTimer, UartOutput, RttOutput |

## まとめ

| もの | 場所 | 理由 |
|------|------|------|
| Concept定義 | umihal | 完全独立、0依存 |
| 共通インフラ | umiport | startup, syscalls, Cortex-M 共通コード |
| レジスタ操作 | umiport-\<mcu\> | 純粋なHW実装 |
| ボード定義 | umiport-boards/board/\<name\>/ | platform.hh + board.hh |
| ライブラリ本体 | umirtm, umibench, umimmio | HW非依存 |
| ターゲット定義 | tests/, examples/ | 使用側に配置 |

## 移行指針（現状の platforms/ から）

| 現状 | 移行先 |
|------|--------|
| `lib/*/platforms/arm/cortex-m/stm32f4/platform.hh` | `umiport-boards/include/board/stm32f4-renode/platform.hh` |
| `lib/*/platforms/arm/cortex-m/stm32f4/xmake.lua` | `lib/*/tests/xmake.lua` に統合 |
| `lib/*/platforms/arm/cortex-m/stm32f4/renode/*.resc` | 削除（embedded ルールが自動生成） |
| `lib/umibench/platforms/arm/cortex-m/dwt.hh` | `umiport/include/umiport/arm/cortex-m/dwt.hh` |
| `lib/umibench/platforms/arm/cortex-m/cortex_m_mmio.hh` | `umiport/include/umiport/arm/cortex-m/cortex_m_mmio.hh` |
| `lib/umibench/platforms/host/` | そのまま（HW 非依存） |
| `lib/umibench/platforms/wasm/` | そのまま（HW 非依存） |
| `lib/umibench/platforms/common/` | そのまま（HW 非依存） |
