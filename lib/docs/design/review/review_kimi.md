# umiport アーキテクチャレビュー (Kimi)

## 概要

本ドキュメントは umiport アーキテクチャ設計の拡張性、将来のハードウェア追加時の問題リスク、および設計の美しさについて評価したものである。

---

# 総合評価

**現在の設計品質: A-**

このアーキテクチャは非常に高品質な設計であり、以下の点で優れている：

* 同名ヘッダによる実装切り替え（プリプロセッサ地獄を回避）
* 明確な依存関係の階層化（循環依存なし）
* Renodeを「仮想ボード」として統一する視点
* ライブラリ（umirtm/umibench/umimmio）の完全なHW非依存化

一方で、**将来の拡張で問題が発生する可能性のある箇所**が数点存在する。

---

# 強み

## 1. 同名ヘッダによる実装切り替え

```cpp
// 優れた設計: xmakeのincludedirsで解決
#include <stm32f4/gpio.hh>
#include <arch/cm4/context.hh>
#include <board/stm32f4-disco/board.hh>
```

プリプロセッサ条件分岐を完全に排除し、クリーンなC++コードを実現。

## 2. 依存関係の階層が明確

```
umihal (0 deps)
    ↑
umiport-arch (deps: umihal)
    ↑
umiport-stm32 (deps: umihal, umiport-arch)
    ↑
umiport-stm32f4 (deps: umihal, umiport-arch, umiport-stm32, umimmio)
    ↑
umiport-boards
```

各層の責務が明確で、下位層は上位層を知らない。

## 3. 出力経路のシンプルさ

```
rt::println("Hello {}", 42)
    ↓
::_write(1, buf, len)  ← newlib syscall
    ↓
umi::port::Platform::Output::putc(c)
    ↓
├─ stm32f4-renode: USART1
├─ stm32f4-disco:  RTT
└─ host:           stdout
```

ライブラリがHWを知らない徹底ぶりが優秀。

---

# 将来問題になり得る点

## 1. umiport-arch の設計が曖昧

### 問題: context.hh の配置

```
lib/umiport-arch/
└── include/arch/
    └── cm4/
        └── context.hh   ← ここで問題が発生する可能性
```

コンテキスト保存の内容は**MCUによって異なる**：
* Cortex-M4 + FPUあり（STM32F4）
* Cortex-M4 + FPUなし（一部のSTM32F3）
* 同じCM4でもTrustZone有無で異なる

**懸念**: `cm4/` だけではなく、実際にはMCUシリーズごと（`stm32f4/` など）に定義が必要になる可能性が高い。

### 推奨対応

```cpp
// 案1: umiport-arch内でバリエーションを持つ
arch/cm4/
├── context/
│   ├── base.hh      // 基本コンテキスト（FPUなし）
│   └── fpu.hh       // FPU対応コンテキスト
└── fpu.hh

// 案2: MCU層に移動
stm32f4/
├── context.hh       // STM32F4特有のコンテキスト定義
└── ...
```

---

## 2. umiport-stm32 のファミリー共通層が薄すぎる

### 問題: 実際のレジスタ互換性

```cpp
// umiport-stm32/include/stm32/
├── rcc_common.hh    // ← 懸念: F4とH7はRCCが完全に異なる
└── gpio_common.hh   // ← これはある程度共通
```

STM32の各シリーズ間でのレジスタ互換性：

| 周辺機器 | F4 | H7 | G0 | G4 | 互換性 |
|---------|----|----|----|----|--------|
| GPIO    | ○  | △  | ○  | ○  | 高い   |
| RCC     | ✗  | ✗  | ✗  | ✗  | 低い   |
| UART    | △  | △  | ○  | ○  | 中程度 |
| DMA     | ✗  | ✗  | ✗  | ✗  | 低い   |

**予測**: `rcc_common.hh` は実用的でない可能性が高く、各シリーズで完全に別実装になる。

### 推奨対応

```cpp
// クロック設定をConceptとして定義
// umihal/include/umihal/concept/clock.hh
namespace umihal::concept {

template<typename T>
concept ClockTree = requires {
    { T::system_clock() } -> std::same_as<uint32_t>;
    { T::ahb_clock() } -> std::same_as<uint32_t>;
    { T::apb1_clock() } -> std::same_as<uint32_t>;
};

} // namespace umihal::concept

// 各シリーズで独立実装
// stm32f4/include/stm32f4/clock.hh
struct ClockTree { /* F4固有の実装 */ };

// stm32h7/include/stm32h7/clock.hh  
struct ClockTree { /* H7固有の実装 */ };
```

---

## 3. クロック設定の抽象化が不足

### 問題: 現在の設計ではクロック設定の場所が不明確

```cpp
// platform.hh
struct Platform {
    using Output = stm32f4::UartOutput;
    static void init() { Output::init(); }  // クロック設定は？
};
```

* `startup.cc` 内で行うと、ボードごとの違い（外部水晶あり/なし、周波数）に対応できない
* ボード層で行うと、依存関係が複雑化

### 推奨対応

```cpp
// board/stm32f4-renode/platform.hh
#include <stm32f4/clock.hh>

struct Platform {
    using Clock = stm32f4::ClockTree;
    using Output = stm32f4::UartOutput;
    
    static void init() {
        // ボード固有のクロック設定
        Clock::init_hse(25000000);  // 25MHz外部水晶
        Clock::configure_pll(168000000);  // 168MHz
        Output::init(Clock::apb2_clock());
    }
};
```

---

## 4. 複数MCUを搭載するボードへの対応

### 問題: 依存関係の逆転

```
現在: umiport-boards → umiport-stm32f4
```

**将来の問題**: WiFiモジュール内蔵ボードなど、複数MCUを搭載するボードへの対応が困難。

```cpp
// 例: WiFi内蔵ボード（メインMCU + WiFiモジュール）
// どちらのplatform.hhを使うか？
```

### 推奨対応

```cpp
// 複数プラットフォームをサポート
struct Platform {
    struct MainMcu {
        using Output = stm32f4::UartOutput<1>;
    };
    struct WiFiModule {
        using Output = esp32::UartOutput;  // 別のMCU
    };
    
    static void init() {
        MainMcu::Output::init();
        WiFiModule::Output::init();
    }
};
```

---

## 5. テスト戦略の不足

### 問題: 現在の設計ではユニットテストが困難

HW非依存ライブラリ（umirtm）はテスト可能だが、ボード依存コードのテスト方法が不明確。

### 推奨対応

```cpp
// モックボードの定義
// umiport-boards/include/board/mock-stm32f4/
struct MockPlatform {
    struct Output {
        static std::vector<char> buffer;
        static void putc(char c) { buffer.push_back(c); }
    };
};

// テストでの使用
TEST(UartTest, BasicOutput) {
    MockPlatform::Output::buffer.clear();
    rt::println("test");
    EXPECT_EQ(MockPlatform::Output::buffer, std::vector{'t','e','s','t','\n'});
}
```

---

# 拡張性評価

| 項目 | 評価 | コメント |
|------|------|---------|
| MCU追加 | 良好 | 層構造が明確で追加しやすい |
| Board追加 | 良好 | platform.hhパターンが確立されている |
| マルチコア対応 | 要改善 | Cortex-A系（Linux/ベアメタル）への拡張が未設計 |
| RTOS対応 | 要改善 | startup/syscallsがRTOSと衝突する可能性 |
| クロック設定 | 要改善 | 抽象化が不足 |
| コンテキスト保存 | 要注意 | arch層の責務範囲が不明確 |

---

# 改善提案

## 改善1: ClockTree Conceptの追加

```cpp
// umihal/include/umihal/concept/clock.hh
#pragma once
#include <concepts>
#include <cstdint>

namespace umihal::concept {

template<typename T>
concept ClockSource = requires {
    { T::enable() } -> std::same_as<void>;
    { T::disable() } -> std::same_as<void>;
    { T::is_ready() } -> std::same_as<bool>;
    { T::get_frequency() } -> std::same_as<uint32_t>;
};

template<typename T>
concept ClockTree = requires {
    { T::init() } -> std::same_as<void>;
    typename T::SysClk;
    typename T::HClk;
    typename T::PClk1;
    typename T::PClk2;
    requires ClockSource<typename T::SysClk>;
    requires ClockSource<typename T::HClk>;
};

} // namespace umihal::concept
```

**効果**:
* 各ボードで統一的なクロック設定が可能
* 周辺機器がボード非依存でクロック周波数を取得可能

---

## 改善2: Board定義の標準化

```cpp
// board/stm32f4-renode/
├── platform.hh       // Platform型定義（必須）
├── clock_config.hh   // クロック設定（必須）
├── pin_mapping.hh    // ピン配置（オプション）
└── memory_map.hh     // メモリマップ（オプション）
```

各ボードが提供すべきインターフェースを明確化：

```cpp
// platform.hh 必須要素
template<typename T>
concept BoardPlatform = requires {
    typename T::Output;           // putc()を持つ型
    { T::init() } -> std::same_as<void>;
    requires requires { T::Output::putc(char{}); };
};
```

---

## 改善3: コンテキスト定義の再配置

```cpp
// 提案: arch層を細分化
lib/
├── umiport-core/          // 最上位概念のみ
│   └── concept/
│       ├── context.hh     // Context概念定義
│       └── platform.hh    // Platform概念定義
│
├── umiport-cm4/           // Cortex-M4固有（FPUオプション付き）
│   ├── context_base.hh    // 基本コンテキスト
│   └── context_fpu.hh     // FPU対応コンテキスト
│
└── umiport-stm32f4/       // MCU固有
    └── context.hh         // STM32F4特有の設定（FPU有無の判定）
```

---

## 改善4: 初期化順序の明示化

```cpp
// 現在: platform.hhで一元管理
struct Platform {
    static void init() {
        // 順序が暗黙的
        Clock::init();
        Output::init();
    }
};

// 提案: 初期化ステージを明示
struct Platform {
    enum class Stage {
        Clock,      // クロック設定
        Memory,     // メモリ/キャッシュ
        Peripherals,// 周辺機器
        Output      // 出力経路
    };
    
    template<Stage S>
    static void init_stage();
    
    static void init() {
        init_stage<Stage::Clock>();
        init_stage<Stage::Memory>();
        init_stage<Stage::Peripherals>();
        init_stage<Stage::Output>();
    }
};
```

---

# 代替アプローチ

## アプローチ1: Policy-based Design

```cpp
// よりC++らしい、コンパイル時に全て解決
template<typename ClockPolicy, typename OutputPolicy>
struct Platform : ClockPolicy, OutputPolicy {
    static void init() {
        ClockPolicy::init();
        OutputPolicy::init();
    }
};

// ボード定義
using Stm32f4Renode = Platform<
    stm32f4::HseClock<25000000, 168000000>,
    stm32f4::UartOutput<1>
>;

// 型エイリアスではなく、継承による実装再利用
```

**利点**:
* コンパイル時に全て解決（ゼロオーバーヘッド）
* 複数設定の組み合わせが容易
* IDEの補完が効きやすい

**欠点**:
* テンプレートエラーの可読性
* コンパイル時間増加の可能性

---

## アプローチ2: デバイスツリー風設定

```yaml
# boards/stm32f4-renode/board.yaml
mcu: stm32f407vg
clock:
  source: hse
  external_freq: 25000000
  sysclk: 168000000
  ahb_prescaler: 1
  apb1_prescaler: 4
  apb2_prescaler: 2
peripherals:
  uart1:
    tx_pin: PA9
    rx_pin: PA10
    baudrate: 115200
```

```cpp
// コード生成またはconstexprパース
#include <board_config.gen.hh>  // YAMLから生成されたヘッダ
```

**利点**:
* 設定とコードの分離
* ツールによる検証可能

**欠点**:
* ビルドシステムの複雑化
* デバッグの困難さ

---

# 結論

## 維持すべき設計原則

1. **ライブラリはHWを知らない**（umirtm/umibench/umimmioの0依存）
2. **統合はboard層のみで行う**
3. **同名ヘッダによる実装切り替え**（ただし一元化を検討）

## 優先度付き改善項目

| 優先度 | 項目 | 理由 |
|--------|------|------|
| 🔴 高 | ClockTree Concept追加 | 各ボードでクロック設定が必須 |
| 🔴 高 | context.hh配置の再検討 | FPU有無などで問題が確実に発生 |
| 🟡 中 | umiport-stm32の実用性検証 | RCC共通部が実際に使えるか |
| 🟡 中 | board/サブフォルダ構造標準化 | clock_config.hhなど追加 |
| 🟢 低 | テスト用モックボード定義 | ユニットテスト用 |

## 最終評価

この設計は**採用価値が非常に高い**。ただし、**クロック設定の抽象化**と**コンテキスト保存の扱い**を明確にすることで、さらに堅牢で長期的に安定したポータブル基盤になる。
