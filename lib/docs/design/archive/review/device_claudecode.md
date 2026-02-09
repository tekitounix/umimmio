****# umidevice の位置づけ分析 (Claude Code)

**レビュー日:** 2026-02-08
**論点:** umidevice は MCU ペリフェラルと同列か？ umiport との関係性、統合 vs 分離の判断

---

## 1. 問いの整理

ユーザーの直感は「外部デバイスドライバは MCU ペリフェラルドライバと同列にある」というものである。この直感を検証するために、以下の3つの問いに分解する。

1. **本質論**: umidevice と umiport/mcu/ は何が同じで何が違うのか？
2. **配置論**: umidevice は umiport に含めるべきか、分離すべきか？
3. **設計論**: ARCHITECTURE_FINAL.md における umidevice の不在は問題か？

---

## 2. 現状の実装分析

### 2.1 umidevice の構造

```
lib/umidevice/
├── include/umidevice/
│   ├── ak4556/ak4556.hh              # GPIO リセットのみ（レジスタなし）
│   ├── cs43l22/
│   │   ├── cs43l22.hh                # template<Transport> — umimmio Transport 注入
│   │   └── cs43l22_regs.hh           # umimmio ベースのレジスタ定義
│   ├── pcm3060/
│   │   ├── pcm3060.hh                # template<I2cWrite, I2cRead> — ラムダ注入
│   │   └── pcm3060_regs.hh           # umimmio ベースのレジスタ定義
│   └── wm8731/
│       ├── wm8731.hh                 # template<I2cWrite16> — ラムダ注入
│       ├── wm8731_regs.hh            # umimmio ベースのレジスタ定義
│       └── wm8731_transport.hh       # 7bit addr + 9bit data パッキング
├── tests/test_drivers.cc             # モックテスト（34/34 通過）
└── xmake.lua                         # headeronly, standalone 対応
```

依存: `umimmio` のみ。MCU 固有コード: **ゼロ**。

### 2.2 umiport/mcu/ の構造（ARCHITECTURE_FINAL.md 計画）

```
lib/umiport/
└── include/umiport/
    ├── arm/cortex-m/                  # アーキテクチャ共通
    │   ├── cortex_m_mmio.hh           # DWT/CoreDebug レジスタ
    │   └── dwt.hh                     # サイクルカウンタ
    └── mcu/                           # MCU 固有
        └── stm32f4/
            └── uart_output.hh         # USART1 レジスタ操作
```

依存: `umimmio`。MCU 固有コード: **全体が MCU 固有**。

### 2.3 umihal における concept の配置

umihal には MCU ペリフェラル向けと外部デバイス向けの concept が**同居**している:

| Concept | 対象 | ファイル |
|---------|------|---------|
| `GpioPin` | MCU 内蔵 GPIO | `umihal/gpio.hh` |
| `I2cMaster` | MCU 内蔵 I2C | `umihal/i2c.hh` |
| `I2sMaster` | MCU 内蔵 I2S | `umihal/i2s.hh` |
| `Timer` | MCU 内蔵タイマ | `umihal/timer.hh` |
| **`AudioCodec`** | **外部コーデック** | `umihal/codec.hh` |
| `AudioDevice` | MCU/外部問わず | `umihal/audio.hh` |
| `BoardSpec` | ボード定数 | `umihal/board.hh` |

**umihal 自身が「MCU ペリフェラルと外部デバイスは同格」と宣言している。** concept 層で区別がないことは、実装層でも同格であるべきことを示唆する。

---

## 3. 本質論: 何が同じで何が違うか

### 3.1 同質性

| 観点 | MCU ペリフェラル | 外部デバイス |
|------|-----------------|-------------|
| 本質 | ハードウェアレジスタ操作 | ハードウェアレジスタ操作 |
| 抽象化基盤 | umimmio (`DirectTransportTag`) | umimmio (`I2CTransportTag`) |
| レジスタ定義 | `Device<RW, DirectTransportTag>` | `Device<RW, I2CTransportTag>` |
| フィールド操作 | `Field<Reg, bit, width>` | `Field<Reg, bit, width>` |
| 形態 | ヘッダオンリー | ヘッダオンリー |
| 決定者 | ボードが MCU を選ぶ | ボードがコーデックを選ぶ |
| HAL 契約 | `umihal::UartBasic` 等 | `umihal::AudioCodec` |
| テスト方法 | モックで検証可能 | モックで検証可能 |

**同じ umimmio 言語でレジスタを記述し、同じ umihal で契約を定義し、同じモックパターンでテストする。** レジスタモデルとしては完全に同質である。

### 3.2 異質性

| 観点 | MCU ペリフェラル | 外部デバイス |
|------|-----------------|-------------|
| トランスポート | メモリマップド（CPU 直結） | バスプロトコル（I2C/SPI 経由） |
| アドレス空間 | CPU 固有（0x40000000〜） | バスアドレス（0x1A, 0x46 等） |
| **MCU 依存性** | **強い（MCU シリーズ固有）** | **なし（どの MCU でも動作）** |
| レイテンシ | 1 CPU サイクル | バスクロック依存（数十〜数百 us） |
| エラー可能性 | ほぼゼロ（固定アドレス） | 通信エラーあり（NACK, タイムアウト） |
| 再利用性 | 同 MCU シリーズ内のみ | **全 MCU 共通** |

### 3.3 核心の差異

**MCU ペリフェラルは特定の MCU に束縛されるが、外部デバイスは MCU に束縛されない。**

- `stm32f4/uart_output.hh` は STM32F4 でしか使えない
- `cs43l22/cs43l22.hh` は STM32F4 でも STM32H7 でも RP2040 でも使える

この差異がパッケージ設計を決定づける。

---

## 4. 配置論: 統合 vs 分離

### 4.1 選択肢の比較

#### A. umiport に統合 (`umiport/device/`)

```
umiport/include/umiport/
├── arm/cortex-m/
├── mcu/stm32f4/
└── device/           ← 外部デバイスを統合
    ├── cs43l22/
    └── wm8731/
```

**問題点:**

1. **MCU 依存コードと MCU 非依存コードの混在** — umiport の性質が不明確になる。`mcu/` は MCU 固有、`device/` は MCU 非依存。同一パッケージ内で依存性の性質が正反対
2. **名前の不一致** — 「port（移植）」に外部デバイスドライバがあるのは意味的に不自然。CS43L22 は何かを「ポート」しているわけではない
3. **スケール問題** — 将来ディスプレイ、センサー、EEPROM 等を追加すると umiport が雑多なドライバ集合体になる
4. **独立運用の喪失** — umidevice は現在 `standalone_repo` 対応で単体ビルド可能。この独立性を失う

**判定: 不適切。**

#### B. umiport-boards に統合

```
umiport-boards/
├── include/board/stm32f4-disco/
│   ├── platform.hh
│   └── devices/cs43l22.hh    ← ボード固有デバイス
└── include/udev/              ← 汎用デバイスドライバ
    └── audio/pcm5102.hh
```

**問題点:**

1. **汎用ドライバの配置矛盾** — CS43L22 は STM32F4-Discovery 固有だが、CS43L22 のドライバ自体はどのボードでも使える。ボードパッケージに汎用ドライバを入れるのは不自然
2. **umimmio 依存の方向** — umiport-boards は「統合層」であり、ドライバ実装を含む場所ではない。責務の逸脱
3. **テストの複雑化** — umiport-boards はビルドターゲットの統合層であり、そこにドライバのユニットテストが混在すると構造が不明瞭

**判定: 不適切。**

#### C. 独立パッケージ維持 (`umidevice`)

**利点:**

1. **MCU 依存性ゼロという性質が名前から明確** — `umidevice` は `umiport` と並列であり、`umiport` に含まれないことが「MCU 非依存」を暗示する
2. **責務が明確** — 外部デバイスのレジスタ操作のみ。MCU ペリフェラルもボード統合も含まない
3. **独立テスト** — モック I2C/GPIO で完全にホストテスト可能。他パッケージの影響を受けない
4. **独立配布** — `standalone_repo` 対応で単体リポジトリとして運用可能
5. **拡張性** — デバイスが増えても umiport や umiport-boards は一切変更不要

**欠点:**

- パッケージ数が増える（ただし本質的に異なるものなので正当）

**判定: 最適。**

### 4.2 判定根拠: MCU 所属 vs ボード所属

**MCU ペリフェラルは「MCU に属する」。外部デバイスは「ボードに属する」。**

- USART1 は STM32F4 の一部 → `umiport/mcu/stm32f4/`
- CS43L22 は STM32F4-Discovery ボードの構成部品 → ボード層で統合
- CS43L22 **ドライバ自体** はどのボードでも使える → `umidevice/`（独立）

この三段階の区別が分離を正当化する:

```
ドライバ実装（MCU 非依存）  → umidevice
ドライバ実装（MCU 固有）    → umiport/mcu/
ドライバの統合（ボード固有） → umiport-boards/board/
```

---

## 5. 設計論: ARCHITECTURE_FINAL.md の不備

### 5.1 現在の依存関係図（umidevice 不在）

ARCHITECTURE_FINAL.md の依存図:

```
umihal (0 deps)
    ↓
umiport
    ↓
umiport-boards
    ↓
application
```

**umidevice が完全に欠落している。** これは設計文書としての重大な不備である。

### 5.2 あるべき依存関係図

```
umihal (0 deps)  ─── Concept 定義（MCU + 外部デバイス両方の契約）
    │
    ↓ (satisfies)
┌──────────────────────────────────────┐
│  Hardware Driver Layer               │
│                                      │
│  umiport          umidevice          │
│  (MCU 内部)       (外部デバイス)      │
│  ┌────────────┐   ┌──────────────┐   │
│  │ arm/cortex │   │ cs43l22      │   │
│  │ mcu/stm32f4│   │ wm8731       │   │
│  └────────────┘   │ pcm3060      │   │
│                   │ ak4556       │   │
│                   └──────────────┘   │
│                                      │
│  共通基盤: umimmio                    │
└──────────────────────────────────────┘
    │                │
    ↓                ↓
umiport-boards  ─── 統合層（platform.hh で MCU + 外部デバイスを結合）
    ↓
application / tests / examples

umirtm (0 deps)  ─── HW 非依存ユーティリティ
umibench (0 deps)
```

**ポイント:** umiport と umidevice は**同格の Hardware Driver Layer** として並列に描く。両者は独立だが、同じ umimmio 基盤の上に立ち、同じ umihal で契約を定義する同列のドライバ群。

### 5.3 ボード統合パターン

platform.hh が MCU ドライバと外部デバイスドライバの両方を統合する場:

```cpp
// umiport-boards/include/board/stm32f4-disco/platform.hh
#include <umiport/mcu/stm32f4/uart.hh>    // MCU ドライバ
#include <umiport/arm/cortex-m/dwt.hh>    // アーキ共通
#include <umidevice/cs43l22/cs43l22.hh>   // 外部デバイス ★

namespace umi::port {
struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;
    using Codec  = umi::device::CS43L22Driver<I2cTransport>;  // ★ 統合点
    static void init() { ... }
};
}
```

**umidevice は umiport を知らない。umiport は umidevice を知らない。** 両者がボード層（platform.hh）で初めて出会う。これは ARCHITECTURE_FINAL.md の「統合は board 層のみで行う」原則に完全に合致する。

---

## 6. 深掘り: トランスポート抽象化の統一性

### 6.1 現状の問題 — API スタイルの不統一

4つのドライバが4つの異なる注入パターンを使っている:

| ドライバ | 注入方式 | シグネチャ |
|---------|---------|-----------|
| AK4556 | テンプレートクラス | `template<GpioDriver>` |
| PCM3060 | コンストラクタ注入（ラムダ×2） | `PCM3060Driver(I2cWrite, I2cRead)` |
| WM8731 | コンストラクタ注入（ラムダ×1） | `WM8731Driver(I2cWrite16)` |
| CS43L22 | 参照注入（umimmio Transport） | `CS43L22Driver(Transport&)` |

これは「動く」が美しくはない。しかし統一を急ぐ必要はない。

### 6.2 統一の方向性

umimmio Transport が本来のクリーンな解。CS43L22 パターンを標準化する:

```cpp
// 理想形: 全ドライバが umimmio Transport を使用
template <typename Transport>
class WM8731Driver {
    Transport& transport;
public:
    void init() {
        transport.write(Regs::RESET::value(0));
        transport.modify(Regs::POWER::OUTPD::Clear{});
        // ...
    }
};
```

AK4556 はレジスタを持たないため例外（GPIO テンプレートで問題ない）。

### 6.3 umihal AudioCodec concept との対応

現在のドライバは `umihal::AudioCodec` concept を**満たしていない**:

```cpp
concept AudioCodec = requires(T& codec, int vol_db, bool mute_state) {
    { codec.init() } -> std::convertible_to<bool>;   // CS43L22: bool ✓, PCM3060: void ✗
    { codec.power_on() } -> std::same_as<void>;       // CS43L22: ✓, PCM3060: ✗
    { codec.power_off() } -> std::same_as<void>;      // CS43L22: ✓, PCM3060: ✗
    { codec.set_volume(vol_db) } -> std::same_as<void>;
    { codec.mute(mute_state) } -> std::same_as<void>;
};
```

CS43L22 のみが concept にほぼ対応。PCM3060 と WM8731 は `power_on()`/`power_off()` を持たない。AK4556 はレジスタ制御すらない。

これは「全コーデックが同じ concept を満たす」という前提が現実に合わないことを示す。コーデックの能力は大きく異なるため、concept の階層化が必要:

```cpp
// 最小限: 初期化のみ
concept CodecBasic = requires(T& c) { { c.init() } -> std::convertible_to<bool>; };

// 音量制御あり
concept CodecWithVolume = CodecBasic<T> && requires(T& c, int db) {
    { c.set_volume(db) } -> std::same_as<void>;
};

// フル機能
concept AudioCodec = CodecWithVolume<T> && requires(T& c, bool m) {
    { c.power_on() } -> std::same_as<void>;
    { c.power_off() } -> std::same_as<void>;
    { c.mute(m) } -> std::same_as<void>;
};
```

---

## 7. ユーザーの直感「MCU と同列」への回答

### 7.1 Yes — 本質的に同列

ユーザーの直感は**正しい**。以下の証拠がそれを裏付ける:

1. **umimmio が統一基盤** — MCU レジスタ定義（`DirectTransportTag`）も外部デバイスレジスタ定義（`I2CTransportTag`）も同じ `Device<RW, Tag>` パターン
2. **umihal が同居させている** — `GpioPin`（MCU）と `AudioCodec`（外部）が同じパッケージに共存
3. **操作の本質が同じ** — レジスタのフィールドを read/write/modify するだけ。バスが直結か I2C 経由かの差
4. **テスト手法が同じ** — 両者ともモックトランスポートでホストテスト可能

### 7.2 ただしパッケージとしては分離すべき

同質であることと同一パッケージに入れるべきことは別の問題。分離の理由は**MCU 依存性の有無**という一点に帰着する:

- `umiport/mcu/stm32f4/` のファイルは **STM32F4 でしか使えない**
- `umidevice/cs43l22/` のファイルは **どの MCU でも使える**

この非対称性がパッケージ境界を決定する。MCU 非依存なコードを MCU 固有パッケージに入れると、移植性を無駄に制限し、概念的な混乱を招く。

### 7.3 アーキテクチャ上の表現

「同列」という直感は**設計文書に反映すべき**。ARCHITECTURE_FINAL.md の依存関係図で umiport と umidevice を同格の「Hardware Driver Layer」として並列に描くことで、パッケージ分離と概念的同質性を両立できる。

---

## 8. 他レビューとの比較と独自見解

### 8.1 Claude 4.6 レビューとの一致点

- 分離維持が正解
- ARCHITECTURE_FINAL.md に umidevice が欠落していることは問題
- umiport と umidevice を「Hardware Driver Layer」として同格に描くべき

### 8.2 Kimi レビューとの相違点

Kimi は「選択肢C: umiport-boards に統合」を推奨しているが、これには同意しない。理由:

1. **汎用ドライバの配置矛盾** — CS43L22 ドライバは STM32F4-Discovery 固有ではない。RP2040 ボードに CS43L22 を載せることも可能。ボードパッケージに汎用ドライバを入れるのは、そのドライバの再利用性を見えにくくする

2. **責務の逸脱** — umiport-boards は「統合層」。ドライバ実装（レジスタ操作のロジック）を含む場所ではない。ドライバの**使用**（platform.hh でのインスタンス化）と**実装**（レジスタシーケンス）は異なる責務

3. **`udev` という命名問題** — Kimi が提案する `udev` はLinux の `/dev` と紛らわしく、UMI プロジェクトの命名規則（`umi` プレフィックス）にも合わない

### 8.3 独自の見解: 「同列」の実装上の表現

両者が同列であることを、コードレベルで**もっと強く**表現する方法がある:

**umimmio の TransportTag による統一的な分類:**

```
umimmio Transport 体系
├── DirectTransportTag   → MCU ペリフェラル（umiport）
├── I2CTransportTag      → 外部 I2C デバイス（umidevice）
├── SPITransportTag      → 外部 SPI デバイス（umidevice）
└── MemoryTransportTag   → 外部メモリマップデバイス（将来）
```

Transport Tag の種類がそのまま「MCU 内部（Direct）vs 外部（I2C/SPI）」の境界線を表す。umimmio が両者の共通基盤であることが、レジスタ定義コードのレベルで可視化される。

---

## 9. 将来の拡張を考慮した評価

### 9.1 デバイス追加シナリオ

| カテゴリ | デバイス例 | バス | umidevice 追加 |
|---------|-----------|------|:--------------:|
| オーディオ CODEC | CS43L22, WM8731, PCM3060, AK4556 | I2C/GPIO | ○（現在） |
| ディスプレイ | SSD1306, ST7789 | I2C/SPI | ○ |
| センサー | BME280, MPU6050 | I2C/SPI | ○ |
| EEPROM | AT24C256 | I2C | ○ |
| 無線 | SX1276 (LoRa) | SPI | ○ |
| 電源 IC | TPS62110 | I2C | ○ |

すべて **MCU 非依存・バスプロトコル経由** であり、umidevice の Transport テンプレートパターンに完全に合致する。

### 9.2 サブカテゴリ構造の検討

デバイス数が増えた場合のディレクトリ構造:

```
lib/umidevice/include/umidevice/
├── audio/                 # オーディオコーデック
│   ├── cs43l22/
│   ├── wm8731/
│   ├── pcm3060/
│   └── ak4556/
├── display/               # ディスプレイ
│   ├── ssd1306/
│   └── st7789/
└── sensor/                # センサー
    └── bme280/
```

現在の 4 デバイスではフラット構造で問題ないが、10 デバイスを超えた時点でカテゴリ分けを検討すべき。

### 9.3 umiport に統合した場合の問題点（反実仮想）

仮に umiport/device/ に統合していた場合:

- センサードライバを追加するたびに umiport が変更される → MCU ポーティングと無関係の変更が混入
- umiport の CI が外部デバイスのテスト失敗で red になる → 責務の混在
- umiport の依存グラフに外部デバイス固有の要件が入る → 不要な複雑化

---

## 10. 結論

### 10.1 核心の回答

| 問い | 回答 |
|------|------|
| MCU ペリフェラルと同列か？ | **Yes.** 同じ umimmio 基盤、同じ umihal 契約、同じテスト手法を共有する同格のハードウェアドライバ |
| umiport に含めるべきか？ | **No.** MCU 非依存性が umidevice の本質的な強み。統合すると移植性と責務の明確さを損なう |
| umiport-boards に含めるべきか？ | **No.** ドライバ実装（レジスタ操作ロジック）は統合層の責務ではない |
| 現在の独立パッケージは妥当か？ | **Yes.** ただし ARCHITECTURE_FINAL.md に位置づけを明記すべき |

### 10.2 推奨アクション

| 優先度 | アクション |
|:------:|-----------|
| 高 | ARCHITECTURE_FINAL.md の依存関係図に umidevice を追加し、umiport と同格の「Hardware Driver Layer」として明示 |
| 高 | 各パッケージの責務表に umidevice を追加（`外部デバイスレジスタ操作 / deps: umimmio / 禁止: MCU 固有コード`） |
| 中 | ボード統合パターン（platform.hh から umidevice を使う例）を設計ガイドに記載 |
| 中 | umihal AudioCodec concept の階層化（CodecBasic → CodecWithVolume → AudioCodec） |
| 低 | ドライバ API スタイルの段階的統一（umimmio Transport パターンへの収束） |

### 10.3 一文要約

> **umidevice と umiport は「外部デバイス vs MCU 内部ペリフェラル」という対称的なドライバ層であり、パッケージとしては分離を維持しつつ、アーキテクチャ上は同格の Hardware Driver Layer として扱うのが最もクリーン。**
