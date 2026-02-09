# 外部デバイスの理想的な扱い — 原理的考察 (Claude Opus 4.6)

**レビュー日:** 2026-02-08
**論点:** 現在の umidevice 実装を忘れ、「外部デバイスドライバは理想的にはどう扱われるべきか」を原理から導出する

---

## 1. 問いの整理

「MCU ペリフェラルと同列」という直感は何を意味するか。

表面的な答え: 両方ともレジスタを操作するハードウェアドライバである。
だが、この答えは不十分。もっと深い構造的問いがある:

> **外部デバイスは「ハードウェア知識の階層」のどこに属するのか？**

---

## 2. ハードウェア知識の分類

UMI のポート層が扱う「ハードウェア知識」を、**その知識がどこに帰属するか** で分類する:

| 知識の種類 | 帰属先 | 例 | 固有性 |
|-----------|--------|-----|--------|
| アーキテクチャ知識 | CPU コア設計 | DWT サイクルカウンタ, NVIC, FPU 有無 | 全 Cortex-M4 で共通 |
| MCU 知識 | チップ設計 | STM32F4 の RCC, USART, I2S レジスタ | MCU シリーズ内で共通 |
| **デバイス知識** | **IC 設計** | **WM8731 のレジスタマップ、初期化手順** | **その IC を使う全ボードで共通** |
| ボード知識 | 基板設計 | ピン配線、クロック周波数、メモリサイズ | 1つのボード固有 |

これを見ると、外部デバイス知識の帰属先は明確: **IC そのもの（シリコンチップ）** に帰属する。MCU のシリコンに帰属するのと全く同じ構造。

ここから導かれる原理:

> **デバイス知識は MCU 知識と同格の「IC 知識」であり、ボード知識ではない。**

WM8731 のレジスタマップを「ボード層」に置くのは、STM32F4 の USART レジスタ定義を「ボード層」に置くのと同じくらい不自然。

---

## 3. Transport という本質的差異

MCU ペリフェラルと外部デバイスの唯一の本質的差異は **transport（レジスタにどう到達するか）** である:

```
MCU ペリフェラル:
  CPU ──[メモリバス]──> レジスタ（固定アドレス空間）

外部デバイス:
  CPU ──[メモリバス]──> MCU I2C ペリフェラル ──[I2C バス]──> レジスタ
```

外部デバイスは **MCU ペリフェラルを経由して** 自分のレジスタに到達する。この「間接性」が唯一の構造的差異であり、以下の結果をもたらす:

| 特性 | MCU ペリフェラル | 外部デバイス |
|------|-----------------|-------------|
| レジスタアクセス | 直接（DirectTransport） | 間接（I2C/SPI Transport） |
| アドレス解決 | コンパイル時確定 | バスアドレス + MCU I2C 実装に依存 |
| MCU 依存性 | **強い**（MCU 固有のアドレスマップ） | **なし**（トランスポートを差し替え可能） |
| 初期化順序 | クロック → ペリフェラル | クロック → バスペリフェラル → 外部デバイス |
| エラーモデル | 基本的に失敗しない | バス通信エラーがありうる |

ここからもう一つの原理が導かれる:

> **外部デバイスドライバは MCU 非依存であり、MCU ドライバとは依存の方向が異なる。**

MCU ドライバは「特定の MCU でしか動かない」。外部デバイスドライバは「どの MCU でも動く（適切な Transport があれば）」。この非対称性は本質的であり、設計に反映されるべき。

---

## 4. 理想的なアーキテクチャの導出

### 4.1 原理から導かれる配置ルール

1. **デバイス知識は IC に帰属する** → ボード層には置かない
2. **デバイスドライバは MCU 非依存** → MCU 固有コード（umiport/mcu/）には置かない
3. **デバイスドライバは Transport に依存** → Transport の concept を umihal で定義
4. **デバイス × MCU の結合はボード設計者が決める** → 結合はボード層で行う

### 4.2 理想構造

```
知識の階層:

    umihal            ─── 契約（Concept）
    ┌────────────────────────────────┐
    │  I2cMaster, SpiMaster          │  バスプロトコル
    │  AudioCodec, Display, Sensor   │  デバイスカテゴリ
    │  GpioPin, Uart, Timer          │  MCU ペリフェラル
    │  Platform                      │  ボード統合
    └────────────────────────────────┘
         ↑                  ↑
         │                  │
    ┌─────────┐      ┌─────────────┐
    │ umiport │      │  デバイス層  │
    │ (MCU)   │      │  (外部 IC)  │
    │         │      │             │
    │ mcu/    │      │ cs43l22/    │
    │ arm/    │      │ wm8731/     │
    └─────────┘      └─────────────┘
         ↑                  ↑
         │                  │
         └──────┬───────────┘
                │
         umiport-boards  ─── 統合（platform.hh で結合）
```

ここで「デバイス層」をどこに置くかが問い。

---

## 5. 配置の選択肢 — 原理的評価

### 選択肢 A: umiport 内 (`umiport/device/`)

**結合するもの:** MCU 知識とデバイス知識を同一パッケージに。

```
umiport/include/umiport/
├── arm/cortex-m/     # アーキ知識
├── mcu/stm32f4/      # MCU 知識（MCU 固有）
└── device/cs43l22/   # デバイス知識（MCU 非依存）
```

**原理との矛盾:** MCU 固有のものと MCU 非依存のものが同一パッケージに混在する。`umiport/mcu/stm32f4/` は STM32F4 でしか使えないが、`umiport/device/cs43l22/` は全 MCU で使える。この非対称性が 1 パッケージ内に隠蔽される。

ただし、**プラグマティックには許容できる**。umimmio もレジスタ抽象化だが MCU 非依存で独立パッケージにしている。MCU 非依存のものを umiport に入れること自体は「ハードウェアドライバの集合」として一貫性がある。

**評価:** 概念的に雑だが、実用上は問題ない。

### 選択肢 B: 独立パッケージ (`umidevice`)

**分離するもの:** MCU 知識とデバイス知識を別パッケージに。

```
lib/umiport/   → MCU に帰属する知識
lib/umidevice/ → 外部 IC に帰属する知識
```

**原理との整合:** 知識の帰属先が異なるものを別パッケージにする。MCU 依存 / MCU 非依存の境界が明確。

**問題:** パッケージが増える。ただし「本質的に異なるもの」を分けているだけなので、複雑性の増加ではなく明示化。

**評価:** 原理的に最もクリーン。

### 選択肢 C: umiport-boards 内 (`umiport-boards/include/udev/`)

kimi レビューが推奨した案。

**原理との矛盾:** デバイス知識は IC に帰属するが、ボード層に置いている。WM8731 のレジスタマップは「Daisy Seed のもの」ではなく「WM8731 というチップのもの」。ボード層に置くと、別ボードで同じ WM8731 を使うとき、ドライバを参照するパスが `board/` 配下になり不自然。

また、将来 10+ のデバイスが増えたとき umiport-boards が肥大化する。kimi 自身も「20 種類以上で分離を検討」と書いているが、これは配置が間違っている証拠: **正しい場所にあるものは量が増えても移動する必要がない**。

**評価:** 帰属先が間違っている。

### 選択肢 D: umimmio に統合

デバイスの本質は「レジスタ操作」であり、umimmio はレジスタ抽象化。

**原理との矛盾:** umimmio は「レジスタにアクセスする仕組み（how）」であり、デバイスドライバは「ある IC のレジスタをどう使うか（what）」。抽象化メカニズムとドメイン知識は別物。

**評価:** 責務の混同。

---

## 6. 理想の結論

### 6.1 独立パッケージが原理的に正解

デバイスドライバを独立パッケージにすべき理由を一文で述べるなら:

> **外部デバイスドライバは、MCU にも、ボードにも、アーキテクチャにも帰属しない。IC 自体に帰属する独立した知識体系である。**

独立パッケージにすることで:
- MCU を変えても外部デバイスドライバは一切変わらない（当然）
- ボードを変えても、同じ IC を使っていればドライバは共有される（当然）
- デバイスドライバの追加・削除が他のパッケージに影響しない

### 6.2 理想的なパッケージ構成

```
lib/umidevice/
├── include/umidevice/
│   ├── audio/                # オーディオコーデック
│   │   ├── cs43l22.hh
│   │   ├── wm8731.hh
│   │   └── pcm3060.hh
│   ├── display/              # ディスプレイ（将来）
│   │   ├── ssd1306.hh
│   │   └── st7789.hh
│   ├── sensor/               # センサー（将来）
│   │   └── bme280.hh
│   └── storage/              # ストレージ（将来）
│       └── w25qxx.hh
└── xmake.lua                 # headeronly, deps: umimmio
```

カテゴリでサブディレクトリを切る。各ドライバは Transport をテンプレートパラメータとして受け取り、MCU を知らない。

---

## 7. 理想的な Transport 設計

外部デバイスの中核問題は「バス越しのレジスタアクセスをどう抽象化するか」。

### 7.1 二つのアプローチ

**A. umimmio Transport 統一（レジスタモデル一元化）**

MCU ペリフェラルも外部デバイスも同じ umimmio の Device/Register/Field で記述し、Transport だけ差し替える:

```cpp
// MCU: DirectTransport（メモリマップド）
struct USART1 : mmio::Device<mmio::RW, mmio::DirectTransportTag> {
    static constexpr mmio::Addr base_address = 0x40011000;
    struct SR : mmio::Register<USART1, 0x00, 32> { ... };
};

// 外部デバイス: I2CTransport（バス経由）
struct CS43L22 : mmio::Device<mmio::RW, mmio::I2CTransportTag> {
    static constexpr uint8_t i2c_address = 0x4A;
    struct ID : mmio::Register<CS43L22, 0x01, 8> { ... };
};
```

**利点:** レジスタ操作の文法が完全に統一される。MCU ペリフェラルも外部デバイスも `transport.read(REG::FIELD{})` で読む。
**問題:** I2C Transport は MCU の I2C ドライバに依存する。Transport の注入方法が課題。

**B. Callable 注入（ドライバが Transport を知らない）**

ドライバは「レジスタを読み書きできる callable」だけを受け取る:

```cpp
template <typename Write, typename Read>
class Codec {
    Write write_reg;
    Read read_reg;
public:
    void init() { write_reg(0x02, 0x01); }
    uint8_t read_id() { return read_reg(0x01); }
};
```

**利点:** 極限まで軽量。umimmio への依存すら不要。
**問題:** 型安全性がない。レジスタアドレスが生の `uint8_t`。umimmio の Field/Value による構造化が使えない。

### 7.2 理想の判断

UMI は umimmio というレジスタ抽象化を持っており、それを活かすのが自然。

**理想: umimmio Transport 統一をベースにしつつ、レジスタなしデバイス（AK4556 のような GPIO のみの IC）は例外として Callable で扱う。**

```cpp
// レジスタありデバイス → umimmio ベース（型安全）
struct WM8731 : mmio::Device<mmio::RW, mmio::I2CTransportTag> { ... };

// レジスタなしデバイス → 最小テンプレート
template <typename Gpio>
class AK4556 {
    Gpio& gpio;
public:
    void init() { gpio.reset_assert(); /* ... */ gpio.reset_release(); }
};
```

---

## 8. 理想的な Concept 階層

umihal が定義すべきデバイスカテゴリ Concept:

```cpp
// umihal — デバイスカテゴリの契約

/// オーディオコーデック（最小）
template <typename T>
concept AudioCodecBasic = requires(T& c) {
    { c.init() } -> std::convertible_to<bool>;
    { c.power_on() } -> std::same_as<void>;
    { c.power_off() } -> std::same_as<void>;
};

/// ボリューム制御可能なコーデック
template <typename T>
concept AudioCodecWithVolume = AudioCodecBasic<T> && requires(T& c, int db) {
    { c.set_volume(db) } -> std::same_as<void>;
    { c.mute(true) } -> std::same_as<void>;
};
```

**ここが最も重要な設計判断:**

デバイスカテゴリの Concept（AudioCodec, Display 等）は umihal に置く。
デバイス固有の実装（CS43L22, WM8731 等）は umidevice に置く。

これは MCU ペリフェラルと完全に対称:

| | 契約（umihal） | MCU 実装（umiport） | デバイス実装（umidevice） |
|---|---|---|---|
| UART | `UartBasic` concept | `stm32f4::Uart1` | — |
| I2C | `I2cMaster` concept | `stm32f4::I2C1` | — |
| オーディオ | `AudioCodecBasic` concept | — | `CS43L22Driver<T>` |
| ディスプレイ | `Display` concept（将来） | — | `SSD1306<T>`（将来） |

**MCU ペリフェラルも外部デバイスも、umihal の Concept を満たす実装を提供するという構造が対称的。**

---

## 9. ボード層での統合パターン

理想的なボード統合:

```cpp
// umiport-boards/include/board/stm32f4-disco/platform.hh

#include <umihal/concept/platform.hh>

// MCU ドライバ（umiport）
#include <umiport/mcu/stm32f4/i2c.hh>
#include <umiport/mcu/stm32f4/i2s.hh>
#include <umiport/arm/cortex-m/dwt.hh>

// 外部デバイスドライバ（umidevice）
#include <umidevice/audio/cs43l22.hh>

// ボード定数
#include <board/stm32f4-disco/board.hh>

namespace umi::port {

struct Platform {
    // MCU ドライバのインスタンス化
    using I2C  = stm32f4::I2C1;
    using I2S  = stm32f4::I2S3;

    // 外部デバイスのインスタンス化（MCU ドライバを Transport として注入）
    using Codec = device::CS43L22Driver<mmio::I2cTransport<I2C>>;

    // 出力経路
    using Output = ...;
    using Timer  = cortex_m::DwtTimer;

    static void init() { ... }
};

static_assert(umihal::Platform<Platform>);

} // namespace umi::port
```

**ボード層が行うのは「MCU の I2C ドライバ」と「外部デバイスドライバ」の結合のみ。** デバイスの知識はデバイス層に、MCU の知識は MCU 層にあり、ボード層は純粋な「配線定義」としてだけ機能する。

---

## 10. 理想の依存関係図

```
                    umihal (0 deps)
               ┌─── Concept 定義 ───┐
               │                     │
         ┌─────┴─────┐      ┌───────┴───────┐
         │  umiport  │      │  umidevice    │
         │  (MCU)    │      │  (外部 IC)    │
         │           │      │               │
         │ arm/      │      │ audio/        │
         │ mcu/      │      │ display/      │
         └─────┬─────┘      └───────┬───────┘
               │                     │
               │    umimmio (0 deps) │
               │    ← 両者が利用 →   │
               │                     │
               └──────────┬──────────┘
                          │
                   umiport-boards
               ─── 統合（MCU × Device を結合）
                          │
                    application

        umirtm (0 deps)  ─── HW 非依存
        umibench (0 deps)
```

**対称性:** umiport と umidevice は umihal の Concept を満たし、umimmio をレジスタ抽象化基盤として共有する。両者は互いを知らず、ボード層で初めて出会う。

---

## 11. 「同列」の正確な意味

最初の問いに戻る: MCU ペリフェラルと外部デバイスは同列か？

**答え:**

同列 ≠ 同一パッケージ。

同列 = **同格のハードウェア知識層として、対称的な構造で扱う。**

具体的には:
1. **umihal が両者の契約を定義する**（GpioPin も AudioCodec も umihal の Concept）
2. **両者が umimmio を共通基盤として使う**（DirectTransport vs I2CTransport）
3. **両者がボード層で結合される**（platform.hh が唯一の統合点）
4. **ただしパッケージは分離する**（MCU 依存 vs MCU 非依存 の本質的差異のため）

---

## 12. まとめ

| 問い | 理想の答え |
|------|-----------|
| 外部デバイスの知識は何に帰属するか | IC（チップ）自体。MCU にもボードにも帰属しない |
| MCU ドライバと同列か | 抽象構造としては対称的な同格。パッケージとしては分離が正しい |
| umiport に入れるべきか | No。MCU 非依存のものを MCU 層に入れるのは帰属先の誤り |
| umiport-boards に入れるべきか | No。IC 知識をボード知識に混ぜるのは帰属先の誤り |
| 独立パッケージの理由 | 「量が増えたから分離」ではなく「知識の帰属先が異なるから分離」 |
| Transport の理想 | umimmio 統一。DirectTransport と I2CTransport の差し替えで MCU/外部を透過的に扱う |
| Concept の位置 | デバイスカテゴリ Concept（AudioCodec 等）は umihal。MCU Concept と対称 |
| 統合の場所 | ボード層の platform.hh のみ。ここで MCU ドライバを Transport として注入 |
