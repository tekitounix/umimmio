# umiport アーキテクチャ設計（最終版）

**ステータス:** 設計確定版
**策定日:** 2026-02-08
**ベース:** ARCHITECTURE.md + 7エージェントレビュー統合 + device/separation レビュー6件統合
**改訂:** umiport/umiport-boards 統合 + umidevice 独立パッケージ化

---

## 1. 設計哲学

### 不変の原則

1. **ソースにハード依存を漏らさない**
   - `#ifdef STM32F4` などの条件分岐を禁止
   - フルパスインクルード `#include <umiport/mcu/stm32f4/gpio.hh>` を禁止（platform.hh 内のみ例外）
   - 同名ヘッダ＋xmake includedirs による実装切り替え

2. **ライブラリは HW を知らない**
   - umirtm, umibench, umimmio は 0 依存を維持
   - HW 固有コードは一切含まない

3. **統合は board 層のみで行う**
   - MCU ドライバと外部デバイスドライバの結合は `board/platform.hh` のみ
   - ライブラリが umiport や umidevice を直接参照しない

4. **ゼロオーバーヘッド**
   - vtable を使用せず、C++ Concept と静的ポリモーフィズムで解決
   - すべての抽象はコンパイル時に解消される

5. **パッケージは「単独で意味を持つ」単位で分割する**
   - 常にセットで使われる不可分なものを別パッケージに分けない
   - 分離の正当性は「独立して使えるか」と「知識の帰属先が異なるか」の2軸で判断

---

## 2. 全体構成

```
lib/
├── umihal/                     # [Interface] Concept 定義のみ（完全独立）
│   └── include/umihal/
│       ├── concept/
│       │   ├── gpio.hh          # GpioPort concept
│       │   ├── uart.hh          # UartBasic / UartAsync concept
│       │   ├── timer.hh         # Timer concept
│       │   ├── clock.hh         # ClockSource / ClockTree concept
│       │   ├── codec.hh         # CodecBasic / AudioCodec concept
│       │   └── platform.hh     # Platform concept + static_assert 用
│       └── ...                  # その他既存 concept
│
├── umiport/                    # [Hardware Driver] MCU + アーキ + ボード統合
│   ├── include/umiport/
│   │   ├── arm/cortex-m/        # アーキテクチャ共通（Cortex-M）
│   │   │   ├── cortex_m_mmio.hh  # CoreSight DWT/CoreDebug レジスタ
│   │   │   └── dwt.hh            # DWT サイクルカウンタ
│   │   ├── mcu/                  # MCU 固有レジスタ操作
│   │   │   ├── stm32f4/
│   │   │   │   ├── rcc.hh
│   │   │   │   ├── gpio.hh
│   │   │   │   ├── uart.hh       # UartOutput（USART レジスタ操作）
│   │   │   │   └── i2s.hh
│   │   │   └── stm32h7/          # 将来の MCU
│   │   │       ├── rcc.hh
│   │   │       ├── sai.hh
│   │   │       └── mdma.hh
│   │   └── board/                # ボード定義（旧 umiport-boards を統合）
│   │       ├── stm32f4-renode/   # Renode 仮想ボード
│   │       │   ├── platform.hh    #   Platform 型定義
│   │       │   └── board.hh       #   定数のみ
│   │       ├── stm32f4-disco/    # STM32F4 Discovery 実機
│   │       │   ├── platform.hh
│   │       │   └── board.hh
│   │       ├── daisy_seed/       # 将来
│   │       └── host/             # ホスト PC
│   │           └── platform.hh
│   ├── src/                      # startup / syscalls / linker
│   │   ├── stm32f4/
│   │   │   ├── startup.cc         # ベクタテーブル、Platform::init() 呼び出し
│   │   │   ├── syscalls.cc        # _write() → Platform::Output::putc()
│   │   │   └── linker.ld          # メモリレイアウト
│   │   └── stm32h7/              # 将来の MCU
│   └── xmake.lua
│
├── umidevice/                  # [Hardware Driver] 外部デバイスドライバ（MCU 非依存）
│   ├── include/umidevice/
│   │   ├── audio/                # オーディオコーデック
│   │   │   ├── cs43l22/
│   │   │   │   ├── cs43l22.hh
│   │   │   │   └── cs43l22_regs.hh
│   │   │   ├── wm8731/
│   │   │   │   ├── wm8731.hh
│   │   │   │   ├── wm8731_regs.hh
│   │   │   │   └── wm8731_transport.hh
│   │   │   ├── pcm3060/
│   │   │   │   ├── pcm3060.hh
│   │   │   │   └── pcm3060_regs.hh
│   │   │   └── ak4556/
│   │   │       └── ak4556.hh
│   │   ├── display/              # ディスプレイ（将来）
│   │   └── sensor/               # センサー（将来）
│   ├── tests/
│   │   └── test_drivers.cc
│   └── xmake.lua
│
├── umirtm/                     # [Library] HW 非依存
│   └── include/umirtm/
│       ├── rtm.hh               # Monitor リングバッファ
│       ├── print.hh             # rt::println() — {} プレースホルダ
│       └── printf.hh            # rt::printf()  — C スタイルフォーマット
│
├── umibench/                   # [Library] HW 非依存
│   └── include/umibench/
│       └── bench.hh             # Runner<Timer>、report<Platform>()
│
├── umimmio/                    # [Library] HW 非依存
│   └── include/umimmio/
│       └── mmio.hh              # レジスタ抽象化
│
└── umitest/                    # [Library] テスト基盤
    └── include/umitest/
        └── test.hh
```

### 旧版からの主要変更点

| 変更 | 旧 | 新 | 理由 |
|------|----|----|------|
| **umiport-boards 廃止** | umiport + umiport-boards の2パッケージ | umiport 単体（board/ を内包） | 単独で意味を持たないものを分離する必然性がない（2/3レビュー支持） |
| **umidevice 追加** | 設計文書に不在 | 独立パッケージ（Hardware Driver Layer） | IC に帰属する MCU 非依存の知識（3/3レビュー支持） |
| startup/syscalls/linker.ld | umiport/src/stm32f4/ | umiport/src/stm32f4/（統合後も同位置） | umiport 内で board/ と共存 |
| MCU 固有ヘッダ | 各 umiport-stm32f4 パッケージ（未作成） | umiport/include/umiport/mcu/ | パッケージ爆発を防止 |
| HAL Concept | Uart（単一巨大concept） | UartBasic / UartAsync + CodecBasic / AudioCodec | 最小限のデバイスでも実装可能に |
| 出力経路 | `_write()` syscall 固定 | link-time注入 + syscallは一実装 | 非ARM/非newlib環境への対応 |

---

## 3. 依存関係

```
                    umihal (0 deps)
               ┌─── Concept 定義 ───┐
               │    MCU + Device    │
               │    両方の契約      │
               │                    │
         ┌─────┴─────┐     ┌───────┴───────┐
         │  umiport  │     │  umidevice    │
         │           │     │               │
         │ arm/      │     │ audio/        │
         │ mcu/      │     │ display/      │
         │ board/    │     │ sensor/       │
         │ src/      │     │               │
         └─────┬─────┘     └───────┬───────┘
               │                    │
               │   umimmio (0 deps) │
               │   ← 両者が利用 →   │
               │                    │
               └─────────┬──────────┘
                         │
                   platform.hh
              ─── board/ 内で MCU × Device を結合
                         │
                   application / tests / examples

         umirtm (0 deps)  ─── HW 非依存ユーティリティ
         umibench (0 deps) ─── ベンチマーク
         umitest (0 deps)  ─── テスト基盤
```

### Hardware Driver Layer の対称構造

umiport と umidevice は**同格の Hardware Driver Layer** として並列する:

| | umiport | umidevice |
|---|---|---|
| 対象 | MCU 内蔵ペリフェラル | 外部 IC デバイス |
| MCU 依存 | あり（MCU シリーズ固有） | **なし**（どの MCU でも動作） |
| トランスポート | メモリマップド（CPU 直結） | バスプロトコル（I2C/SPI 経由） |
| umimmio Tag | `DirectTransportTag` | `I2CTransportTag`, `SPITransportTag` |
| レジスタ定義 | `Device<RW, DirectTransportTag>` | `Device<RW, I2CTransportTag>` |
| umihal 契約 | `UartBasic`, `GpioPin`, `Timer` | `AudioCodec`, `CodecBasic` |
| テスト方法 | モックで検証可能 | モックで検証可能 |
| 再利用性 | 同 MCU シリーズ内のみ | **全 MCU 共通** |

**共通点:** 同じ umimmio 基盤でレジスタを記述し、同じ umihal で契約を定義し、同じモックパターンでテストする。

**差異の核心:** MCU 依存性の有無。この一点がパッケージ境界を決定づける。

依存は**厳格に一方向**。umiport と umidevice は互いを知らない。両者がボード層（`board/platform.hh`）で初めて出会う。

---

## 4. 出力経路の設計

umirtm の出力はライブラリ自体が HW を知らない。
出力先の解決は **link-time 注入** で行う。

### 4.1 出力経路フロー

```
rt::println("Hello {}", 42)
    ↓
rt::printf(fmt, args...)             ← umirtm（HW 非依存）
    ↓
umi::rt::detail::write_bytes(data)   ← link-time 注入点
    ↓
┌─ Cortex-M + newlib:
│    _write(1, buf, len) → umi::port::Platform::Output::putc(c)
│    ├─ stm32f4-renode: USART1 レジスタ書き込み
│    └─ stm32f4-disco:  RTT リングバッファ
│
├─ WASM:
│    wasm_write(buf, len) → console.log 等
│
└─ ホスト:
     ::write(1, buf, len) → stdout
```

### 4.2 link-time 注入の実装

umirtm が要求するシンボル:

```cpp
// umirtm/include/umirtm/detail/write.hh
#pragma once
#include <cstddef>
#include <span>

namespace umi::rt::detail {

/// ボード/ターゲット側が定義する出力関数。
/// umirtm はこのシンボルの存在のみを要求する。
extern void write_bytes(std::span<const std::byte> data);

} // namespace umi::rt::detail
```

Cortex-M + newlib 環境での実装（umiport 内のボード層）:

```cpp
// umiport/src/stm32f4/syscalls.cc
#include "platform.hh"  // 同一パッケージ内の board/ から解決
#include <umirtm/detail/write.hh>

namespace umi::rt::detail {

void write_bytes(std::span<const std::byte> data) {
    for (auto byte : data) {
        umi::port::Platform::Output::putc(static_cast<char>(byte));
    }
}

} // namespace umi::rt::detail

// newlib _write syscall（printf 等からの経路も維持）
extern "C" int _write(int file, char* ptr, int len) {
    if (file == 1 || file == 2) {
        for (int i = 0; i < len; i++) {
            umi::port::Platform::Output::putc(ptr[i]);
        }
    }
    return len;
}
```

ホスト環境での実装:

```cpp
// umiport/src/host/write.cc
#include <umirtm/detail/write.hh>
#include <unistd.h>

namespace umi::rt::detail {

void write_bytes(std::span<const std::byte> data) {
    ::write(1, data.data(), data.size());
}

} // namespace umi::rt::detail
```

### 4.3 旧方式との互換

`_write()` syscall 経由のルーティングは Cortex-M + newlib 環境で引き続き機能する。`umi::rt::detail::write_bytes()` と `_write()` は同じ syscalls.cc 内で共存し、両方とも `Platform::Output::putc()` に到達する。

---

## 5. HAL Concept 設計

### 5.1 設計原則

- Concept は**必須（Basic）** と **拡張（Extended）** に分離する
- 「NOT_SUPPORTED を返す」escape hatch は使わない
- 使用側は必要な制約だけを要求する
- MCU ペリフェラルと外部デバイスの両方の Concept を umihal が定義する（対称構造）

### 5.2 Result 型

HAL の全操作は `Result<T>` を返す。エラー体系を統一し、Transport エラーとデバイスエラーを一貫して扱う。

```cpp
// umihal/include/umihal/result.hh
#pragma once
#include <expected>
#include <cstdint>

namespace umi::hal {

enum class ErrorCode : uint8_t {
    OK = 0,
    TIMEOUT,
    NACK,
    BUS_ERROR,
    OVERRUN,
    NOT_READY,
    INVALID_CONFIG,
};

template <typename T>
using Result = std::expected<T, ErrorCode>;

} // namespace umi::hal
```

`Result<T>` は `umihal` に所属する。全パッケージがこの型を共有することで、Transport エラー（NACK, TIMEOUT）とデバイスエラー（NOT_READY）を統一的に扱える。

### 5.3 Transport Concept

外部デバイスドライバが必要とするバストランスポートの契約:

```cpp
// umihal/include/umihal/concept/transport.hh
namespace umi::hal {

/// I2C トランスポート — 外部デバイスドライバが要求する
template <typename T>
concept I2cTransport = requires(T& t, uint8_t addr, uint8_t reg,
                                std::span<const uint8_t> tx,
                                std::span<uint8_t> rx) {
    { t.write(addr, reg, tx) } -> std::same_as<Result<void>>;
    { t.read(addr, reg, rx) } -> std::same_as<Result<void>>;
};

/// SPI トランスポート
template <typename T>
concept SpiTransport = requires(T& t,
                                std::span<const uint8_t> tx,
                                std::span<uint8_t> rx) {
    { t.transfer(tx, rx) } -> std::same_as<Result<void>>;
    { t.select() } -> std::same_as<void>;
    { t.deselect() } -> std::same_as<void>;
};

} // namespace umi::hal
```

umiport の MCU I2C ドライバがこの Concept を満たし、umidevice のデバイスドライバが Transport テンプレートパラメータとして受け取る。ボード層の platform.hh で両者を結合する:

```cpp
// platform.hh での結合例
using I2C = stm32f4::I2C1;                    // umiport — I2cTransport を満たす
using Codec = device::CS43L22Driver<I2C>;       // umidevice — I2cTransport を要求
static_assert(umi::hal::I2cTransport<I2C>);      // コンパイル時検証
```

### 5.4 MCU ペリフェラル向け Concept

```cpp
// umihal/include/umihal/concept/uart.hh
namespace umi::hal {

/// 最小限の UART Concept — すべての UART 実装が満たす
template <typename T>
concept UartBasic = requires(T& u, const uart::Config& cfg, std::uint8_t byte) {
    { u.init(cfg) } -> std::same_as<Result<void>>;
    { u.write_byte(byte) } -> std::same_as<Result<void>>;
    { u.read_byte() } -> std::same_as<Result<std::uint8_t>>;
};

/// 非同期転送をサポートする UART
template <typename T>
concept UartAsync = UartBasic<T> && requires(T& u, std::span<const std::uint8_t> data) {
    { u.write_async(data) } -> std::same_as<Result<void>>;
};

} // namespace umi::hal
```

```cpp
// umihal/include/umihal/concept/clock.hh
namespace umi::hal {

/// クロックソース
template <typename T>
concept ClockSource = requires {
    { T::enable() } -> std::same_as<void>;
    { T::is_ready() } -> std::same_as<bool>;
    { T::get_frequency() } -> std::same_as<uint32_t>;
};

/// クロックツリー — ボードが必ず定義
template <typename T>
concept ClockTree = requires {
    { T::init() } -> std::same_as<void>;
    { T::system_clock() } -> std::same_as<uint32_t>;
    { T::ahb_clock() } -> std::same_as<uint32_t>;
    { T::apb1_clock() } -> std::same_as<uint32_t>;
};

} // namespace umi::hal
```

### 5.5 外部デバイス向け Concept

外部デバイスの能力は大きく異なるため、Concept を階層化する:

```cpp
// umihal/include/umihal/concept/codec.hh
namespace umi::hal {

/// 最小限: 初期化のみ（AK4556 等のパッシブコーデック対応）
template <typename T>
concept CodecBasic = requires(T& c) {
    { c.init() } -> std::convertible_to<bool>;
};

/// 音量制御あり
template <typename T>
concept CodecWithVolume = CodecBasic<T> && requires(T& c, int db) {
    { c.set_volume(db) } -> std::same_as<void>;
};

/// フル機能コーデック（電源管理 + ミュート）
template <typename T>
concept AudioCodec = CodecWithVolume<T> && requires(T& c, bool m) {
    { c.power_on() } -> std::same_as<void>;
    { c.power_off() } -> std::same_as<void>;
    { c.mute(m) } -> std::same_as<void>;
};

} // namespace umi::hal
```

### 5.6 Platform Concept とコンパイル時検証

```cpp
// umihal/include/umihal/concept/platform.hh
namespace umi::hal {

/// 出力デバイス
template <typename T>
concept OutputDevice = requires(char c) {
    { T::init() } -> std::same_as<void>;
    { T::putc(c) } -> std::same_as<void>;
};

/// Platform — 全ボードが満たすべき契約
template <typename T>
concept Platform = requires {
    requires OutputDevice<typename T::Output>;
    { T::init() } -> std::same_as<void>;
};

} // namespace umi::hal
```

各 platform.hh で HAL 契約の充足を static_assert で検証する:

```cpp
// umiport/include/umiport/board/stm32f4-renode/platform.hh
#include <umihal/concept/platform.hh>
#include <umiport/mcu/stm32f4/uart.hh>

namespace umi::port {

struct Platform {
    using Output = stm32f4::UartOutput;

    static void init() {
        Output::init();
    }
};

// コンパイル時契約検証
static_assert(umi::hal::Platform<Platform>,
    "Platform must satisfy umi::hal::Platform concept");

} // namespace umi::port
```

ボード追加時に「何を実装すべきか」がコンパイラエラーで明確になる。

---

## 6. 各パッケージの責務

| パッケージ | 内容 | 依存 | 禁止事項 | 単独で意味を持つか |
|-----------|------|------|---------|:------------------:|
| **umihal** | Concept 定義（Platform, Uart, ClockTree, AudioCodec） | なし | 実装を含めない | ○ |
| **umiport** | アーキ共通 + MCU レジスタ操作 + ボード統合 + startup | umimmio | ─ | ○（ボード選択で動作） |
| **umidevice** | 外部デバイスドライバ（MCU 非依存） | umimmio | MCU 固有コード | ○（Mock Transport で動作） |
| **umirtm** | printf/print/Monitor | なし | HW 依存コード | ○ |
| **umibench** | ベンチマーク Runner/Report | なし | HW 依存コード | ○ |
| **umimmio** | レジスタ抽象化 | なし | HW 依存コード | ○ |
| **umitest** | テスト基盤 | なし | HW 依存コード | ○ |

**全パッケージが単独で意味を持つ。** これが正しい分割の証拠。

### umiport の内部構造ルール

umiport は4つのサブディレクトリで責務を分離する:

```
umiport/include/umiport/
├── arm/cortex-m/    ← アーキテクチャ共通（全 Cortex-M で共通）
├── mcu/             ← MCU 固有（レジスタ定義、ドライバ）
│   ├── stm32f4/
│   └── stm32h7/     ← MCU追加は mcu/ にディレクトリを足すだけ
└── board/           ← ボード定義（platform.hh + board.hh）
    ├── stm32f4-renode/
    ├── stm32f4-disco/
    └── host/        ← ボード追加は board/ にディレクトリを足すだけ

umiport/src/
└── stm32f4/         ← startup / syscalls / linker（同一 MCU のボード間で共有）
```

- `arm/cortex-m/` — DWT, SCB, NVIC 等、ARM Cortex-M コアに共通のコード
- `mcu/<series>/` — 特定 MCU シリーズのレジスタ操作のみ
- `board/<name>/` — ボード定義（旧 umiport-boards）。platform.hh が MCU + Device の統合点
- `src/<mcu>/` — startup.cc, syscalls.cc, linker.ld。platform.hh に依存するボード統合コード

**パッケージ数の爆発を防ぎつつ、MCU 固有コード・ボード定義・アーキ共通コードの場所を明確にする。**

### umidevice の内部構造ルール

umidevice はカテゴリ別サブディレクトリで分類する:

```
umidevice/include/umidevice/
├── audio/           ← オーディオコーデック
│   ├── cs43l22/
│   ├── wm8731/
│   ├── pcm3060/
│   └── ak4556/
├── display/         ← ディスプレイ（将来）
└── sensor/          ← センサー（将来）
```

- MCU 固有コードは一切含まない
- 全ドライバが umimmio Transport テンプレートで通信バスを注入される
- Mock Transport でホストテスト可能

---

## 7. ボード定義（umiport 内 board/）

### 7.1 ボードが提供するもの

| ファイル | 内容 | 必須 |
|---------|------|:----:|
| `board/<name>/platform.hh` | `umi::port::Platform` 型定義（Output, Timer, init()） | ○ |
| `board/<name>/board.hh` | 定数のみ（ピン配置、クロック定数、メモリマップ） | ○ |
| `src/<mcu>/startup.cc` | ベクタテーブル、リセットハンドラ（MCU 間共有可） | ○（組み込み） |
| `src/<mcu>/syscalls.cc` | newlib syscall + `umi::rt::detail::write_bytes()` | ○（組み込み） |
| `src/<mcu>/linker.ld` | メモリレイアウト | ○（組み込み） |

ホスト/WASM ターゲットでは startup.cc / syscalls.cc / linker.ld は不要。
`umi::rt::detail::write_bytes()` の実装のみ提供する。

### 7.2 board.hh は定数専用

board.hh には**定数のみ**を記述する。ロジックや初期化コードは含めない。

`constexpr` メンバ関数による**純粋な派生値計算**は定数として扱い、board.hh に含めてよい。判断基準は「副作用がなく、コンパイル時に完全に解決されるか」：

```cpp
// OK: 他の定数から導出される派生値（定数の一種）
static constexpr uint32_t ahb_prescaler() { return system_clock / ahb_clock; }
static constexpr uint32_t apb1_timer_clock() { return apb1_clock * 2; }

// NG: 実行時ロジック、初期化、HW アクセス
static void configure_pll() { /* ... */ }  // ロジック → platform.hh へ
```

```cpp
// umiport/include/umiport/board/stm32f4-renode/board.hh
#pragma once
#include <cstdint>

namespace umi::board {

struct Stm32f4Renode {
    static constexpr uint32_t hse_frequency = 25'000'000;
    static constexpr uint32_t system_clock  = 168'000'000;

    struct Pin {
        static constexpr uint32_t console_tx = 9;   // PA9
        static constexpr uint32_t console_rx = 10;  // PA10
    };

    struct Memory {
        static constexpr uint32_t flash_base = 0x08000000;
        static constexpr uint32_t flash_size = 1024 * 1024;
        static constexpr uint32_t ram_base   = 0x20000000;
        static constexpr uint32_t ram_size   = 128 * 1024;
    };
};

} // namespace umi::board
```

### 7.3 platform.hh の設計

platform.hh は MCU ドライバ + 外部デバイスドライバ + ボード定数を**唯一の統合点**として結合する:

```cpp
// umiport/include/umiport/board/stm32f4-renode/platform.hh
#pragma once
#include <umihal/concept/platform.hh>
#include <umiport/mcu/stm32f4/uart.hh>       // MCU ドライバ（同一パッケージ内）
#include <umiport/arm/cortex-m/dwt.hh>       // アーキ共通（同一パッケージ内）
#include <umiport/board/stm32f4-renode/board.hh>  // ボード定数（同一パッケージ内）

namespace umi::port {

struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;

    static constexpr const char* name() { return "stm32f4-renode"; }

    static void init() {
        Output::init();
    }
};

static_assert(umi::hal::Platform<Platform>);

} // namespace umi::port
```

外部デバイスを使用するボードの場合:

```cpp
// umiport/include/umiport/board/stm32f4-disco/platform.hh
#pragma once
#include <umihal/concept/platform.hh>
#include <umiport/mcu/stm32f4/uart.hh>       // MCU ドライバ
#include <umiport/mcu/stm32f4/i2c.hh>        // MCU I2C ドライバ
#include <umiport/arm/cortex-m/dwt.hh>       // アーキ共通
#include <umidevice/audio/cs43l22/cs43l22.hh> // 外部デバイスドライバ ★
#include <umiport/board/stm32f4-disco/board.hh>

namespace umi::port {

struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;
    using Codec  = umi::device::CS43L22Driver<I2cTransport>;  // ★ 統合点

    static constexpr const char* name() { return "stm32f4-disco"; }

    static void init() {
        Output::init();
    }
};

static_assert(umi::hal::Platform<Platform>);

} // namespace umi::port
```

**umidevice は umiport を知らない。umiport は umidevice を知らない。** 両者が `board/platform.hh` で初めて出会う。これは「統合は board 層のみで行う」原則に完全に合致する。

> **⚠️ disco の Output 実装について:**
> 現在の `stm32f4-disco/platform.hh` は `<umirtm/rtm.hh>` を include して RTT リングバッファ経由で出力する。
> headeronly パッケージのため**ビルドは壊れない**（include パスは使用側ターゲットで解決される）が、
> umiport のヘッダが umirtm のヘッダを論理的に参照しており、Section 3 の依存図と矛盾する。
> この矛盾は現在の umiport-boards でも同じであり、移行で新たに発生する問題ではない。
>
> また、本 Section 7.3 のコード例では disco の Output を `stm32f4::UartOutput` と記述しているが、
> Section 7.5 の表では disco の Output を「RTT リングバッファ」と明記しており、文書内に矛盾がある。
> 移行時に disco の出力方式を確定させること。

### 7.4 Timer を Platform に統合（二重 Platform の解消）

umibench 用の Timer 情報を `umi::port::Platform` に含めることで、umibench 固有の platform.hh を不要にする。

旧設計:
- `umi::port::Platform`（起動用）と `umi::bench::Platform`（ベンチマーク用）が別ファイル
- 新ボード追加時に両方を作る必要がある

新設計:
- `umi::port::Platform::Timer` で一元管理
- umibench はボードの Platform から Timer 型を取得

```cpp
// umibench 側（ボード固有 platform.hh 不要）
template <typename BoardPlatform>
void run_benchmark() {
    using Timer = typename BoardPlatform::Timer;
    // ...
}
```

### 7.5 Renode は仮想ボード

Renode エミュレータは「仮想ボード」として扱う。
同じ MCU（STM32F4）でも出力経路が異なるため、別ボードとして定義:

| ボード | Output | デバッグ手段 |
|--------|--------|------------|
| `stm32f4-renode` | USART1（UART） | Renode UART モニタ |
| `stm32f4-disco` | RTT リングバッファ | J-Link/OpenOCD RTT |

---

## 8. ソースコード規約

### 8.1 マクロ禁止（例外なし）

プリプロセッサマクロによる条件分岐は**全面禁止**。「定数定義ヘッダ内のみ許容」のような例外も設けない。

MCU シリーズ内の微細な差異（STM32F401 vs STM32F429 のペリフェラル数差異、クロックツリー差異等）は **constexpr 定数とテンプレート** で解決する:

```cpp
// 禁止：マクロ条件分岐（定数定義であっても禁止）
#ifdef STM32F407xx
    static constexpr uint32_t max_clock = 168'000'000;
#elif defined(STM32F401xx)
    static constexpr uint32_t max_clock = 84'000'000;
#endif

// 正解：MCU 特性をテンプレートパラメータまたは board.hh の constexpr で解決
// board/stm32f4-renode/board.hh
namespace umi::board {
struct Stm32f4Renode {
    static constexpr uint32_t max_clock = 168'000'000;
    static constexpr bool has_fmc = true;
    static constexpr uint32_t sram_size = 128 * 1024;
};
}

// 正解：テンプレートで能力差を吸収
template <typename Board>
void configure_clock() {
    if constexpr (Board::max_clock > 100'000'000) {
        // 高速クロック設定
    } else {
        // 低速クロック設定
    }
}
```

MCU 固有の差異はすべて `board.hh` の constexpr 定数として表現する。`if constexpr` でコンパイル時分岐すれば、`#ifdef` と同等の効果をゼロオーバーヘッドで、かつ型安全に達成できる。

### 8.2 その他の禁止事項

```cpp
// 禁止：フルパスインクルード（platform.hh の外部からの使用）
#include <umiport/mcu/stm32f4/gpio.hh>

// 禁止：ライブラリが umiport / umidevice を直接参照
namespace rt {
    void init() { umi::stm32f4::UART::init(); }  // 違反！
}

// 正解：同名ヘッダ（xmake がパスを解決）
#include <board/stm32f4-disco/board.hh>

// 正解：ライブラリは HW を知らない
rt::println("Hello {}", 42);  // write_bytes() 経由で Output に到達
```

### 8.3 include 許可の層ルール

| 層 | umiport/mcu/ | umidevice/ | board.hh | 許可 |
|----|:---:|:---:|:---:|:---:|
| HW 非依存ライブラリ | × | × | × | umirtm, umibench 等 |
| 共通アプリケーション | × | × | × | ボード非依存のアプリ |
| platform.hh（統合層） | ○ | ○ | ○ | **唯一の統合点** |
| ボード専用アプリ | × | × | ○ | Platform 経由で情報を取得 |

共通アプリケーションが必要な HW 情報は **Platform 型の型メンバ経由** で公開する。直接 `board.hh` を include しない。

### 8.4 platform.hh 内のインクルードは例外

platform.hh は**統合層**であり、umiport の MCU 固有ヘッダと umidevice のデバイスヘッダを直接インクルードすることが許可される:

```cpp
// platform.hh 内では umiport / umidevice のヘッダを直接使用可能
#include <umiport/mcu/stm32f4/uart.hh>          // OK（統合層のみ許可）
#include <umiport/arm/cortex-m/dwt.hh>          // OK
#include <umidevice/audio/cs43l22/cs43l22.hh>   // OK（統合層のみ許可）
```

---

## 9. xmake によるビルド設定

### 9.1 ルールベースのボード選択

ボイラープレートを排除するため、xmake ルールでボード設定を一元管理する:

```lua
-- umiport/rules/board.lua
rule("umiport.board")
    on_config(function(target)
        local board = target:values("umiport.board")
        local umiport_dir = path.join(os.scriptdir(), "..")
        local board_include = path.join(umiport_dir, "include/umiport/board", board)
        local src_dir = path.join(umiport_dir, "src")

        -- ボード固有の includedirs
        target:add("includedirs", board_include, {public = false})

        -- MCU に応じた startup/syscalls/linker
        -- umiport.startup = false でデフォルト startup を無効化可能
        -- 用途:
        --   - ブートローダ開発（独自ベクタテーブル・リンカスクリプトが必要）
        --   - テスト用スタブ（startup をモックに差し替え）
        --   - マルチステージブート（第2ステージのエントリポイントが異なる）
        local use_startup = target:values("umiport.startup")
        if use_startup == "false" then return end

        local mcu = target:values("embedded.mcu")
        if mcu then
            local mcu_family = mcu:match("^(stm32%a%d)")  -- stm32f4, stm32h7 etc
            if mcu_family then
                local mcu_src = path.join(src_dir, mcu_family)
                target:add("files", path.join(mcu_src, "startup.cc"))
                target:add("files", path.join(mcu_src, "syscalls.cc"))
                target:set("values", "embedded.linker_script",
                    path.join(mcu_src, "linker.ld"))
            end
        end
    end)
```

使用側（1行で完結）:

```lua
-- lib/umirtm/tests/xmake.lua
target("umirtm_stm32f4_renode")
    set_kind("binary")
    set_default(false)
    add_rules("embedded", "umiport.board")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("umiport.board", "stm32f4-renode")
    add_deps("umirtm", "umiport")
    add_files("test_*.cc")
```

外部デバイスを使うターゲット:

```lua
-- examples/stm32f4_audio/xmake.lua
target("stm32f4_audio_demo")
    set_kind("binary")
    set_default(false)
    add_rules("embedded", "umiport.board")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("umiport.board", "stm32f4-disco")
    add_deps("umiport", "umidevice")  -- 外部デバイスパッケージを追加
    add_files("audio_demo.cc")
```

### 9.2 umiport-boards → umiport 統合の利点

旧設計（umiport-boards 分離時）:

```lua
-- 旧: 2パッケージのパスを手動で毎回組み合わせ（ボイラープレート）
local umiport_stm32f4 = path.join(os.scriptdir(), "../../umiport/src/stm32f4")
local board_stm32f4_disco = path.join(os.scriptdir(), "../../umiport-boards/include/board/stm32f4-disco")
add_files(path.join(umiport_stm32f4, "startup.cc"))
add_includedirs(board_stm32f4_disco, {public = false})
```

新設計（統合後）:

```lua
-- 新: ルール1行で完結
add_rules("umiport.board")
set_values("umiport.board", "stm32f4-disco")
```

startup.cc と platform.hh が同一パッケージ内にあるため、cross-package 参照トリックが不要。

### 9.3 ボード追加時の修正箇所

新ボード追加時に変更が必要なファイル:

| ファイル | 内容 |
|---------|------|
| `umiport/include/umiport/board/<name>/platform.hh` | Platform 型定義（**新規作成**） |
| `umiport/include/umiport/board/<name>/board.hh` | ボード定数（**新規作成**） |
| `umiport/src/<mcu>/` | startup/syscalls/linker（既存 MCU なら共有） |

同じ MCU の別ボードなら startup/syscalls/linker.ld は**共有**でき、platform.hh + board.hh の2ファイル作成のみで完了する。

---

## 10. アプリケーションコード

アプリケーションのソースはボード非依存:

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
`umi::rt::detail::write_bytes()` が link-time にボードの実装にバインドされる。

---

## 11. テストとサンプルのターゲット配置

ターゲット定義は**使用側**に置く:

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

---

## 12. 命名規則

| 対象 | 命名 | 例 |
|-----|------|-----|
| HW 非依存ライブラリ | `umi<name>` | umirtm, umibench, umimmio, umihal, umitest |
| Hardware Driver（MCU） | `umiport` | umiport（単一パッケージ） |
| Hardware Driver（外部デバイス） | `umidevice` | umidevice（単一パッケージ） |
| Concept（MCU） | CamelCase | GpioPort, UartBasic, ClockTree |
| Concept（デバイス） | CamelCase | CodecBasic, AudioCodec |
| ボードディレクトリ | `board/<name>/` | board/stm32f4-renode/, board/daisy_seed/ |
| MCU サブディレクトリ | `mcu/<series>/` | mcu/stm32f4/, mcu/stm32h7/ |
| デバイスカテゴリ | `<category>/<device>/` | audio/cs43l22/, display/ssd1306/ |
| 実装型 | ベース+サフィックス | DwtTimer, UartOutput, RttOutput |

---

## 13. 拡張ガイド

### 13.1 新しいボードを追加する場合（同一 MCU）

1. `umiport/include/umiport/board/<name>/platform.hh` を作成
   - `umi::port::Platform` 型を定義（Output, Timer, init()）
   - `static_assert(umi::hal::Platform<Platform>)` で契約検証
2. `umiport/include/umiport/board/<name>/board.hh` を作成
   - 定数のみ（ピン配置、クロック設定）
3. xmake で `set_values("umiport.board", "<name>")` を指定

→ **ソース変更ゼロ。startup/syscalls/linker は既存 MCU と共有。**

### 13.2 新しい MCU を追加する場合

1. `umiport/include/umiport/mcu/<series>/` にレジスタ操作ヘッダを追加
2. `umiport/src/<series>/` に startup.cc, syscalls.cc, linker.ld を追加
3. `umiport/include/umiport/board/<board>/` にボード定義を作成
4. xmake ルールに MCU ファミリーのマッピングを追加（必要に応じて）

→ **すべて umiport 内で完結。**

### 13.3 新しい外部デバイスを追加する場合

1. `umidevice/include/umidevice/<category>/<device>/` にドライバを追加
   - `<device>.hh` — ドライバ本体（`template<Transport>`）
   - `<device>_regs.hh` — umimmio ベースのレジスタ定義
2. `umidevice/tests/` にモックテストを追加
3. ボードで使う場合、該当 board の platform.hh から `#include` して統合

→ **umiport の変更は不要。board/platform.hh のみ修正。**

### 13.4 非 ARM アーキテクチャを追加する場合

1. `umiport/include/umiport/<arch>/` にアーキテクチャ共通コードを追加
   - 例: `umiport/include/umiport/riscv/` — RISC-V 共通
2. `umiport/include/umiport/mcu/<series>/` に MCU 固有コードを追加
3. `umiport/src/<mcu>/` にそのアーキ用の startup/linker を追加
4. `umiport/src/<mcu>/write.cc` で `umi::rt::detail::write_bytes()` を実装
   - **`_write()` syscall は不要**（newlib を使わない環境）

### 13.5 ホスト/WASM ターゲットを追加する場合

1. `umiport/include/umiport/board/host/platform.hh` を作成
2. `umiport/src/host/write.cc` で `umi::rt::detail::write_bytes()` を実装
3. startup / linker は不要

→ 出力経路が link-time 注入なので、`_write()` syscall に依存しない。

### 13.6 デュアルコア MCU（将来課題）

STM32H755（CM7 + CM4）のようなデュアルコア MCU は、現時点で対応 MCU がないため未実装。

方針:
- `board/stm32h755-<name>/platform.hh` 内で両コアの型を定義する拡張が自然
- コアごとに Platform 型を分けるか、単一 Platform 内で `using Core0 = ...` / `using Core1 = ...` とするかは、実際のユースケースに基づいて決定
- アーキテクチャ共通コード（`arm/cortex-m/`）は単一コア前提のまま維持し、マルチコア固有の同期プリミティブ等は必要時に追加

---

## 14. まとめ

| もの | 場所 | 理由 |
|------|------|------|
| Concept 定義（MCU + Device） | umihal | 完全独立、0 依存、対称構造 |
| アーキ共通コード | umiport/arm/cortex-m/ | DWT, SCB 等のコア共通 |
| MCU レジスタ操作 | umiport/mcu/<series>/ | 純粋な HW 実装、パッケージ爆発防止 |
| ボード定義 | umiport/board/<name>/ | platform.hh + board.hh（MCU × Device の統合点） |
| startup / syscalls / linker | umiport/src/<mcu>/ | ボード統合の一部（MCU 間共有可） |
| 外部デバイスドライバ | umidevice/<category>/<device>/ | MCU 非依存。IC に帰属する知識 |
| ライブラリ本体 | umirtm, umibench, umimmio | HW 非依存 |
| テスト基盤 | umitest | HW 非依存 |
| ターゲット定義 | tests/, examples/ | 使用側に配置 |

---

## 15. 移行指針（現状からの移行）

| 現状 | 移行先 | 状態 |
|------|--------|------|
| umiport-boards パッケージ | umiport/include/umiport/board/ に統合 | 要統合 |
| `umiport-boards/include/board/` | `umiport/include/umiport/board/` | 要移動 |
| `umiport-boards/src/stm32f4/` | `umiport/src/stm32f4/` | 統合済み（元の位置） |
| `umiport/include/umiport/stm32f4/uart_output.hh` | `umiport/include/umiport/mcu/stm32f4/uart_output.hh` | 要移動 |
| 各lib tests/xmake.lua のボイラープレート | `umiport.board` ルール使用に置換 | 要リファクタ |
| umibench 固有 platform.hh | Platform::Timer 統合後に削除 | 要統合 |
| `_write()` 固定経路 | `umi::rt::detail::write_bytes()` + syscall 併存 | 要追加 |
| umihal Uart concept | UartBasic / UartAsync 分離 | 要分割 |
| umihal AudioCodec concept | CodecBasic / CodecWithVolume / AudioCodec 階層化 | 要分割 |
| platform.hh に static_assert なし | `static_assert(umi::hal::Platform<Platform>)` 追加 | 要追加 |
| umidevice が ARCHITECTURE に不在 | 本文書で Hardware Driver Layer として明記 | 完了 |
| umidevice のカテゴリ分類なし | `audio/`, `display/`, `sensor/` サブディレクトリ | 要導入（※1） |
| Result<T> 型が未定義 | `umihal/include/umihal/result.hh` に定義 | 要追加 |
| Transport Concept が未定義 | `umihal/include/umihal/concept/transport.hh` に定義 | 要追加 |
| MCU 差異に `#ifdef` 使用 | constexpr + `if constexpr` に移行 | 要移行 |

> **※1 umidevice カテゴリ導入の移行手順:**
> `include/umidevice/cs43l22/` → `include/umidevice/audio/cs43l22/` 等の移動は include パスの破壊的変更を伴う。
> 手順: (1) 新パスにファイルを移動 (2) 旧パスに forwarding header を配置（`#include <umidevice/audio/cs43l22/cs43l22.hh>`）(3) 全参照箇所を新パスに更新 (4) forwarding header を削除。
> umidevice は現時点で外部利用者がいないため、forwarding header なしの即時移行も可。

### 移行の推奨実行順序

依存関係を考慮した段階的移行順序:

| Phase | 作業 | 理由 |
|:-----:|------|------|
| **1** | umihal Concept 分割（UartBasic/Async, Codec 階層, Transport, Platform） | 他の全作業の前提。Concept が確定しないと static_assert も実装検証もできない |
| **2** | Result<T> の ErrorCode 拡張（NACK, BUS_ERROR 追加） | Phase 1 の Concept が Result<T> を参照する |
| **3** | umiport-boards → umiport 統合 + MCU ヘッダの `mcu/` 配置 + board.hh 作成 | パッケージ構造の確定。以降の作業はこの構造を前提とする |
| **4** | umidevice カテゴリ化（`audio/` サブディレクトリ導入） | Phase 3 と独立して実行可能だが、platform.hh の統合例コードと整合させる |
| **5** | `write_bytes()` link-time 注入 + platform.hh に static_assert 追加 | Phase 1 の Platform Concept + Phase 3 の構造が前提 |
| **6** | xmake `umiport.board` ルール作成 + 各ターゲットのリファクタ | Phase 3, 5 の完了後にビルド設定を最適化 |
| **7** | umibench 二重 Platform 解消 + `#ifdef` → constexpr 移行 | 最終クリーンアップ |

Phase 3 と Phase 4 は並行実行可能。Phase 1〜2 は他のすべてに先行する。

---

## 付録 A: 設計決定の根拠

本設計は7つのアーキテクチャレビュー + 6つの device/separation レビュー + 3つの最終版レビューの分析結果を統合して策定された。

### A.1 なぜ umiport-boards を廃止して統合するのか

旧設計: `umiport`（MCU レジスタ）+ `umiport-boards`（ボード定義 + startup）の2パッケージ

問題点（6件中4件が指摘）:
- umiport は umiport-boards なしでは動かない（startup.cc が platform.hh を参照）
- umiport-boards は umiport なしでは動かない（platform.hh が umiport のヘッダを参照）
- **単独で意味を持たないものを2つに分けている** — これは分離ではなく分散
- xmake で常に両パッケージのパスを手動で組み合わせるボイラープレートが発生
- startup.cc の配置がどちらに置いても cross-package 参照を生む

解決: umiport に統合し、`board/` サブディレクトリとして吸収。ディレクトリ構造で内部的な責務分離は維持される。

**判定基準:** パッケージ分割は「独立して使える単位」でやるべき。umiport / umiport-boards はこの基準を満たさない。

### A.2 なぜ umidevice は独立パッケージなのか

候補:
- A: umiport/device/ に統合 → MCU 依存コードと MCU 非依存コードの混在。不適切
- B: umiport-boards に統合 → IC 知識をボード層に入れる帰属先の誤り。不適切
- C: 独立パッケージ維持 → **最適**

根拠（全レビュー一致）:
- **知識の帰属先が異なる** — IC に帰属する知識は MCU にもボードにも属さない
- **MCU 非依存が本質的な強み** — CS43L22 ドライバはどの MCU でも動作する
- **単独で意味を持つ** — Mock Transport でホストテスト完全可能
- **umihal が対称構造を定義** — MCU Concept と Device Concept が同居していることが、実装層での同格を示唆

### A.3 なぜパッケージを分割しないのか（umiport 一本化）

旧設計: `umiport-arch`, `umiport-stm32`, `umiport-stm32f4`, `umiport-stm32h7`... → MCU 1シリーズにつき 1パッケージ

問題点（4/7 レビューが指摘）:
- 10 MCU で 10+ パッケージになり依存管理が複雑化
- 4段の依存チェーン（arch→stm32→stm32f4→boards）に対して共有コードが極めて少ない
- 存在しないパッケージが3つある（arch, stm32, stm32f4）のに、ドキュメントでは「ある」かのように記述

解決: umiport 内のサブディレクトリ（`arm/cortex-m/`, `mcu/stm32f4/`）で分離し、パッケージは 1つに留める。ヘッダオンリーなので、使わない MCU のコードはビルドに影響しない。

### A.4 なぜ link-time 注入なのか

旧設計: `_write()` newlib syscall 経由で出力

問題点（5/7 レビューが指摘）:
- WASM: newlib syscall が存在しない
- ESP-IDF: `esp_log` システムと衝突
- ホスト: stdout 直書きが自然、syscall 不要
- ベアメタル RISC-V: picolibc 使用時に形式が異なる

解決: `umi::rt::detail::write_bytes()` を link-time 注入点とし、各環境が独自に定義。`_write()` syscall は Cortex-M + newlib 環境の**一実装**に格下げ。

---

## 付録 B: レビューから採用した設計要素

各レビューから ARCHITECTURE_FINAL.md に反映した独自の貢献:

| レビュアー | テーマ | 採用した要素 |
|-----------|--------|------------|
| ChatGPT | アーキ | config.hh 統合の発想 |
| Gemini | アーキ | 詳細なディレクトリ構造提案 |
| Kimi | アーキ | ClockTree Concept、Policy-based Design |
| Claude Web | アーキ | 非 ARM 拡張性の厳格な評価 |
| Opus 4.6 | アーキ | Concept 分割（Basic/Async）、パッケージ爆発防止 |
| Claude Code | アーキ | 二重 Platform 問題の指摘、xmake ルール設計 |
| Kilo | アーキ | 優先度付きアクションプラン |
| Kimi | device | 2フェーズ初期化、エラー型の階層化、合成 > 継承 |
| Opus 4.6 | device | 知識の帰属先フレームワーク、Transport の本質的差異分析 |
| Claude Code | device | TransportTag による統一分類、AudioCodec concept 階層化 |
| Kimi | separation | startup.cc のボード統合コードとしての性質分析 |
| Opus 4.6 | separation | 「単独で意味を持つか」を分離正当性の判定基準として確立 |
| Claude Code | separation | 不可分なものの分離 = 分散であるという指摘 |
| ChatGPT | final | Result/Error 設計の必要性、platform.hh 肥大化リスク、CI 規約チェック |
| Claude | final | Transport Concept の具体定義、Result<T> の所属明確化、IDE 親和性 |
| Gemini | final | S ランク承認、startup 上書き許可、MCU 微細差異への対処（constexpr で解決） |
