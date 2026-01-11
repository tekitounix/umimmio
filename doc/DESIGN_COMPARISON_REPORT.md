# UMI-OS 設計比較レポート

**新設計（NEW_DESIGN.md）と既存設計の詳細比較**

作成日: 2026年1月12日

---

## 参照ドキュメント

| ドキュメント | 行数 | 内容 |
|-------------|------|------|
| **NEW_DESIGN.md** | ~5,700行 | 新しい包括的仕様書 |
| **DESIGN_DETAIL.md** | ~430行 | 既存の詳細設計 |
| **README.md** | ~180行 | 既存のコンセプト説明 |
| **ADAPTER.md** | ~1,450行 | ホストアダプター設計 |
| **COMPARISON_MOS_STM32.md** | ~860行 | MOS-STM32との比較検討 |

---

## 目次

1. [エグゼクティブサマリー](#1-エグゼクティブサマリー)
2. [比較概要](#2-比較概要)
3. [アーキテクチャ比較](#3-アーキテクチャ比較)
4. [カーネル設計比較](#4-カーネル設計比較)
5. [オーディオ処理比較](#5-オーディオ処理比較)
6. [MIDI/イベント処理比較](#6-midiイベント処理比較)
7. [時間管理比較](#7-時間管理比較)
8. [メモリ管理比較](#8-メモリ管理比較)
9. [保護モデル比較](#9-保護モデル比較)
10. [プラットフォーム抽象化比較](#10-プラットフォーム抽象化比較)
11. [API設計比較](#11-api設計比較)
12. [アダプタ層比較](#12-アダプタ層比較)
13. [エラー処理比較](#13-エラー処理比較)
14. [ログ・デバッグ比較](#14-ログデバッグ比較)
15. [UI/ハードウェア抽象化比較](#15-uiハードウェア抽象化比較)
16. [コルーチン比較](#16-コルーチン比較)
17. [ドキュメント品質比較](#17-ドキュメント品質比較)
18. [新機能一覧](#18-新機能一覧)
19. [既存設計の強み（維持すべき点）](#19-既存設計の強み維持すべき点)
20. [実装上の課題](#20-実装上の課題)
21. [推奨事項](#21-推奨事項)
22. [結論](#22-結論)

---

## 1. エグゼクティブサマリー

### 1.1 総合評価

| 項目 | 既存設計 | 新設計 | 評価 |
|------|----------|--------|------|
| **規模** | ~600行 | ~5,700行 | 約10倍 |
| **対象範囲** | 組込みRTOS | マルチプラットフォームOS | 大幅拡張 |
| **詳細度** | 概念レベル | 実装レベル | 大幅向上 |
| **実用性** | プロトタイプ向け | 製品開発向け | 大幅向上 |
| **複雑性** | 低 | 高 | トレードオフ |

### 1.2 主要な改善点

1. **マルチプラットフォーム対応**: 組込み専用 → VST3/AU/CLAP/WASM対応
2. **保護モデルの体系化**: MPU有無の2パターン → L1/L2/L3の3段階
3. **時間管理の明確化**: 曖昧 → サンプル時間/システム時間の分離
4. **ログ・アサートの体系化**: 基本的 → 層別使用方針とバックエンド切替
5. **I/Oモデルの柔軟化**: 固定 → 仮想/物理I/Oマッピング
6. **ISP（診断プロトコル）**: なし → MIDI SysEx経由の診断・更新

### 1.3 既存設計の強み（維持すべき）

1. **Context経由のデータ渡し**: ADAPTER.mdで確立された設計原則
2. **コルーチン実装**: MOS-STM32より成熟した`umi::coro::Task<T>`
3. **UIサーバー分離**: Input Server / Display Serverの分離設計
4. **MOS-STM32との比較検討**: 必要十分な機能の見極め

### 1.4 懸念事項

1. 実装工数の大幅増加
2. 仕様と実装の乖離リスク
3. 小規模プロジェクトには過剰
4. 段階的実装戦略が必要

---

## 2. 比較概要

### 2.1 ドキュメント構成

| 既存設計 | 新設計 |
|----------|--------|
| README.md (コンセプト) | NEW_DESIGN.md (包括的仕様書) |
| DESIGN_DETAIL.md (詳細設計) | - |
| ADAPTER.md (アダプタ) | NEW_DESIGN.mdに統合 |

### 2.2 セクション対応表

| 既存設計のセクション | 新設計での対応 | 変化 |
|----------------------|----------------|------|
| 構成モデル (DESIGN_DETAIL) | §3 システムアーキテクチャ、§4 保護モデル | 拡張 |
| カーネル (DESIGN_DETAIL) | §8 カーネルアーキテクチャ | 大幅拡張 |
| オーディオサブシステム (DESIGN_DETAIL) | §7 Processor API | 再設計 |
| MIDIサブシステム (DESIGN_DETAIL) | §6 I/Oモデル、§7.5 EventQueue | 統合 |
| IPC機構 (DESIGN_DETAIL) | §8.5 スレッド間通信 | 詳細化 |
| コルーチン (DESIGN_DETAIL) | 未言及 | **要確認** |
| モニタリング (DESIGN_DETAIL) | §8.4 System Monitor、ISP統計 | 再設計 |
| エラーハンドリング (DESIGN_DETAIL) | §14 エラー処理 | 体系化 |
| デバッグシェル (DESIGN_DETAIL) | §15 ISP | 統合 |
| ハードウェア抽象化 (DESIGN_DETAIL) | §10 PAL | 拡張 |
| Context設計 (ADAPTER) | §7 AudioContext | 統合・簡略化 |
| ホストアダプタ (ADAPTER) | §11 アダプタ層 | 継承 |
| UIアダプター (ADAPTER) | §18 UI/Controller分離 | 簡略化 |
| MOS-STM32比較 (COMPARISON) | 参照なし | 設計判断の継承 |
| - | §2 設計原則 | 新規 |
| - | §12 状態管理 | 新規 |
| - | §13 ケーパビリティ | 新規 |
| - | §17 数値表現 | 新規 |
| - | §19 プリセット/パターン | 新規 |

---

## 3. アーキテクチャ比較

### 3.1 全体構造

#### 既存設計

```
┌─────────────────────────────────────────────────────┐
│                  AudioProcessor                      │
├─────────────────────────────────────────────────────┤
│                 System Services                      │
│    Audio Task │ MIDI Server │ Monitor │ Shell       │
├─────────────────────────────────────────────────────┤
│                   Core Kernel                        │
│    Scheduler │ Timer │ IPC │ Watchdog               │
├─────────────────────────────────────────────────────┤
│               Hardware Abstraction                   │
│    DMA │ I2S │ GPIO │ UART │ USB                    │
└─────────────────────────────────────────────────────┘
```

**特徴**:
- 4層構造
- 組込み専用設計
- シンプルで理解しやすい

#### 新設計

```
┌─────────────────────────────────────────────────────────────┐
│                     Application                             │
│              (Processor実装、プラットフォーム非依存)           │
├─────────────────────────────────────────────────────────────┤
│                    Processor API                            │
├──────────────┬──────────────┬──────────────┬───────────────┤
│   Adapter    │   Adapter    │   Adapter    │    Adapter    │
│   Embedded   │   VST3/AU    │    CLAP      │  WASM/WebAudio│
├──────────────┼──────────────┼──────────────┼───────────────┤
│  UMI Kernel  │   DAW Host   │   DAW Host   │    Browser    │
├──────────────┴──────────────┴──────────────┴───────────────┤
│                Platform Abstraction Layer                   │
├─────────────────────────────────────────────────────────────┤
│  UMI-RTOS │ FreeRTOS │ Zephyr │ POSIX │ Browser Runtime    │
└─────────────────────────────────────────────────────────────┘
```

**特徴**:
- 6層構造
- マルチプラットフォーム対応
- アダプタ層による抽象化
- 複雑だが柔軟

### 3.2 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| 理解しやすさ | ◎ 優秀 | △ 複雑 |
| 柔軟性 | △ 限定的 | ◎ 優秀 |
| 実装工数 | ◎ 少ない | × 多い |
| スケーラビリティ | △ 限定的 | ◎ 優秀 |
| 製品適用性 | △ プロトタイプ向け | ◎ 製品向け |

---

## 4. カーネル設計比較

### 4.1 タスク構成

#### 既存設計

| タスク | 優先度 | 説明 |
|--------|--------|------|
| Audio Task | Realtime | オーディオ処理 |
| MIDI Server | High | MIDI受信処理 |
| User Task | Normal | ユーザータスク |
| Monitor | Low | 負荷監視 |

```cpp
// ユーザーがタスクを直接作成可能（モノリシック構成）
TaskId id = kernel.create_task(config);
```

#### 新設計

| タスク | 優先度 | 説明 |
|--------|--------|------|
| Audio Runner | 最高 | process()の周期実行 |
| Control Runner | 中 | control()の実行 |
| Driver Server | 中 | ドライバ要求処理 |
| System Monitor | 最低 | 統計収集・報告 |

```cpp
// タスクはカーネルが自動生成
// ユーザーはProcessorを実装するのみ
class Processor {
    virtual void process(AudioContext& ctx) = 0;
    virtual void control(ControlContext& ctx) {}
};
```

### 4.2 タスク管理の違い

| 項目 | 既存設計 | 新設計 |
|------|----------|--------|
| タスク生成 | ユーザー可能 | カーネル専用 |
| 優先度設定 | ユーザー可能 | 固定 |
| タスク数 | 動的 | 固定4タスク |
| 隠蔽度 | 低（タスクAPI露出） | 高（Processorのみ） |

### 4.3 UMI-RTOS（新設計で追加）

新設計では専用の軽量RTOSを定義:

```cpp
// UMI-RTOS仕様
constexpr uint32_t MAX_TASKS = 4;
constexpr uint32_t TICK_HZ = 1000;

// メモリフットプリント
// ROM: ~1KB
// RAM: ~4.2KB（スタック含む）
```

**FreeRTOSとの比較**:

| 項目 | UMI-RTOS | FreeRTOS |
|------|----------|----------|
| ROM | ~1KB | ~6-10KB |
| RAM（最小） | ~4KB | ~8KB |
| タスク数 | 固定4 | 動的 |
| コンテキストスイッチ | ~50 cycles | ~80-100 cycles |
| 機能 | 最小限 | 豊富 |

### 4.4 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| 柔軟性 | ◎ 高い | △ 低い |
| 安全性 | △ 低い | ◎ 高い |
| リソース効率 | ○ 良好 | ◎ 優秀 |
| 学習曲線 | △ RTOS知識必要 | ◎ 容易 |
| カスタマイズ性 | ◎ 高い | △ 限定的 |

---

## 5. オーディオ処理比較

### 5.1 バッファリング

#### 既存設計

```cpp
// ダブルバッファリング
template<typename HW, std::size_t BufferSize = 256>
class AudioEngine {
    alignas(4) std::array<Sample, BufferSize> buffer_a_;
    alignas(4) std::array<Sample, BufferSize> buffer_b_;
};
```

#### 新設計

```
┌──────────────────────────────────────────────────────────────┐
│ DMA Triple Buffering                                         │
│                                                              │
│   Buffer A ←── DMA書込中（ハードウェア）                      │
│   Buffer B ←── process()処理中                               │
│   Buffer C ←── DMA読出中（出力、前回の結果）                  │
│                                                              │
│ ※ process()が遅れてもDMAは止まらない                         │
└──────────────────────────────────────────────────────────────┘
```

### 5.2 ドロップ処理

#### 既存設計

- ドロップ検出のみ言及
- 具体的な処理戦略なし

#### 新設計

```
【最重要】タイミング同期 > 音質

ドロップ発生時:
  ✗ 遅れを取り戻そうとしない
  ✓ 遅れを受け入れ、現在位置に同期
```

```cpp
void handle_drop(uint64_t dma_pos) {
    uint32_t dropped = (dma_pos - current_sample_pos_) / buffer_size_;
    drop_count_ += dropped;
    current_sample_pos_ = dma_pos;  // 同期
    event_queue_.clear();
}
```

### 5.3 外部同期（新設計で追加）

```cpp
enum class SyncSource : uint8_t {
    Internal,       // 内部クロック
    WordClock,      // Word Clock入力
    MidiClock,      // MIDI Clock
    Spdif,          // S/PDIF
    Adat,           // ADAT
    Usb,            // USB SOF
};
```

### 5.4 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| ドロップ耐性 | △ ダブルバッファ | ◎ トリプルバッファ |
| 処理戦略 | × 未定義 | ◎ 明確 |
| 外部同期 | × なし | ◎ 複数対応 |
| 実装詳細度 | △ 概念的 | ◎ 実装レベル |

---

## 6. MIDI/イベント処理比較

### 6.1 イベント構造

#### 既存設計

```cpp
namespace midi {
    struct Event {
        Type type;
        std::uint8_t channel;
        std::uint8_t data1;
        std::uint8_t data2;
    };
}
```

#### 新設計

```cpp
struct Event {
    uint32_t port_id;
    uint32_t sample_pos;    // サンプル精度タイムスタンプ
    EventType type;
    
    union {
        MidiData midi;
        ParamData param;
        RawData raw;
    };
};

enum class EventType : uint8_t {
    Midi,
    Param,
    Raw,
};
```

### 6.2 ポート設計（新設計で追加）

```cpp
enum class PortKind { Continuous, Event };
enum class PortDirection { In, Out };

enum class TypeHint : uint16_t {
    MidiBytes   = 0x0100,
    MidiSysex   = 0x0102,
    ParamChange = 0x0200,
    Osc         = 0x0300,
    Clock       = 0x0400,
    // ...
};

struct PortDescriptor {
    uint32_t id;
    std::string_view name;
    PortKind kind;
    PortDirection dir;
    uint32_t channels;       // Continuous用
    TypeHint expected_type;  // Event用
};
```

### 6.3 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| タイムスタンプ | × なし | ◎ サンプル精度 |
| 汎用性 | △ MIDI専用 | ◎ 複数タイプ |
| ポート抽象化 | × なし | ◎ 完備 |
| 拡張性 | △ 限定的 | ◎ TypeHintで拡張可能 |

---

## 7. 時間管理比較

### 7.1 時間概念

#### 既存設計

- 明確な時間管理方針なし
- `usec monotonic_time_usecs()` のみ

#### 新設計

**2種類の時間を明確に分離**:

| 種類 | 型 | 用途 |
|------|-----|------|
| サンプル時間 | `uint64_t` | DSP、イベント、同期 |
| システム時間 | `std::chrono` | タイムアウト、ログ |

```
「このタイミングは音に影響するか？」
    │
    ├─ Yes → サンプル時間を使用
    │         ・ノートオン/オフのタイミング
    │         ・シーケンサーのステップ
    │         ・LFO同期
    │
    └─ No  → std::chrono を使用
              ・タイムアウト待機
              ・ログのタイムスタンプ
```

### 7.2 時間ユーティリティ（新設計で追加）

```cpp
namespace umi::time {
    constexpr uint64_t ms_to_samples(uint32_t ms, uint32_t sample_rate);
    constexpr uint32_t bpm_to_samples_per_beat(float bpm, uint32_t sample_rate);
    // ...
}
```

### 7.3 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| 概念の明確さ | × 曖昧 | ◎ 明確 |
| 使い分け指針 | × なし | ◎ フローチャート提供 |
| ユーティリティ | × なし | ◎ 完備 |
| process()での制約 | × 未定義 | ◎ 明確（chrono禁止） |

---

## 8. メモリ管理比較

### 8.1 ヒープ使用ルール

#### 既存設計

- 「実行時動的メモリは禁止」と言及
- 詳細な指針なし

#### 新設計

| フェーズ | ヒープ使用 | 備考 |
|----------|------------|------|
| initialize() | ✓ 許可 | ウェーブテーブル等 |
| prepare() | △ 最小限 | サンプルレート依存 |
| process() | ✗ 禁止 | リアルタイム要件 |
| control() | △ 最小限 | 可能なら避ける |

### 8.2 コンテナ使用方針（新設計で追加）

| コンテナ | 使用 | 条件 |
|----------|------|------|
| `std::array` | ✓ 推奨 | サイズ固定 |
| `std::span` | ✓ 推奨 | 非所有 |
| `std::vector` | △ 条件付 | 初期化時のみ |
| `std::map/set` | ✗ 非推奨 | オーバーヘッド大 |

### 8.3 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| 方針の明確さ | △ 基本のみ | ◎ 詳細 |
| フェーズ別ルール | × なし | ◎ 完備 |
| コンテナ指針 | × なし | ◎ 表形式 |
| 実例 | × なし | ◎ 豊富 |

---

## 9. 保護モデル比較

### 9.1 保護レベル

#### 既存設計

2パターン:
- マイクロカーネル構成（MPU有効）
- モノリシック構成（MPU無効）

#### 新設計

3レベル:

| レベル | ハードウェア | 保護方式 | 対象 |
|--------|--------------|----------|------|
| L3 | MMUあり | 完全メモリ分離 | Cortex-A |
| L2 | MPUあり | 軽量メモリ分離 | Cortex-M3/M4/M7 |
| L1 | 保護機構なし | 論理分離のみ | Cortex-M0, ESP32 |

### 9.2 ケーパビリティモデル（新設計で追加）

```cpp
struct Capability {
    uint32_t id;
    CapabilityType type;
    uint32_t instance;
    CapabilityFlags flags;
};

enum class CapabilityType : uint16_t {
    AudioIn, AudioOut,
    MidiIn, MidiOut,
    FlashRead, FlashWrite,
    // ...
};
```

### 9.3 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| 保護レベル | △ 2パターン | ◎ 3レベル |
| 権限管理 | × なし | ◎ ケーパビリティ |
| セキュリティ | × なし | ◎ 署名検証 |
| 柔軟性 | △ 限定的 | ◎ 高い |

---

## 10. プラットフォーム抽象化比較

### 10.1 対応プラットフォーム

#### 既存設計

- STM32F4
- スタブ（テスト用）

#### 新設計

| プラットフォーム | PAL実装 |
|------------------|---------|
| Cortex-M (STM32等) | UMI-RTOS |
| Cortex-M (STM32等) | FreeRTOS（互換用） |
| ESP-IDF | FreeRTOS |
| Zephyr | Zephyr native |
| Linux | POSIX threads |
| WASM | 単一スレッド |

### 10.2 PALインターフェース

#### 既存設計

```cpp
struct HW {
    static void enable_interrupts();
    static void disable_interrupts();
    static usec monotonic_time_usecs();
    static void mute_audio_dma();
    static void reboot();
};
```

#### 新設計

```cpp
struct PalOps {
    void (*task_create)(void (*entry)(void*), void* arg, int priority);
    void (*task_yield)(void);
    void (*task_sleep_us)(uint32_t us);
    void (*mutex_create)(pal_mutex_t* m);
    void (*mutex_lock)(pal_mutex_t* m);
    void (*mutex_unlock)(pal_mutex_t* m);
    uint64_t (*time_us)(void);
    void (*critical_enter)(void);
    void (*critical_exit)(void);
};
```

### 10.3 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| 対応範囲 | △ STM32のみ | ◎ 広範囲 |
| 抽象化度 | △ 基本的 | ◎ 完全 |
| タスク管理 | × なし | ◎ 完備 |
| 同期プリミティブ | × なし | ◎ 完備 |

---

## 11. API設計比較

### 11.1 Processor API

#### 既存設計

```cpp
struct MySynth : umi::AudioProcessor<MySynth> {
    void process(float** out, const float** in,
                 std::size_t frames, std::size_t channels);
    void on_midi(const umi::midi::Event& ev);
};
```

#### 新設計

```cpp
class Processor {
public:
    virtual void initialize() {}
    virtual void prepare(const StreamConfig& config) {}
    virtual void process(AudioContext& ctx) = 0;
    virtual void control(ControlContext& ctx) {}
    virtual void release() {}
    virtual void terminate() {}
    
    virtual std::span<const ParamDescriptor> params() { return {}; }
    virtual std::span<const PortDescriptor> ports() { return {}; }
    virtual std::vector<uint8_t> save_state() { return {}; }
    virtual void load_state(std::span<const uint8_t>) {}
};
```

### 11.2 AudioContext

#### 既存設計

- 引数として直接渡される

#### 新設計

```cpp
struct AudioContext {
    std::span<const sample_t* const> inputs;
    std::span<sample_t* const> outputs;
    EventQueue& events;
    uint32_t sample_rate;
    uint32_t buffer_size;
    uint64_t sample_position;
};
```

### 11.3 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| ライフサイクル | △ 2メソッド | ◎ 6メソッド |
| メタデータ | △ 基本的 | ◎ 完全 |
| 状態管理 | × なし | ◎ save/load |
| コンテキスト | △ 引数直接 | ◎ 構造体 |

---

## 12. アダプタ層比較

### 12.1 設計原則

#### 既存設計（ADAPTER.md）

**核心原則**: 「process()が必要とする全ての外部情報は引数（Context）経由で渡す」

```cpp
// ✅ 良い例: 全て引数経由
void process(umi::Context& ctx) {
    float freq = ctx.params[0];
    while (auto ev = ctx.midi_in.pop_at(i))
    ctx.audio_out[0][i] = generate();
}

// ❌ 悪い例: グローバル/シングルトン
void process(umi::Context& ctx) {
    float sr = GlobalConfig::sample_rate;        // NG
    auto& midi = MidiServer::instance().queue(); // NG
}
```

| 許可 | 禁止 |
|------|------|
| 引数 (Context) | グローバル変数 |
| メンバ変数（自身の状態） | シングルトン |
| constexpr 定数 | static mutable |
| | ハードウェア直接アクセス |

#### 新設計

同様の原則を継承しつつ、`AudioContext`として構造を整理。

### 12.2 Context構造の比較

#### 既存設計

```cpp
struct Context {
    AudioBuffer<float> audio_out;
    AudioBuffer<const float> audio_in;
    float sample_rate;
    MidiIn midi_in;
    MidiOut midi_out;
    std::span<const float> params;
    Display* display{nullptr};
    const Transport* transport{nullptr};
};
```

#### 新設計

```cpp
struct AudioContext {
    std::span<const sample_t* const> inputs;
    std::span<sample_t* const> outputs;
    EventQueue& events;
    uint32_t sample_rate;
    uint32_t buffer_size;
    uint64_t sample_position;
};
```

### 12.3 サポートホストの比較

| ホスト | 既存設計 | 新設計 |
|--------|----------|--------|
| MCU (UMI-OS) | ✅ 詳細実装 | ✅ 詳細実装 |
| VST3 | ✅ 詳細実装 | ✅ 詳細実装 |
| AU | ✅ 詳細実装 | ○ 概念のみ |
| CLAP | ✅ 詳細実装 | ✅ 詳細実装 |
| WASM | ✅ 詳細実装（AudioWorklet含む） | ✅ 詳細実装 |
| Standalone | - | ✅ 追加 |

### 12.4 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| 設計原則の明確さ | ◎ 非常に明確 | ○ 継承 |
| 実装詳細度 | ◎ JS含む完全例 | ○ C++中心 |
| Context統一性 | ○ 型安全 | ◎ より統一的 |
| サンプル位置 | △ Transportに依存 | ◎ sample_position標準 |

**結論**: 既存のADAPTER.mdの設計原則と実装詳細は非常に優れており、新設計に引き継ぐべき。

---

## 13. エラー処理比較

### 12.1 エラーコード

#### 既存設計

```cpp
enum class Error {
    OutOfMemory, OutOfTasks, Timeout, WouldBlock,
    BufferOverrun, BufferUnderrun,
    MidiParseError, MidiBufferFull,
};
```

#### 新設計

```cpp
enum class Error : int32_t {
    Ok = 0,
    
    // 一般エラー (0x0001-0x00FF)
    Unknown = 0x0001,
    NotImplemented = 0x0002,
    // ...
    
    // ケーパビリティエラー (0x0100-0x01FF)
    CapabilityNotHeld = 0x0100,
    // ...
    
    // I/Oエラー (0x0200-0x02FF)
    IoError = 0x0200,
    // ...
    
    // オーディオエラー (0x0300-0x03FF)
    AudioUnderrun = 0x0300,
    // ...
    
    // ISPエラー (0x0500-0x05FF)
    // セキュリティエラー (0x0600-0x06FF)
};
```

### 12.2 Result型

#### 既存設計

```cpp
Result<TaskId> result = kernel.try_create_task(config);
if (!result) {
    Error err = result.error();
}
```

#### 新設計

```cpp
template<typename T>
class Result {
public:
    bool ok() const;
    T& value();
    Error error() const;
    
    template<typename F> auto map(F&& f);      // モナディック
    template<typename F> auto and_then(F&& f); // モナディック
};
```

### 12.3 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| エラーコード数 | △ 8個 | ◎ 体系的に分類 |
| エラー分類 | × なし | ◎ カテゴリ別 |
| Result型 | ○ 基本的 | ◎ モナディック |
| 処理方針 | × なし | ◎ フェーズ別 |

---

## 14. ログ・デバッグ比較

### 14.1 ログシステム

#### 既存設計（DESIGN_DETAIL + COMPARISON_MOS_STM32）

- デバッグシェル（`umi_shell.hh`）実装済み
- UART/SysEx/USB CDC出力対応
- 基本コマンド: `ps`, `mem`, `load`, `reboot`, `help`

```cpp
// シェル出力関数（差し替え可能）
using ShellWriteFn = void (*)(const char* str);
umi::Shell<HW, Kernel> shell(kernel, sysex_write);
```

#### 新設計

**層ごとの使用方針**:

| 層 | assert | log | 依存 |
|----|--------|-----|------|
| DSP部品 | ✗ なし | ✗ なし | C++標準のみ |
| Processor | ✓ バッファ単位 | ✓ 適宜 | UMI-OS |
| Kernel | ✓ 必要に応じて | ✓ 必要に応じて | UMI-OS + PAL |

**ログAPI**:

```cpp
namespace umi::log {
    template<typename... Args> void trace(...);
    template<typename... Args> void debug(...);
    template<typename... Args> void info(...);
    template<typename... Args> void warn(...);
    template<typename... Args> void error(...);
}
```

### 14.2 リソースモニタリング

#### 既存設計（umi_monitor.hh）

```cpp
// 実装済み
StackMonitor     // スタック使用量監視
HeapMonitor      // ヒープ使用量追跡
TaskProfiler     // タスク実行時間計測
IrqLatencyMonitor // 割り込みレイテンシ監視
LoadMonitor      // DSP負荷監視
```

#### 新設計

ISP統計情報としてSysEx経由で取得:

```cpp
struct StatsPayload {
    uint8_t cpu_usage;
    uint8_t audio_load;
    uint16_t max_process_us;
    uint32_t drop_count;
    uint16_t heap_used_kb;
    // ...
};
```

### 14.3 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| シェル実装 | ◎ 完成 | ○ ISPに統合 |
| モニタリング | ◎ 複数ツール | ◎ 統計レポート |
| ログレベル | △ 暗黙的 | ◎ 5段階 |
| 層別方針 | × なし | ◎ 明確 |
| RTT対応 | × なし | ◎ 追加 |

**結論**: 既存のモニタリング実装は維持しつつ、新設計の層別ログ方針を採用。

---

## 15. UI/ハードウェア抽象化比較

### 15.1 UIアーキテクチャ

#### 既存設計（ADAPTER.md）

**3層サーバー構成**:

```
┌─────────────────────────────────────────────────────────────────┐
│  AudioProcessor（ユーザー）                                      │
│  ctx.params[i] / ctx.display.line(...)                          │
├─────────────────────────────────────────────────────────────────┤
│                    共有メモリ                                    │
│  params[]  │  encoder_states[]  │  display_buffer[]             │
├─────────────────────────────────────────────────────────────────┤
│  Input Server (Normal)      │  Display Server (Low)             │
│  - ノイズフィルタ            │  - dirty確認                      │
│  - デバウンス               │  - LCD更新                        │
│  - バインディング適用        │  - LED PWM                        │
├─────────────────────────────────────────────────────────────────┤
│  Hardware Drivers                                                │
└─────────────────────────────────────────────────────────────────┘
```

**4段階のバインディングレベル**:
1. Level 1: 自動マッピング
2. Level 2: カスタムマッピング
3. Level 3: ページ切り替え
4. Level 4: フルカスタム（ページ+レイヤー+メニュー混在）

#### 新設計

**シンプルなController分離**:

```cpp
class Controller {
    virtual void on_param_changed(uint32_t id, float value) {}
    virtual void on_meter_update(std::span<const float> levels) {}
    void set_param(uint32_t id, float value);
};
```

### 15.2 UIモードの比較

| モード | 既存設計 | 新設計 |
|--------|----------|--------|
| Headless (MIDI) | ✅ 対応 | ✅ 対応 |
| HardwareUI | ✅ 詳細設計 | ○ 概念のみ |
| GUI (VST/AU) | ✅ GUIAdapter | ○ 概念のみ |

### 15.3 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| 詳細度 | ◎ 非常に詳細 | △ 概念的 |
| バインディング | ◎ 4段階 | × なし |
| Input Server | ◎ 実装詳細 | × なし |
| Display Server | ◎ 実装詳細 | × なし |
| 共有メモリ設計 | ◎ atomic対応 | △ 基本のみ |

**結論**: 既存のUIサーバー設計は非常に成熟しており、新設計は簡略化されすぎ。既存設計を維持・参照すべき。

---

## 16. コルーチン比較

### 16.1 実装状況

#### 既存設計（umi_coro.hh + COMPARISON_MOS_STM32）

```cpp
namespace umi::coro {
    template <typename T = void>
    struct Task {
        struct promise_type {
            Task get_return_object();
            std::suspend_always initial_suspend() noexcept;
            std::suspend_always final_suspend() noexcept;
            void return_value(T value);
            void unhandled_exception();
        };
    };
    
    struct Scheduler {
        void schedule(std::coroutine_handle<> h);
        void run();
    };
    
    // SleepAwaiter（非ブロッキングスリープ）
    template<std::size_t MaxCoros>
    class SleepAwaiter;
}

// 使用例
using namespace umi::coro::literals;
co_await ctx.sleep(100ms);
```

**MOS-STM32との比較結果**:
> 「UMI-OS のコルーチン実装は MOS-STM32 より成熟している。」

#### 新設計

コルーチンへの言及なし。

### 16.2 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| Task<T>実装 | ◎ 完全 | × 未言及 |
| Task<void>特殊化 | ◎ 対応 | × 未言及 |
| Scheduler | ◎ 実装済 | × 未言及 |
| SleepAwaiter | ◎ 実装済 | × 未言及 |
| chrono literals | ◎ 対応 | × 未言及 |

**結論**: 既存のコルーチン実装は維持必須。新設計に明記すべき。

---

## 17. ドキュメント品質比較

### 17.1 網羅性

| トピック | 既存設計 | 新設計 |
|----------|----------|--------|
| アーキテクチャ概要 | ◎ | ◎ |
| API仕様 | ○ | ◎ |
| 実装例 | ○ | ◎ |
| エラー処理 | △ | ◎ |
| 時間管理 | × | ◎ |
| メモリ管理 | △ | ◎ |
| 保護モデル | △ | ◎ |
| プラットフォーム対応 | ◎ 詳細 | ◎ 詳細 |
| ビルド設定 | ○ | ◎ |
| バージョニング | × | ◎ |
| サンプルコード | ○ | ◎ |
| 改訂履歴 | × | ◎ |
| UIアーキテクチャ | ◎ ADAPTER詳細 | △ 概念的 |
| コルーチン | ◎ 実装済 | × 未言及 |

### 17.2 実装可能性

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| 仕様の曖昧さ | △ 多い | ○ 少ない |
| コード例の品質 | ○ 実装済み | ◎ 詳細 |
| 型定義の完全性 | ○ 部分的 | ◎ 完全 |
| API署名の明確さ | ○ 部分的 | ◎ 明確 |
| UI詳細設計 | ◎ 実装レベル | △ 概念のみ |

### 17.3 ドキュメント構成比較

| ドキュメント | 行数 | 主な内容 | 品質 |
|--------------|------|----------|------|
| NEW_DESIGN.md | 5,695 | 包括的仕様 | ◎ 高品質 |
| DESIGN_DETAIL.md | 426 | カーネル詳細 | ○ 実用的 |
| README.md | 182 | コンセプト | ○ 良い導入 |
| ADAPTER.md | 1,447 | アダプタ層詳細 | ◎ 非常に詳細 |
| COMPARISON_MOS_STM32.md | 860 | RTOS比較・決定 | ◎ 意思決定記録 |

### 17.4 評価

| 観点 | 既存設計 | 新設計 |
|------|----------|--------|
| 分量 | ◎ 約3,000行 | ◎ 約5,700行 |
| 詳細度 | ○ 混在 | ◎ 実装レベル |
| 実装可能性 | ○ 補足必要 | ○ 大部分可能 |
| メンテナンス性 | ◎ 容易 | △ 工数必要 |
| UI詳細 | ◎ 詳細 | △ 不十分 |
| 意思決定記録 | ◎ あり | △ 少ない |

---

## 18. 新機能一覧

新設計で追加された主要機能:

### 18.1 コア機能

| 機能 | 説明 | 重要度 |
|------|------|--------|
| UMI-RTOS | 専用軽量RTOS | 中 |
| トリプルバッファリング | 遅延耐性向上 | 高 |
| 外部同期 | WordClock/MIDI Clock等 | 中 |
| ケーパビリティ | 権限管理 | 中 |
| ISP | SysEx経由の診断・更新 | 中 |
| 保護レベル（L1/L2/L3） | 明確な制約定義 | 高 |

※アダプタ層は既存設計(ADAPTER.md)で詳細に設計済み

### 18.2 API機能

| 機能 | 説明 | 重要度 |
|------|------|--------|
| Processorライフサイクル | 6段階メソッド | 高 |
| AudioContext | 統合コンテキスト | 高 |
| EventQueue | サンプル精度イベント | 高 |
| ParamDescriptor | パラメータ宣言 | 高 |
| PortDescriptor | ポート宣言 | 高 |
| save/load_state | 状態管理 | 中 |

### 18.3 設計ガイドライン

| 機能 | 説明 | 重要度 |
|------|------|--------|
| 時間管理方針 | サンプル時間/システム時間 | 高 |
| メモリ管理方針 | フェーズ別ルール | 高 |
| コンテナ使用方針 | 推奨/非推奨表 | 中 |
| ログ・アサート方針 | 層別使用ルール | 中 |
| DSP設計指針 | 仮想関数使用基準 | 中 |

### 18.4 追加機能

| 機能 | 説明 | 重要度 |
|------|------|--------|
| プリセット/バンク | 状態管理 | 中 |
| パターンフォーマット | シーケンスデータ | 低 |
| 数値表現 | float/fixed切替 | 中 |
| I/Oマッピング | 仮想/物理I/O | 中 |

---

## 19. 既存設計の維持すべき要素

### 19.1 ADAPTER.mdから（重要）

| 要素 | 重要度 | 理由 |
|------|--------|------|
| Context経由データ渡し原則 | **必須** | 設計の核心的原則 |
| Input Server / Display Server分離 | 高 | UI責務の明確な分離 |
| 4段階バインディングレベル | 高 | 柔軟なUI対応 |
| SharedState設計 | 高 | スレッド安全なUI通信 |
| ホスト別実装例（VST3/AU/CLAP/WASM） | 中 | 実装ガイドとして |

### 19.2 COMPARISON_MOS_STM32.mdから（重要）

| 要素 | 重要度 | 理由 |
|------|--------|------|
| SPSCで十分（MPSCは過剰） | **必須** | 検討済み設計決定 |
| コルーチン優先（delay()より） | **必須** | 成熟した実装 |
| 最小限の同期プリミティブ | 高 | シンプルさ維持 |
| Result<T,E>採用 | 高 | 既に導入済み |
| タスク名・ラウンドロビン | 中 | デバッグ支援 |

### 19.3 umi_coro.hhから

| 要素 | 重要度 | 理由 |
|------|--------|------|
| Task<T>実装 | **必須** | 動作実績あり |
| Scheduler | **必須** | コルーチン基盤 |
| SleepAwaiter | 高 | 非ブロッキングスリープ |
| chrono literals対応 | 中 | 使いやすさ |

---

## 20. 実装上の課題

### 20.1 工数見積もり

| コンポーネント | 既存実装 | 追加工数 |
|----------------|----------|----------|
| UMI-RTOS | なし | 大 |
| PAL（追加プラットフォーム） | STM32のみ | 大 |
| アダプタ層 | **ADAPTER.md設計済** | 中（実装のみ） |
| ISP | なし | 中 |
| ケーパビリティ | なし | 中 |
| 外部同期 | なし | 中 |

### 20.2 技術的課題

| 課題 | 影響度 | 対策案 |
|------|--------|--------|
| 仕様と実装の乖離 | 高 | 段階的実装、テスト駆動 |
| プラットフォーム差異 | 中 | PAL抽象化の徹底 |
| 性能オーバーヘッド | 中 | プロファイリング、最適化 |
| ドキュメントメンテナンス | 中 | 自動生成、CI連携 |
| 新設計のUI設計不足 | 高 | ADAPTER.md設計を維持 |

### 20.3 優先度マトリクス

| 機能 | 重要度 | 実装難易度 | 優先度 |
|------|--------|------------|--------|
| Processor API改訂 | 高 | 低 | **P0** |
| AudioContext/EventQueue | 高 | 低 | **P0** |
| 時間管理方針 | 高 | 低 | **P0** |
| 既存コルーチン維持 | 高 | 低 | **P0** |
| トリプルバッファリング | 高 | 中 | **P1** |
| UMI-RTOS | 中 | 高 | P2 |
| アダプタ層実装 | 高 | 中 | P2 |
| ISP | 中 | 中 | P2 |
| ケーパビリティ | 中 | 中 | P3 |
| 外部同期 | 中 | 中 | P3 |

---

## 21. 推奨事項

### 21.1 短期（0-3ヶ月）

1. **P0機能の実装**
   - Processor API改訂（既存コードの更新）
   - AudioContext/EventQueue実装
   - 時間管理ユーティリティ

2. **既存設計の維持**
   - `umi_coro.hh`のコルーチン実装を維持
   - ADAPTER.mdのUI設計を正式仕様として維持

3. **ドキュメント整備**
   - 新旧API移行ガイド
   - 最小実装サンプル
   - **新設計にコルーチン・UI設計への参照を追記**

### 21.2 中期（3-6ヶ月）

1. **P1機能の実装**
   - トリプルバッファリング
   - ドロップ処理ロジック
   - ログ・アサートシステム

2. **P2機能の一部**
   - 1つ目のアダプタ（VST3推奨、ADAPTER.md設計使用）
   - ISPの基本機能

### 21.3 長期（6-12ヶ月）

1. **P2/P3機能の完成**
   - UMI-RTOS完成
   - 残りのアダプタ（AU/CLAP/WASM）
   - ケーパビリティ
   - 外部同期

2. **製品化準備**
   - 性能検証
   - セキュリティ監査
   - ドキュメント統合

### 21.4 継続的アクション

| アクション | 頻度 |
|------------|------|
| 仕様レビュー | 月次 |
| 実装との整合確認 | PR毎 |
| テストカバレッジ確認 | 週次 |
| ドキュメント更新 | 機能追加毎 |

---

## 22. 結論

### 22.1 総合評価

| 観点 | 新設計 | 既存設計 |
|------|--------|----------|
| Processor API | ◎ 大幅改善 | △ 基本的 |
| 時間管理 | ◎ 詳細 | × なし |
| メモリ管理 | ◎ フェーズ別 | △ 基本的 |
| 保護レベル | ◎ 3段階 | △ 暗黙的 |
| UIアーキテクチャ | △ 概念的 | ◎ 詳細（ADAPTER.md） |
| コルーチン | × 未言及 | ◎ 成熟（umi_coro.hh） |
| 設計決定記録 | △ 少ない | ◎ あり（COMPARISON） |

### 22.2 新設計の強み

- **包括的で体系的な仕様** - 5,700行の詳細ドキュメント
- **マルチプラットフォーム対応** - MCU/VST3/AU/CLAP/WASM
- **実装レベルの詳細度** - コード例が豊富
- **電子楽器OS設計として合理的** - 時間管理、保護レベル等

### 22.3 既存設計の強み（維持すべき）

- **Context経由データ渡し原則** - ADAPTER.mdで確立された核心的原則
- **Input/Display Server分離** - 成熟したUIアーキテクチャ
- **コルーチン実装** - MOS-STM32より成熟と評価
- **SPSC十分の判断** - 検討済み設計決定
- **最小限の同期プリミティブ** - シンプルさ重視

### 22.4 新設計の問題点

| 問題 | 重要度 | 推奨対応 |
|------|--------|----------|
| コルーチン未言及 | 高 | umi_coro.hhへの参照追加 |
| UI設計が浅い | 高 | ADAPTER.md設計を正式採用 |
| 設計決定理由不足 | 中 | COMPARISON形式で追記 |

### 22.5 採用推奨度

| 用途 | 推奨度 | 理由 |
|------|--------|------|
| 製品開発 | ◎ 強く推奨 | 長期的な拡張性・保守性 |
| プロトタイプ | △ 条件付き | 一部機能のみ採用 |
| 学習・研究 | ○ 推奨 | 設計の参考として |
| 既存プロジェクト | △ 段階的 | 移行計画が必要 |

### 22.6 最終評価

新設計は**既存設計の正統な進化版**であり、電子楽器向けOSとして必要な要素を網羅しています。ただし、**既存設計で成熟している部分（UI設計、コルーチン、設計決定）は維持・参照**すべきです。

**推奨アプローチ**:

1. 新設計を**目標仕様**として位置づける
2. **ADAPTER.md、COMPARISON_MOS_STM32.md、umi_coro.hh**を正式仕様として維持
3. 新設計に上記への参照を追記し、**ドキュメント間の整合性**を確保
4. **優先度に従って段階的に実装**
5. **テスト駆動**で品質を担保

**最終判定**: 採用推奨（既存設計との統合が条件）

---

*本レポート作成日: 2025年1月*
*対象ドキュメント: NEW_DESIGN.md (v0.1.11), DESIGN_DETAIL.md, README.md, ADAPTER.md, COMPARISON_MOS_STM32.md*
