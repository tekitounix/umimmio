# 統合 I/O ポートシステム（第4版）

## 概要

連続状態バッファを **カテゴリーベース** で統一的に扱うシステム。
イベント処理は既存の EventRouter をそのまま維持する。

```
┌─────────────────────────────────────────────────────────────────┐
│ 入力                                                            │
│  イベント:   input_events (既存 EventRouter)                     │
│  連続状態:   IoPort (本提案) — IN: AUDIO, KNOB, BUTTON, CV_IN   │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ 出力                                                            │
│  イベント:   output_events (既存) — MIDI OUT                    │
│  連続状態:   IoPort (本提案) — OUT: AUDIO, LED, RGB_LED, DISPLAY, CV_OUT │
└─────────────────────────────────────────────────────────────────┘
```

## 設計原則

1. **OS/App 完全分離** — 互いを知らない、共有メモリは構造なし
2. **カテゴリーベース** — 意味論的カテゴリーでマッチング
3. **標準形式** — App は常に正規化された値でアクセス
4. **リマップ可能** — プロファイルでカスタマイズ可能
5. **イベントは既存維持** — EventRouter, RouteTable はそのまま

---

## IoInputCategory / IoOutputCategory — 意味論的カテゴリー

```cpp
namespace umi {

enum class IoInputCategory : uint8_t {
    AUDIO,          // オーディオ入力バッファ
    KNOB,           // アナログノブ/フェーダー
    BUTTON,         // デジタルボタン
    ENCODER,        // ロータリーエンコーダ
    CV_IN,          // CV 入力
    GENERIC,        // 汎用入力（スケーリングなし）
};

enum class IoOutputCategory : uint8_t {
    AUDIO,          // オーディオ出力バッファ
    LED,            // 単色 LED
    RGB_LED,        // RGB LED
    DISPLAY,        // ディスプレイ
    CV_OUT,         // CV 出力
    GENERIC,        // 汎用出力（スケーリングなし）
};

} // namespace umi
```

## カテゴリー別標準形式

App は常にこの形式でアクセス。OS が HW ↔ 標準形式の変換を行う。

### 入力

| Category | 標準形式 | stride | 値域 | HW 変換例 |
|----------|---------|--------|------|----------|
| AUDIO | `float` | 4 | -1.0 ~ +1.0 | I2S 24bit → float |
| KNOB | `float` | 4 | 0.0 ~ 1.0 | ADC 12bit (0-4095) → float |
| BUTTON | `uint8_t` | 1 | 0 / 1 | GPIO → uint8 |
| ENCODER | `int8_t` | 1 | -128 ~ +127 (相対) | クワドラチャ → int8 |
| CV_IN | `float` | 4 | -1.0 ~ +1.0 | ADC 16bit → float |
| GENERIC | `uint8_t[]` | 可変 | — | 変換なし |

### 出力

| Category | 標準形式 | stride | 値域 | HW 変換例 |
|----------|---------|--------|------|----------|
| AUDIO | `float` | 4 | -1.0 ~ +1.0 | float → I2S |
| LED | `float` | 4 | 0.0 ~ 1.0 | float → PWM 8bit |
| RGB_LED | `float[3]` | 12 | RGB 各 0.0~1.0 | float×3 → WS2812B |
| DISPLAY | `uint8_t[]` | 可変 | ピクセルバッファ | そのまま |
| CV_OUT | `float` | 4 | -1.0 ~ +1.0 | float → DAC 16bit |
| GENERIC | `uint8_t[]` | 可変 | — | 変換なし |

---

## IoInputPortDesc / IoOutputPortDesc — ポート記述子

OS 側と App 側で共通の型。

```cpp
namespace umi {

struct IoInputPortDesc {
    IoInputCategory category;  // 意味論的カテゴリー（入力）
    uint16_t count;            // 要素数
    
    [[nodiscard]] constexpr uint16_t stride() const noexcept {
        switch (category) {
        case IoInputCategory::AUDIO:
        case IoInputCategory::KNOB:
        case IoInputCategory::CV_IN:
            return 4;  // float
        case IoInputCategory::BUTTON:
        case IoInputCategory::ENCODER:
            return 1;  // uint8_t / int8_t
        default:
            return 1;  // GENERIC
        }
    }
    
    [[nodiscard]] constexpr size_t size() const noexcept {
        return static_cast<size_t>(stride()) * count;
    }
};

struct IoOutputPortDesc {
    IoOutputCategory category;  // 意味論的カテゴリー（出力）
    uint16_t count;             // 要素数
    
    [[nodiscard]] constexpr uint16_t stride() const noexcept {
        switch (category) {
        case IoOutputCategory::AUDIO:
        case IoOutputCategory::LED:
        case IoOutputCategory::CV_OUT:
            return 4;  // float
        case IoOutputCategory::RGB_LED:
            return 12; // float[3]
        default:
            return 1;  // GENERIC, DISPLAY
        }
    }
    
    [[nodiscard]] constexpr size_t size() const noexcept {
        return static_cast<size_t>(stride()) * count;
    }
};

} // namespace umi
```

---

## OS 側: Capability 定義

ボードが提供する I/O を一箇所で宣言。

```cpp
// board/daisy_pod/capability.hh
namespace umi::board::daisy_pod {

// ハードウェアが提供する入力ポート
inline constexpr IoInputPortDesc hw_inputs[] = {
    {IoInputCategory::AUDIO,   2},    // ステレオ入力
    {IoInputCategory::KNOB,    2},    // ノブ 2個
    {IoInputCategory::BUTTON,  2},    // ボタン 2個
    {IoInputCategory::ENCODER, 1},    // エンコーダ 1個
};

// ハードウェアが提供する出力ポート
inline constexpr IoOutputPortDesc hw_outputs[] = {
    {IoOutputCategory::AUDIO,   2},    // ステレオ出力
    {IoOutputCategory::RGB_LED, 2},    // RGB LED 2個
};

// 共有メモリサイズ（構造体定義なし、サイズのみ）
inline constexpr size_t SHARED_MEMORY_SIZE = 16 * 1024;

} // namespace umi::board::daisy_pod
```

---

## App 側: Requirement 定義

アプリが必要とする I/O を一箇所で宣言。`.umia` バイナリに埋め込まれる。

```cpp
// app/synth/io_requirements.hh
namespace app {

// アプリが要求する入力ポート
inline constexpr umi::IoInputPortDesc required_inputs[] = {
    {umi::IoInputCategory::KNOB,    8},    // ノブ 8個欲しい
    {umi::IoInputCategory::BUTTON,  4},    // ボタン 4個欲しい
    {umi::IoInputCategory::ENCODER, 2},    // エンコーダ 2個欲しい
};

// アプリが要求する出力ポート
inline constexpr umi::IoOutputPortDesc required_outputs[] = {
    {umi::IoOutputCategory::AUDIO,   2},    // ステレオ出力
    {umi::IoOutputCategory::LED,     8},    // LED 8個欲しい
    {umi::IoOutputCategory::RGB_LED, 4},    // RGB LED 4個欲しい
};

} // namespace app
```

---

## 共有メモリ: 構造なし

```cpp
// 共有メモリは構造体を持たない
// サイズのみ定義、レイアウトはランタイムで決定
alignas(4) std::byte shared_memory[SHARED_MEMORY_SIZE];
```

---

## ランタイム結合: PortBinding

OS が App ロード時にマッチングを実行。

```cpp
namespace umi::kernel {

struct PortBinding {
    uint8_t app_index;          // App の requirements[] インデックス
    uint8_t hw_index;           // HW の hw_ports[] インデックス (0xFF = なし)
    uint32_t offset;            // 共有メモリ内オフセット
    uint16_t active_count;      // 実際に使える要素数
};

/// 入力カテゴリーマッチングでバインディングを計算
void bind_input_ports(
    std::span<const IoInputPortDesc> requirements,
    std::span<const IoInputPortDesc> hw_ports,
    std::span<PortBinding> out_bindings
) {
    uint32_t offset = 0;
    
    for (size_t i = 0; i < requirements.size(); ++i) {
        auto& req = requirements[i];
        auto& bind = out_bindings[i];
        bind.app_index = i;
        bind.hw_index = 0xFF;
        bind.active_count = 0;
        
        // 同じ入力カテゴリーの HW ポートを探す
        for (size_t j = 0; j < hw_ports.size(); ++j) {
            if (hw_ports[j].category == req.category) {
                bind.hw_index = j;
                bind.active_count = std::min(req.count, hw_ports[j].count);
                break;
            }
        }
        
        // オフセット計算（要求サイズ分確保）
        bind.offset = offset;
        offset = (offset + req.size() + 3) & ~3;  // 4バイトアライン
    }
}

/// 出力カテゴリーマッチングでバインディングを計算
void bind_output_ports(
    std::span<const IoOutputPortDesc> requirements,
    std::span<const IoOutputPortDesc> hw_ports,
    std::span<PortBinding> out_bindings
) {
    uint32_t offset = 0;
    
    for (size_t i = 0; i < requirements.size(); ++i) {
        auto& req = requirements[i];
        auto& bind = out_bindings[i];
        bind.app_index = i;
        bind.hw_index = 0xFF;
        bind.active_count = 0;
        
        // 同じ出力カテゴリーの HW ポートを探す
        for (size_t j = 0; j < hw_ports.size(); ++j) {
            if (hw_ports[j].category == req.category) {
                bind.hw_index = j;
                bind.active_count = std::min(req.count, hw_ports[j].count);
                break;
            }
        }
        
        // オフセット計算（要求サイズ分確保）
        bind.offset = offset;
        offset = (offset + req.size() + 3) & ~3;  // 4バイトアライン
    }
}

} // namespace umi::kernel
```

---

## IoConverter — HW ↔ 標準形式変換

```cpp
namespace umi::kernel {

class IoConverter {
public:
    /// HW raw → 標準形式
    void convert_input(IoInputCategory cat,
                       std::span<const std::byte> hw_raw,
                       std::span<std::byte> app_buffer,
                       uint16_t count) {
        switch (cat) {
        case IoInputCategory::KNOB:
            // ADC 12bit (0-4095) → float (0.0-1.0)
            convert_adc12_to_float(hw_raw, app_buffer, count);
            break;
        case IoInputCategory::BUTTON:
            // GPIO bitmask → uint8_t array (0/1)
            convert_gpio_to_buttons(hw_raw, app_buffer, count);
            break;
        case IoInputCategory::ENCODER:
            // Quadrature delta → int8_t
            convert_quad_to_int8(hw_raw, app_buffer, count);
            break;
        case IoInputCategory::AUDIO:
        case IoInputCategory::CV_IN:
            // I2S/ADC → float (-1.0 ~ +1.0)
            // 通常は DMA が直接バッファに書くので変換不要
            break;
        default:
            break;
        }
    }
    
    /// 標準形式 → HW raw
    void convert_output(IoOutputCategory cat,
                        std::span<const std::byte> app_buffer,
                        std::span<std::byte> hw_raw,
                        uint16_t count) {
        switch (cat) {
        case IoOutputCategory::LED:
            // float (0.0-1.0) → PWM 8bit (0-255)
            convert_float_to_pwm8(app_buffer, hw_raw, count);
            break;
        case IoOutputCategory::RGB_LED:
            // float[3] (0.0-1.0 each) → WS2812B (GRB 8bit each)
            convert_float3_to_ws2812(app_buffer, hw_raw, count);
            break;
        case IoOutputCategory::AUDIO:
        case IoOutputCategory::CV_OUT:
            // float → I2S/DAC
            // 通常は DMA が直接バッファから読むので変換不要
            break;
        default:
            break;
        }
    }
    
private:
    void convert_adc12_to_float(std::span<const std::byte> src,
                                 std::span<std::byte> dst,
                                 uint16_t count) {
        auto* raw = reinterpret_cast<const uint16_t*>(src.data());
        auto* out = reinterpret_cast<float*>(dst.data());
        for (uint16_t i = 0; i < count; ++i) {
            out[i] = static_cast<float>(raw[i]) / 4095.0f;
        }
    }
    
    void convert_float_to_pwm8(std::span<const std::byte> src,
                                std::span<std::byte> dst,
                                uint16_t count) {
        auto* in = reinterpret_cast<const float*>(src.data());
        auto* out = reinterpret_cast<uint8_t*>(dst.data());
        for (uint16_t i = 0; i < count; ++i) {
            out[i] = static_cast<uint8_t>(std::clamp(in[i], 0.0f, 1.0f) * 255.0f);
        }
    }
    
    // 他の変換関数...
};

} // namespace umi::kernel
```

---

## リマッププロファイル（オプション）

ユーザーがカスタマイズ可能なマッピング。

```cpp
namespace umi {

struct IoMapping {
    uint8_t app_port;           // App が要求したポートインデックス
    uint8_t hw_port;            // 実際にバインドする HW ポート
    float scale;                // スケール係数 (1.0 = そのまま)
    float offset;               // オフセット (0.0 = そのまま)
};

// プロファイル例: "MIDIコントローラA用"
inline constexpr IoMapping profile_midi_ctrl_a[] = {
    {0, 2, 1.0f, 0.0f},         // App port 0 → HW port 2
    {1, 0, 1.0f, 0.0f},         // App port 1 → HW port 0（入れ替え）
    {2, 3, 0.5f, 0.0f},         // App port 2 → HW port 3, 半分のレンジ
};

} // namespace umi
```

---

## App API

```cpp
namespace umi {

struct IoContext {
    // カテゴリー別アクセス
    template<IoInputCategory Cat>
    [[nodiscard]] auto input(uint8_t index = 0) noexcept;
    
    template<IoOutputCategory Cat>
    [[nodiscard]] auto output(uint8_t index = 0) noexcept;
};

// 特殊化
template<>
[[nodiscard]] inline std::span<float> IoContext::input<IoInputCategory::KNOB>(uint8_t index) noexcept {
    return get_input_span<float>(IoInputCategory::KNOB, index);
}

template<>
[[nodiscard]] inline std::span<uint8_t> IoContext::input<IoInputCategory::BUTTON>(uint8_t index) noexcept {
    return get_input_span<uint8_t>(IoInputCategory::BUTTON, index);
}

template<>
[[nodiscard]] inline std::span<float> IoContext::output<IoOutputCategory::LED>(uint8_t index) noexcept {
    return get_output_span<float>(IoOutputCategory::LED, index);
}

// RGB LED は float[3] × count
template<>
[[nodiscard]] inline std::span<std::array<float, 3>> IoContext::output<IoOutputCategory::RGB_LED>(uint8_t index) noexcept {
    return get_output_span<std::array<float, 3>>(IoOutputCategory::RGB_LED, index);
}

} // namespace umi
```

---

## 使用例

### Processor (リアルタイム)

```cpp
void Processor::process(umi::AudioContext& ctx) {
    // === オーディオ（既存 API そのまま）===
    auto in_l = ctx.input(0);
    auto out_l = ctx.output(0);
    
    // === 連続状態入力（新 IoPort）===
    auto knobs = ctx.io.input<IoInputCategory::KNOB>();
    if (!knobs.empty()) {
        cutoff_ = knobs[0];      // 0.0 ~ 1.0 (標準形式)
        resonance_ = knobs[1];
    }
    
    auto buttons = ctx.io.input<IoInputCategory::BUTTON>();
    bool shift_held = !buttons.empty() && buttons[0];
    
    // === イベント（既存 EventRouter そのまま）===
    for (auto& ev : ctx.input_events) {
        if (ev.type == EventType::MIDI) {
            handle_midi(ev);
        }
    }
    
    // オーディオ処理...
}
```

### Controller (非リアルタイム)

```cpp
void Controller::update(umi::ControllerContext& ctx) {
    // === 連続状態出力（新 IoPort）===
    auto leds = ctx.io.output<IoOutputCategory::LED>();
    if (!leds.empty()) {
        for (size_t i = 0; i < leds.size(); ++i) {
            leds[i] = (i == selected_param_) ? 1.0f : 0.2f;
        }
    }
    
    auto rgb = ctx.io.output<IoOutputCategory::RGB_LED>();
    if (!rgb.empty()) {
        rgb[0] = {1.0f, 0.0f, 0.0f};  // 赤
        rgb[1] = {0.0f, 1.0f, 0.0f};  // 緑
    }
    
    // === イベント（既存 wait_event そのまま）===
    auto ev = wait_event();
    if (ev.type == ControlEventType::INPUT_CHANGE) {
        handle_input_change(ev);
    }
}
```

---

## 既存システムとの統合

| 既存コンポーネント | 変更 |
|------------------|------|
| EventRouter | 変更なし |
| RouteTable | 変更なし |
| ParamMapping | 変更なし |
| input_events | 変更なし |
| output_events | 変更なし |
| AudioContext.inputs/outputs | IoPort に統合可能（段階的移行） |
| SharedParamState | 維持（ParamMapping 経由の値） |
| SharedInputState | IoPort に置換 |
| led_state (atomic) | IoPort に置換 |

---

## 実行フロー

```
┌─────────────────────────────────────────────────────────────────┐
│ HW Layer (BSP)                                                  │
│   ADC → raw_adc[]                                               │
│   GPIO → raw_gpio                                               │
│   I2S DMA → audio_buffer                                        │
└─────────────────────────────────────────────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────────────────────────────┐
│ IoConverter + EventRouter                                       │
│                                                                 │
│   連続状態:                                                      │
│     raw_adc → IoConverter → SharedMemory (標準形式)             │
│                                                                 │
│   イベント:                                                      │
│     MIDI/GPIO変化 → EventRouter → AudioEventQueue/ControlQueue  │
└─────────────────────────────────────────────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────────────────────────────┐
│ SharedMemory (構造なし、PortBinding でアクセス)                  │
│   [audio_in][audio_out][knobs][buttons][leds][rgb]...           │
└─────────────────────────────────────────────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────────────────────────────┐
│ App                                                             │
│   Processor::process(ctx)                                       │
│     ctx.io.input<KNOB>()     → span<float>                      │
│     ctx.input_events         → span<Event> (既存)               │
│                                                                 │
│   Controller::update(ctx)                                       │
│     ctx.io.output<LED>()     → span<float>                      │
│     wait_event()             → ControlEvent (既存)              │
└─────────────────────────────────────────────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────────────────────────────┐
│ IoConverter (出力方向)                                          │
│   SharedMemory (標準形式) → IoConverter → HW raw                │
│     leds (float) → PWM 8bit                                     │
│     rgb (float×3) → WS2812B                                     │
└─────────────────────────────────────────────────────────────────┘
```

---

## 実装計画

| Phase | 内容 | 優先度 |
|-------|------|--------|
| 1 | IoInputCategory / IoOutputCategory 定義 | 高 |
| 2 | bind_input_ports()/bind_output_ports() カテゴリーマッチング | 高 |
| 3 | IoConverter 入力方向 (KNOB, BUTTON) | 高 |
| 4 | IoContext API (input/output) | 高 |
| 5 | IoConverter 出力方向 (LED, RGB_LED) | 中 |
| 6 | リマッププロファイル | 低 |
| 7 | DISPLAY カテゴリー対応 | 低 |

---

## 関連ドキュメント

- [03-event-system.md](../01-application/03-event-system.md) — イベントルーティング（既存、維持）
- [01-audio-context.md](../00-fundamentals/01-audio-context.md) — AudioContext（IoPort と統合予定）
- [10-shared-memory.md](../01-application/10-shared-memory.md) — 現在の SharedMemory 定義（IoPort で置換予定）
- [21-config-mismatch.md](../01-application/21-config-mismatch.md) — Capability マッチング（IoPort に発展）
