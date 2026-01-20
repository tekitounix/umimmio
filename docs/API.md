# UMI API リファレンス

UMI の主要な API を解説します。

---

## 目次

1. [アプリケーション API](#アプリケーション-api)
2. [UI API (入出力抽象化)](#ui-api-入出力抽象化)
3. [共有メモリモデル](#共有メモリモデル)
4. [DSP モジュール](#dsp-モジュール)
5. [Kernel API (組込み)](#kernel-api-組込み)
6. [エラーハンドリング](#エラーハンドリング)

---

## アプリケーション API

統一 `main()` モデルで使用するAPI。組込み/Web共通。

```cpp
#include <umi/app.hh>
```

### ライフサイクル

| 関数 | 説明 |
|------|------|
| `umi::register_processor(proc)` | Processorをカーネルに登録 |
| `umi::wait_event()` | イベント待機（ブロッキング） |
| `umi::send_event(event)` | イベント送信 |
| `umi::log(msg)` | ログ出力 |
| `umi::get_time()` | 現在時刻取得（μs） |

### 最小実装例

```cpp
#include <umi/app.hh>
#include <umi/ui.hh>

struct Synth {
    float gain = 1.0f;
    
    void process(umi::ProcessContext& ctx) {
        auto* out = ctx.output(0);
        for (uint32_t i = 0; i < ctx.frames(); ++i) {
            out[i] = generate() * gain;
        }
    }
};

int main() {
    // 共有メモリ取得（カーネルが提供）
    auto& shared = umi::get_shared();
    
    // UI初期化（共有メモリ参照を渡す）
    umi::ui::Input input(shared);
    umi::ui::Output output(shared);
    
    // Processor登録
    static Synth synth;
    umi::register_processor(synth);
    
    // メインループ
    while (true) {
        auto ev = umi::wait_event();
        if (ev.type == umi::EventType::Shutdown) break;
        
        // 入力読み取り（インデックスのみ、デバイス種別は問わない）
        synth.gain = input[0];           // 0.0-1.0
        synth.cutoff = input[1];         // 0.0-1.0
        bool trigger = input.triggered(2); // 閾値を超えた瞬間
        
        // 出力更新（インデックスのみ、デバイス種別は問わない）
        output[0] = synth.gain > 0.5f;   // bool → 0.0/1.0
        output[1] = synth.get_level();   // 0.0-1.0
        output[2] = {1.0f, 0.0f, 0.0f};  // RGB
    }
    
    return 0;
}
```

---

### ProcessContext

オーディオ処理コールバック `process()` に渡されるコンテキスト。

```cpp
struct ProcessContext {
    uint32_t frames();              // バッファサイズ
    uint32_t sample_rate();         // サンプルレート
    float* output(uint32_t ch);     // 出力バッファ
    const float* input(uint32_t ch); // 入力バッファ
    
    // イベント
    auto events();                  // MIDIイベント等のイテレータ
    void send_to_control(Event e);  // Control Taskへイベント送信
};
```

#### 使用例

```cpp
void MyProcessor::process(umi::ProcessContext& ctx) {
    // MIDIイベント処理
    for (const auto& ev : ctx.events()) {
        if (ev.is_note_on()) {
            note_on(ev.note(), ev.velocity());
        }
    }
    
    // オーディオ生成
    auto* out = ctx.output(0);
    for (uint32_t i = 0; i < ctx.frames(); ++i) {
        out[i] = generate_sample();
    }
}
```

---

### Event

イベント型。MIDIメッセージ、パラメータ変更、UI入力など。

```cpp
namespace umi {
enum class EventType {
    Shutdown,
    MidiNoteOn, MidiNoteOff, MidiCC, MidiPitchBend,
    ParamChange,
    EncoderRotate, ButtonPress, ButtonRelease,
    DisplayUpdate, Meter,
};

struct Event {
    EventType type;
    union {
        struct { uint8_t ch, note, vel; } midi;
        struct { uint32_t id; float value; } param;
        struct { int id, delta; } encoder;
        struct { int id; } button;
    };
};
}
```

---

### パラメータ操作

```cpp
// Control Task から Processor のパラメータを変更
umi::set_param(PARAM_VOLUME, 0.8f);
float val = umi::get_param(PARAM_VOLUME);
```

---

### コルーチン (C++20)

```cpp
#include <umi/coro.hh>

umi::Task<void> my_task() {
    while (true) {
        auto ev = co_await umi::wait_event_async();
        if (ev.type == umi::EventType::Shutdown) co_return;
        handle(ev);
    }
}

umi::Task<void> display_task() {
    while (true) {
        co_await umi::sleep(33ms);  // 30fps
        update_display();
    }
}

int main() {
    static MyProcessor proc;
    umi::register_processor(proc);
    
    umi::Scheduler<4> sched;
    sched.spawn(my_task());
    sched.spawn(display_task());
    sched.run();
    
    return 0;
}
```

---

## Kernel API (組込み)

組込み環境でのカーネル直接操作。アプリケーションは通常 syscall 経由でアクセス。

```cpp
#include <umios/kernel/umi_kernel.hh>
```

### Syscall ABI

```cpp
namespace umi::syscall {
    constexpr uint32_t Exit = 0;          // アプリ終了
    constexpr uint32_t RegisterProc = 1;  // Processor登録
    constexpr uint32_t WaitEvent = 2;     // イベント待機
    constexpr uint32_t SendEvent = 3;     // イベント送信
    constexpr uint32_t Log = 10;          // ログ出力
    constexpr uint32_t GetTime = 11;      // 時刻取得
}
```

### 動的IRQ登録

```cpp
#include <umios/backend/cm/irq.hh>

// IRQハンドラをラムダで登録
umi::irq::init();
umi::irq::set_handler(irqn::DMA1_Stream5, +[]() {
    if (dma_i2s.transfer_complete()) {
        dma_i2s.clear_tc();
        g_kernel.notify(g_audio_task_id, Event::I2sReady);
    }
});
```

### Priority (優先度)

```cpp
enum class Priority : uint8_t {
    Realtime = 0,  // オーディオ処理 - 最高
    Server   = 1,  // ドライバ、I/O
    User     = 2,  // アプリケーション
    Idle     = 3,  // バックグラウンド - 最低
};
```

### Notification (タスク通知)

```cpp
// 通知を送信 (ISR-safe)
kernel.notify(task_id, Event::AudioReady);

// ブロッキング受信
uint32_t bits = kernel.wait(task_id, Event::AudioReady | Event::MidiReady);
```

### SpscQueue (ロックフリーキュー)

```cpp
umi::SpscQueue<int, 64> queue;

// Producer (ISR or task)
queue.try_push(42);

// Consumer (task)
if (auto val = queue.try_pop()) {
    process(*val);
}
```

---

## DSP モジュール

```cpp
#include <umidsp/oscillator.hh>
#include <umidsp/filter.hh>
#include <umidsp/envelope.hh>
```

### Oscillators

```cpp
umi::dsp::Sine sine;
umi::dsp::SawBL saw;      // バンドリミテッド
umi::dsp::SquareBL square;
umi::dsp::Triangle tri;

float freq_norm = 440.0f / sample_rate;
float sample = sine.tick(freq_norm);
```

### Filters

```cpp
// Biquad
umi::dsp::Biquad bq;
bq.set_lowpass(cutoff_norm, 0.707f);
float out = bq.tick(input);

// State Variable Filter
umi::dsp::SVF svf;
svf.set_params(cutoff_norm, resonance);
svf.tick(input);
float lp = svf.lp();
float hp = svf.hp();
```

### Envelopes

```cpp
umi::dsp::ADSR env;
env.set_params(0.01f, 0.1f, 0.7f, 0.3f);  // A, D, S, R

env.trigger();   // Note On
float val = env.tick(dt);
env.release();   // Note Off
```

### ユーティリティ

```cpp
float freq = umi::dsp::midi_to_freq(69);      // A4 = 440Hz
float gain = umi::dsp::db_to_gain(-6.0f);     // ≈ 0.5
float soft = umi::dsp::soft_clip(x);
```

---

## エラーハンドリング

```cpp
#include <umi/error.hh>

enum class Error : uint8_t {
    None,
    OutOfMemory, OutOfTasks,
    InvalidTask, InvalidState,
    InvalidParam, NullPointer,
    Timeout, WouldBlock,
    HardwareFault, DmaError,
    BufferOverrun, BufferUnderrun,
};

// Result型（C++23 std::expected）
umi::Result<int> divide(int a, int b) {
    if (b == 0) return umi::Err(Error::InvalidParam);
    return umi::Ok(a / b);
}

auto result = divide(10, 2);
if (result) {
    int value = *result;
}
```

---

## UI API (入出力抽象化)

`umi::ui` は**完全にハードウェア非依存**の統一インターフェースを提供します。
物理デバイスの種類（ノブ/スライダー/ボタン、LED/メーター/ディスプレイ）は区別しません。
全ては「値 + 変更イベント」に統一されます。

```cpp
#include <umi/ui.hh>
```

### 設計思想

**物理デバイスの違いはアプリから見えない**

| 物理デバイス | 抽象化 |
|--------------|--------|
| ポテンショメータ、スライダー、エンコーダ | `input[i]` → float (0.0-1.0) |
| ボタン、タクトスイッチ | `input[i]` → float (0.0 or 1.0) |
| XYパッド | `input[i]`, `input[i+1]` → float × 2 |
| LED、メーター、7セグ | `output[i]` ← float |
| RGB LED | `output[i]` ← float × 3 |
| ディスプレイ | `canvas` ← バッファ |

```
┌─────────────────────────────────────────────────────────────────────┐
│  Application                                                        │
│                                                                     │
│    float v = input[0];      // 値を読む                             │
│    bool c = input.changed(0); // 変化を検出                         │
│    output[0] = 0.8f;        // 値を書く                             │
│                                                                     │
└──────────────────────┬──────────────────────┬───────────────────────┘
                       │                      │
                       v                      v
┌─────────────────────────────────────────────────────────────────────┐
│  Shared Memory                                                      │
│  ┌────────────────────────┐  ┌────────────────────────┐             │
│  │ inputs[N]              │  │ outputs[M]             │             │
│  │  float value           │  │  float value[4]        │             │
│  │  bool  changed         │  │  bool  dirty           │             │
│  └────────────────────────┘  └────────────────────────┘             │
└──────────────────────┬──────────────────────┬───────────────────────┘
                       │                      │
                       v                      v
┌─────────────────────────────────────────────────────────────────────┐
│  Kernel / Driver (BSPで物理デバイスにマッピング)                     │
│  - ADC → 正規化 → inputs[0]                                         │
│  - outputs[0] → PWM → LED                                           │
│  - outputs[1] → I2C → 7seg                                          │
└─────────────────────────────────────────────────────────────────────┘
```

### Input (入力)

全ての入力は統一された `Input` クラスでアクセス。

```cpp
class Input {
public:
    explicit Input(SharedRegion& shared);
    
    // 値の読み取り（常に 0.0-1.0 に正規化済み）
    float operator[](uint8_t id) const;
    float get(uint8_t id) const;
    
    // 変化検出
    bool changed(uint8_t id) const;      // 前回から変化した
    bool triggered(uint8_t id) const;    // 0→1 に変化した瞬間
    bool released(uint8_t id) const;     // 1→0 に変化した瞬間
    
    // 相対値（エンコーダ等、読むとリセット）
    int delta(uint8_t id);
    
    // 入力数
    uint8_t count() const;
};
```

#### 使用例

```cpp
int main() {
    auto& shared = umi::get_shared();
    umi::ui::Input input(shared);
    umi::ui::Output output(shared);
    
    static Synth synth;
    umi::register_processor(synth);
    
    while (true) {
        auto ev = umi::wait_event();
        if (ev.type == umi::EventType::Shutdown) break;
        
        // 連続値（ノブでもスライダーでも同じ）
        synth.volume = input[0];
        synth.cutoff = input[1];
        synth.resonance = input[2];
        
        // 2値（ボタンでもフットスイッチでも同じ）
        if (input.triggered(3)) {
            synth.toggle_mode();
        }
        
        // 相対値（エンコーダでもホイールでも同じ）
        int d = input.delta(4);
        if (d != 0) {
            synth.adjust_preset(d);
        }
        
        // 出力
        output[0] = synth.volume;
        output[1] = synth.get_level();
    }
}
```

### Output (出力)

全ての出力は統一された `Output` クラスでアクセス。

```cpp
class Output {
public:
    explicit Output(SharedRegion& shared);
    
    // 単一値出力（0.0-1.0）
    OutputRef operator[](uint8_t id);
    void set(uint8_t id, float value);
    
    // 複数値出力（RGB等）
    void set(uint8_t id, float v0, float v1, float v2);
    void set(uint8_t id, std::span<const float> values);
    
    // 一括更新
    void flush();
    
    // 出力数
    uint8_t count() const;
};

// 代入演算子対応のプロキシ
class OutputRef {
public:
    OutputRef& operator=(float value);
    OutputRef& operator=(bool value);
    OutputRef& operator=(std::initializer_list<float> rgb);
    operator float() const;
};
```

#### 使用例

```cpp
// 単一値（LED、メーター、7セグ、全て同じ）
output[0] = 0.8f;          // 80%
output[1] = level;         // メーター
output[2] = is_active;     // bool → 0.0 or 1.0

// 複数値（RGB等）
output[3] = {1.0f, 0.0f, 0.0f};  // 赤
output.set(3, r, g, b);          // 同等

// 配列出力（LEDストリップ等）
float strip[16];
output.set_array(STRIP_ID, strip);

// 更新を反映
output.flush();
```

### Canvas (2次元出力)

ディスプレイ用のフレームバッファ。これも本質的には「大きな出力配列」。

```cpp
class Canvas {
public:
    explicit Canvas(SharedRegion& shared);
    
    int width() const;
    int height() const;
    
    // ピクセル単位（最も基本的な操作）
    void set(int x, int y, float brightness);
    void set(int x, int y, float r, float g, float b);
    
    // ユーティリティ（描画ヘルパー）
    void clear();
    void fill(float brightness);
    void line(int x0, int y0, int x1, int y1, float brightness);
    void rect(int x, int y, int w, int h, float brightness);
    void text(int x, int y, const char* str);
    
    // バッファ直接アクセス
    std::span<uint8_t> buffer();
    
    // 更新通知
    void flush();
};
```

### イベントベース vs ポーリング

```cpp
// イベントベース（推奨）
while (true) {
    auto ev = umi::wait_event();
    if (ev.type == umi::EventType::InputChanged) {
        // ev.input.id で変化した入力を特定
        handle_input(ev.input.id, input[ev.input.id]);
    }
}

// ポーリング（低レイテンシ用）
while (true) {
    umi::wait_event(1000);  // 1ms タイムアウト
    
    for (int i = 0; i < input.count(); ++i) {
        if (input.changed(i)) {
            handle_input(i, input[i]);
        }
    }
}
```

### コルーチンでの使用

```cpp
umi::Task<void> ui_task(umi::ui::Input& input, umi::ui::Output& output, Synth& synth) {
    while (true) {
        co_await umi::sleep(16ms);  // 60fps
        
        // 全入力を読む
        synth.volume = input[0];
        synth.cutoff = input[1];
        
        // 全出力を更新
        output[0] = synth.get_level();
        output[1] = synth.is_playing();
        output.flush();
    }
}

int main() {
    auto& shared = umi::get_shared();
    umi::ui::Input input(shared);
    umi::ui::Output output(shared);
    
    static Synth synth;
    umi::register_processor(synth);
    
    umi::Scheduler<2> sched;
    sched.spawn(ui_task(input, output, synth));
    sched.run();
    
    return 0;
}
```

---

## 共有メモリモデル

### アーキテクチャ

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Shared Memory                               │
├─────────────────────────────────────────────────────────────────────┤
│  inputs[N] ────────────────────────────────────────────────────────│
│  │ [0] : { value: 0.75, changed: true,  delta: 0 }                 │
│  │ [1] : { value: 0.50, changed: false, delta: 0 }                 │
│  │ [2] : { value: 1.00, changed: true,  delta: 0 }  ← ボタン       │
│  │ [3] : { value: 0.30, changed: true,  delta: 2 }  ← エンコーダ   │
│  └──────────────────────────────────────────────────────────────── │
│                                                                     │
│  outputs[M] ───────────────────────────────────────────────────────│
│  │ [0] : { value: [0.8, 0, 0, 0], dirty: true  }   ← 単一値        │
│  │ [1] : { value: [1.0, 0, 0, 0], dirty: true  }   ← LED           │
│  │ [2] : { value: [1.0, 0.5, 0, 0], dirty: true }  ← RGB           │
│  └──────────────────────────────────────────────────────────────── │
│                                                                     │
│  canvas ───────────────────────────────────────────────────────────│
│  │ buffer[width * height * bpp]                                    │
│  │ dirty: true                                                     │
│  └──────────────────────────────────────────────────────────────── │
│                                                                     │
│  audio, events, params (既存)                                       │
└─────────────────────────────────────────────────────────────────────┘
```

### SharedRegion 構造

```cpp
struct SharedRegion {
    // === 入力（カーネル→アプリ、読み取り専用） ===
    struct InputSlot {
        float value;              // 正規化値 (0.0-1.0)
        int16_t delta;            // 相対値（エンコーダ等）
        uint8_t flags;            // changed, triggered, released
    };
    InputSlot inputs[MAX_INPUTS];
    
    // === 出力（アプリ→カーネル） ===
    struct OutputSlot {
        float value[4];           // 最大4値（単一, RGB, RGBW等）
        uint8_t dirty;            // 更新フラグ
    };
    OutputSlot outputs[MAX_OUTPUTS];
    
    // === キャンバス ===
    struct CanvasSlot {
        uint8_t buffer[MAX_CANVAS_SIZE];
        uint16_t width, height;
        uint8_t bpp;
        uint8_t dirty;
    };
    CanvasSlot canvas;
    
    // === オーディオ ===
    struct Audio {
        float output[2][BUFFER_SIZE];
        float input[2][BUFFER_SIZE];
    };
    Audio audio;
    
    // === イベント ===
    SpscQueue<Event, 64> events;
    
    // === パラメータ（Processor用） ===
    std::atomic<float> params[MAX_PARAMS];
};
```

### 同期モデル

| データ | Writer | Reader | 方式 |
|--------|--------|--------|------|
| `inputs[]` | Kernel | App | 単一writer、flag通知 |
| `outputs[]` | App | Kernel | dirty flag + flush |
| `canvas` | App | Kernel | dirty + flush |
| `audio` | App (process) | Kernel (DMA) | ダブルバッファ |
| `events` | Kernel | App | SpscQueue |
| `params` | App (main) | App (process) | atomic relaxed |

### BSPによるマッピング

BSPが論理インデックス→物理デバイスのマッピングを定義:

```cpp
// lib/bsp/my_board/io_mapping.hh

namespace bsp::io {

// 入力マッピング（アプリからは単なるインデックス）
constexpr InputMapping inputs[] = {
    { .id = 0, .hw_type = HwType::Adc,     .hw_id = 0, .curve = Curve::Linear },
    { .id = 1, .hw_type = HwType::Adc,     .hw_id = 1, .curve = Curve::Log },
    { .id = 2, .hw_type = HwType::Gpio,    .hw_id = 0, .threshold = 0.5f },  // ボタン
    { .id = 3, .hw_type = HwType::Encoder, .hw_id = 0, .scale = 0.01f },
};

// 出力マッピング（アプリからは単なるインデックス）
constexpr OutputMapping outputs[] = {
    { .id = 0, .hw_type = HwType::Pwm,     .hw_id = 0 },  // LED (明るさ)
    { .id = 1, .hw_type = HwType::Gpio,    .hw_id = 1 },  // LED (ON/OFF)
    { .id = 2, .hw_type = HwType::PwmRgb,  .hw_id = 0 },  // RGB LED
    { .id = 3, .hw_type = HwType::I2c7Seg, .hw_id = 0 },  // 7セグ
};

// キャンバス設定
constexpr CanvasConfig canvas = {
    .hw_type = HwType::SpiOled,
    .hw_id = 0,
    .width = 128,
    .height = 64,
    .bpp = 1,
};

}
```

### 入力の更新フロー

```
Hardware          Kernel Driver         Shared Memory        Application
   │                    │                     │                    │
   │ ADC/GPIO/TIM IRQ   │                     │                    │
   │───────────────────>│                     │                    │
   │                    │ BSPマッピング参照    │                    │
   │                    │ ノイズ除去、正規化   │                    │
   │                    │ カーブ適用          │                    │
   │                    │                     │                    │
   │                    │ inputs[i].value = v │                    │
   │                    │ inputs[i].flags |= CHANGED               │
   │                    │────────────────────>│                    │
   │                    │                     │                    │
   │                    │ events.push(InputChanged, i)             │
   │                    │────────────────────>│                    │
   │                    │                     │                    │
   │                    │                     │ wait_event()       │
   │                    │                     │───────────────────>│
   │                    │                     │ input[i]           │
   │                    │                     │<───────────────────│
```

### 出力の更新フロー

```
Application          Shared Memory        Kernel Driver         Hardware
   │                      │                    │                    │
   │ output[0] = 0.8f     │                    │                    │
   │─────────────────────>│                    │                    │
   │ outputs[0].value[0]=0.8                   │                    │
   │ outputs[0].dirty=true│                    │                    │
   │                      │                    │                    │
   │ output.flush()       │                    │                    │
   │─────────────────────>│                    │                    │
   │                      │ BSPマッピング参照  │                    │
   │                      │<───────────────────│                    │
   │                      │ outputs[0]→PWM CH0 │                    │
   │                      │ outputs[2]→RGB PWM │                    │
   │                      │                    │───────────────────>│
   │                      │ dirty = false      │                    │
```

### プラットフォーム統一

| 操作 | 組込み | Web | Desktop |
|------|--------|-----|---------|
| `input[i]` | 共有メモリ | JS→WASM | MIDI CC / GUI |
| `output[i] = v` | 共有メモリ | Canvas/CSS | Log / GUI |
| `canvas.set()` | 共有メモリ | Canvas | Window |
| `flush()` | syscall→DMA | requestAnimationFrame | Render |

アプリコードは**完全に同一**。BSP/ランタイムがプラットフォーム差を吸収。

### process() での制約

```cpp
void MyProcessor::process(umi::ProcessContext& ctx) {
    // ✅ OK: オーディオバッファ
    auto* out = ctx.output(0);
    
    // ✅ OK: パラメータ（atomic読み取り）
    float cutoff = ctx.param(PARAM_CUTOFF);
    
    // ✅ OK: Control Taskへイベント送信
    ctx.send_to_control(Event::meter(0, peak));
    
    // ❌ NG: input/output操作（syscall/flag操作が必要）
    // float v = input[0];   // 禁止
    // output[0] = level;    // 禁止
}
```

---

## 関連ドキュメント

- [ARCHITECTURE.md](ARCHITECTURE.md) - アーキテクチャ概要
- [UMIP_SPEC.md](UMIP_SPEC.md) - Processor仕様
- [UMIC_SPEC.md](UMIC_SPEC.md) - Controller仕様
- [UMIM_SPEC.md](UMIM_SPEC.md) - バイナリ形式
- [SECURITY_ANALYSIS.md](SECURITY_ANALYSIS.md) - セキュリティとメモリ保護
