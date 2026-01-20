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
    umi::ui::View view(shared);
    umi::ui::Controls controls(shared);
    
    // Processor登録
    static Synth synth;
    umi::register_processor(synth);
    
    // メインループ
    while (true) {
        auto ev = umi::wait_event();
        if (ev.type == umi::EventType::Shutdown) break;
        
        // 入力読み取り（共有メモリから）
        float volume = controls.knob(0);  // 0.0-1.0
        synth.gain = volume;
        
        // 出力更新（共有メモリへ）
        view.led(0).set(volume > 0.5f);
        view.meter(0).set(volume);
        view.text(0).set("Vol: %.0f%%", volume * 100);
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

`umi::ui` はハードウェア非依存の統一インターフェースを提供します。
アプリは共有メモリを読み書きするだけで、HW依存処理はドライバー/サーバータスクが吸収します。

```cpp
#include <umi/ui.hh>
```

### 設計思想

```
┌─────────────────────────────────────────────────────────────────────┐
│  Application Layer                                                  │
│  ┌────────────────┐  ┌────────────────┐                             │
│  │ umi::ui::View  │  │umi::ui::Controls│  ← 統一API                 │
│  │  .led(i)       │  │  .knob(i)      │                             │
│  │  .rgb(i)       │  │  .slider(i)    │                             │
│  │  .meter(i)     │  │  .button(i)    │                             │
│  │  .text(i)      │  │  .encoder(i)   │                             │
│  │  .canvas()     │  │  .xy_pad(i)    │                             │
│  └───────┬────────┘  └───────┬────────┘                             │
│          │ write              │ read                                │
└──────────┼────────────────────┼─────────────────────────────────────┘
           v                    v
┌─────────────────────────────────────────────────────────────────────┐
│  Shared Memory (umi::SharedRegion)                                  │
│  ┌──────────────────┐  ┌──────────────────┐                         │
│  │ outputs[]        │  │ inputs[]         │                         │
│  │  .value (float)  │  │  .value (float)  │                         │
│  │  .dirty (bool)   │  │  .changed (bool) │                         │
│  └──────────────────┘  └──────────────────┘                         │
└──────────┬────────────────────┬─────────────────────────────────────┘
           │ read               │ write
           v                    v
┌─────────────────────────────────────────────────────────────────────┐
│  Kernel / Driver Layer (Server Tasks)                               │
│  - ノイズ除去、正規化、デバウンス                                    │
│  - 更新レート制御、DMA転送                                           │
│  - HW固有処理を完全に隠蔽                                            │
└─────────────────────────────────────────────────────────────────────┘
```

### Controls (入力)

全ての入力デバイスを統一的に扱います。

```cpp
class Controls {
public:
    explicit Controls(SharedRegion& shared);
    
    // 連続値入力（0.0-1.0 正規化済み）
    float knob(uint8_t id) const;      // ポテンショメータ
    float slider(uint8_t id) const;    // リニアフェーダー
    float encoder(uint8_t id) const;   // エンコーダ累積値（0.0-1.0にクランプ）
    
    // 相対値入力
    int encoder_delta(uint8_t id);     // エンコーダ増分（読むとリセット）
    
    // 2値入力
    bool button(uint8_t id) const;     // ボタン状態
    bool button_pressed(uint8_t id);   // 押された瞬間（読むとリセット）
    bool button_released(uint8_t id);  // 離された瞬間（読むとリセット）
    
    // 2次元入力（0.0-1.0, 0.0-1.0）
    std::pair<float, float> xy_pad(uint8_t id) const;
    
    // 変化検出
    bool changed(uint8_t id) const;    // 前回読み取りから変化したか
    
    // 一括読み取り
    void poll();                       // 全入力の変化フラグを更新
};
```

#### 使用例

```cpp
int main() {
    auto& shared = umi::get_shared();
    umi::ui::Controls ctrl(shared);
    
    static Synth synth;
    umi::register_processor(synth);
    
    while (true) {
        auto ev = umi::wait_event();
        if (ev.type == umi::EventType::Shutdown) break;
        
        // 連続値（すでに正規化・ノイズ除去済み）
        synth.volume = ctrl.knob(0);
        synth.cutoff = ctrl.knob(1);
        synth.resonance = ctrl.knob(2);
        
        // ボタン
        if (ctrl.button_pressed(0)) {
            synth.toggle_mode();
        }
        
        // エンコーダ（相対値）
        int delta = ctrl.encoder_delta(0);
        if (delta != 0) {
            synth.adjust_preset(delta);
        }
    }
}
```

### View (出力)

全ての出力デバイスを統一的に扱います。

```cpp
class View {
public:
    explicit View(SharedRegion& shared);
    
    // 単一値出力
    Led led(uint8_t id);           // 単色LED
    Rgb rgb(uint8_t id);           // RGB LED
    Meter meter(uint8_t id);       // レベルメーター/VU
    Numeric numeric(uint8_t id);   // 数値表示（7セグ等）
    Text text(uint8_t id);         // テキスト表示
    
    // 2次元出力
    Canvas canvas();               // フレームバッファ（OLED/LCD）
    
    // 一括更新
    void flush();                  // dirty出力をカーネルに通知
};
```

#### Led / Rgb

```cpp
class Led {
public:
    void set(bool on);
    void set(float brightness);    // 0.0-1.0（PWM対応時）
    void toggle();
    bool get() const;
};

class Rgb {
public:
    void set(float r, float g, float b);  // 各0.0-1.0
    void set(uint32_t rgb24);             // 0xRRGGBB
    void set_hsv(float h, float s, float v);
    void set_brightness(float b);         // 全体の明るさ
};
```

```cpp
// 使用例
view.led(0).set(true);
view.led(1).set(0.5f);  // 50%明るさ

view.rgb(0).set(1.0f, 0.0f, 0.0f);  // 赤
view.rgb(0).set(0xFF0000);          // 赤
view.rgb(0).set_hsv(0.66f, 1.0f, 1.0f);  // 青
```

#### Meter / Numeric

```cpp
class Meter {
public:
    void set(float value);         // 0.0-1.0
    void set_peak(float value);    // ピークホールド
    void set_range(float min_db, float max_db);  // dBスケール設定
};

class Numeric {
public:
    void set(int value);
    void set(float value, int decimals = 1);
    void set_format(const char* fmt);  // printf形式
};
```

```cpp
// 使用例
view.meter(0).set(peak_level);
view.meter(0).set_range(-60.0f, 0.0f);

view.numeric(0).set(440);          // "440"
view.numeric(1).set(3.14f, 2);     // "3.14"
```

#### Text

```cpp
class Text {
public:
    void set(const char* str);
    void set(const char* fmt, ...);  // printf形式
    void clear();
};
```

```cpp
// 使用例
view.text(0).set("Volume");
view.text(1).set("Freq: %d Hz", 440);
```

#### Canvas (フレームバッファ)

```cpp
class Canvas {
public:
    int width() const;
    int height() const;
    
    // 基本描画
    void clear();
    void pixel(int x, int y, float brightness);
    void pixel(int x, int y, float r, float g, float b);
    
    // 図形
    void line(int x0, int y0, int x1, int y1, float brightness);
    void rect(int x, int y, int w, int h, float brightness);
    void fill_rect(int x, int y, int w, int h, float brightness);
    void circle(int cx, int cy, int r, float brightness);
    
    // テキスト
    void text(int x, int y, const char* str, FontSize size = FontSize::Small);
    
    // ビットマップ
    void bitmap(int x, int y, const uint8_t* data, int w, int h);
    
    // 波形/グラフ
    void waveform(int x, int y, int w, int h, std::span<const float> samples);
    void bar_graph(int x, int y, int w, int h, std::span<const float> values);
};
```

```cpp
// 使用例
auto& canvas = view.canvas();
canvas.clear();
canvas.text(0, 0, "UMI Synth", FontSize::Large);
canvas.text(0, 16, "Vol: 80%");
canvas.meter_bar(0, 32, 128, 8, volume);
canvas.waveform(0, 48, 128, 16, waveform_data);
view.flush();
```

### RGB Strip (配列出力)

```cpp
class RgbStrip {
public:
    explicit RgbStrip(SharedRegion& shared, uint8_t strip_id);
    
    int size() const;
    
    void set(int index, float r, float g, float b);
    void set(int index, uint32_t rgb24);
    void fill(float r, float g, float b);
    void fill(uint32_t rgb24);
    
    // エフェクト
    void gradient(uint32_t start_color, uint32_t end_color);
    void shift(int amount);  // 全体をシフト
};
```

### コルーチンでの使用

```cpp
umi::Task<void> ui_task(umi::ui::View& view, umi::ui::Controls& ctrl, Synth& synth) {
    while (true) {
        co_await umi::sleep(16ms);  // 60fps
        
        // 入力
        synth.volume = ctrl.knob(0);
        synth.cutoff = ctrl.knob(1);
        
        // 出力
        view.meter(0).set(synth.get_level());
        view.led(0).set(synth.is_playing());
        
        auto& canvas = view.canvas();
        canvas.clear();
        draw_ui(canvas, synth);
        view.flush();
    }
}

int main() {
    auto& shared = umi::get_shared();
    umi::ui::View view(shared);
    umi::ui::Controls ctrl(shared);
    
    static Synth synth;
    umi::register_processor(synth);
    
    umi::Scheduler<2> sched;
    sched.spawn(ui_task(view, ctrl, synth));
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
│  inputs[] ─────────────────────────────────────────────────────────│
│  │ [0] knob0   : { value: 0.75, changed: true  }                   │
│  │ [1] knob1   : { value: 0.50, changed: false }                   │
│  │ [2] button0 : { value: 1.00, changed: true, pressed: true }     │
│  │ ...                                                              │
│  └──────────────────────────────────────────────────────────────── │
│                                                                     │
│  outputs[] ────────────────────────────────────────────────────────│
│  │ [0] led0    : { value: 1.0, dirty: true  }                      │
│  │ [1] rgb0    : { r: 1.0, g: 0.0, b: 0.0, dirty: true }           │
│  │ [2] meter0  : { value: 0.8, dirty: false }                      │
│  │ ...                                                              │
│  └──────────────────────────────────────────────────────────────── │
│                                                                     │
│  canvas[] ─────────────────────────────────────────────────────────│
│  │ framebuffer[width * height]                                     │
│  │ dirty_rect: { x, y, w, h }                                      │
│  └──────────────────────────────────────────────────────────────── │
│                                                                     │
│  audio ────────────────────────────────────────────────────────────│
│  │ output[ch][frames], input[ch][frames]                           │
│  └──────────────────────────────────────────────────────────────── │
│                                                                     │
│  events ───────────────────────────────────────────────────────────│
│  │ SpscQueue<Event, 64>                                            │
│  └──────────────────────────────────────────────────────────────── │
│                                                                     │
│  params ───────────────────────────────────────────────────────────│
│  │ atomic<float> values[MAX_PARAMS]                                │
│  └──────────────────────────────────────────────────────────────── │
└─────────────────────────────────────────────────────────────────────┘
```

### SharedRegion 構造

```cpp
struct SharedRegion {
    // === 入力（カーネル→アプリ、読み取り専用） ===
    struct Input {
        float value;              // 正規化値 (0.0-1.0)
        uint8_t flags;            // changed, pressed, released等
    };
    Input inputs[MAX_INPUTS];
    
    // === 出力（アプリ→カーネル） ===
    struct Output {
        float value;              // 主値
        float aux[3];             // 補助値（RGB等）
        uint8_t dirty;            // 更新フラグ
    };
    Output outputs[MAX_OUTPUTS];
    
    // === キャンバス ===
    struct Canvas {
        uint8_t buffer[MAX_CANVAS_SIZE];
        uint16_t width, height;
        uint8_t bpp;              // 1, 8, 16, 24
        uint32_t dirty_rect;      // パックされた領域
    };
    Canvas canvas;
    
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
    
    // === Processor → Control Task ===
    SpscQueue<Event, 32> proc_to_ctrl;
};
```

### 同期モデル

| データ | Writer | Reader | 方式 |
|--------|--------|--------|------|
| `inputs[]` | Kernel | App | 単一writer、コピー不要 |
| `outputs[]` | App | Kernel | dirty flag |
| `canvas` | App | Kernel | dirty_rect + flush |
| `audio` | App (process) | Kernel (DMA) | ダブルバッファ |
| `events` | Kernel | App | SpscQueue |
| `params` | App (main) | App (process) | atomic relaxed |

### 入力の更新フロー

```
Hardware          Kernel Driver         Shared Memory        Application
   │                    │                     │                    │
   │ ADC IRQ            │                     │                    │
   │───────────────────>│                     │                    │
   │                    │ ノイズ除去          │                    │
   │                    │ 正規化 (0-4095→0.0-1.0)                  │
   │                    │ 閾値判定            │                    │
   │                    │                     │                    │
   │                    │ inputs[i].value = normalized            │
   │                    │ inputs[i].changed = true                 │
   │                    │────────────────────>│                    │
   │                    │                     │                    │
   │                    │ event.push(InputChanged)                 │
   │                    │────────────────────>│                    │
   │                    │                     │                    │
   │                    │                     │ wait_event() 返る  │
   │                    │                     │───────────────────>│
   │                    │                     │                    │
   │                    │                     │ ctrl.knob(i)       │
   │                    │                     │<───────────────────│
   │                    │                     │ return value       │
   │                    │                     │───────────────────>│
```

### 出力の更新フロー

```
Application          Shared Memory        Kernel Driver         Hardware
   │                      │                    │                    │
   │ view.led(0).set(true)│                    │                    │
   │─────────────────────>│                    │                    │
   │ outputs[0].value = 1.0                    │                    │
   │ outputs[0].dirty = true                   │                    │
   │                      │                    │                    │
   │ view.flush()         │                    │                    │
   │─────────────────────>│                    │                    │
   │ syscall(FLUSH)       │                    │                    │
   │──────────────────────────────────────────>│                    │
   │                      │ dirty出力をスキャン│                    │
   │                      │<───────────────────│                    │
   │                      │                    │ GPIO/PWM/DMA操作   │
   │                      │                    │───────────────────>│
   │                      │ dirty = false      │                    │
   │                      │<───────────────────│                    │
   │<─────────────────────────────────────────│                    │
```

### process() での制約

```cpp
void MyProcessor::process(umi::ProcessContext& ctx) {
    // ✅ OK: オーディオバッファ（共有メモリ直接アクセス）
    auto* out = ctx.output(0);
    
    // ✅ OK: パラメータ（atomic読み取り）
    float cutoff = ctx.param(PARAM_CUTOFF);
    
    // ✅ OK: Control Taskへイベント送信（ロックフリーキュー）
    ctx.send_to_control(Event::meter(0, peak));
    
    // ❌ NG: UI操作（syscall必要）
    // view.led(0).set(true);  // 禁止！
    
    // ❌ NG: 入力読み取り（変化フラグ操作）
    // ctrl.knob(0);  // 禁止！
}
```

### BSPによるマッピング

BSPが論理ID→物理デバイスのマッピングを定義:

```cpp
// lib/bsp/my_board/ui_mapping.hh

namespace bsp::ui {

// 入力マッピング
constexpr InputMapping inputs[] = {
    { .id = 0, .type = InputType::Adc,     .hw_id = 0 },  // knob0 → ADC CH0
    { .id = 1, .type = InputType::Adc,     .hw_id = 1 },  // knob1 → ADC CH1
    { .id = 2, .type = InputType::Encoder, .hw_id = 0 },  // encoder0 → TIM2
    { .id = 3, .type = InputType::Button,  .hw_id = 0 },  // button0 → PA0
};

// 出力マッピング
constexpr OutputMapping outputs[] = {
    { .id = 0, .type = OutputType::Led,    .hw_id = 0 },  // led0 → PA5
    { .id = 1, .type = OutputType::Rgb,    .hw_id = 0 },  // rgb0 → TIM3 CH1-3
    { .id = 2, .type = OutputType::Meter,  .hw_id = 0 },  // meter0 → 7seg
};

// キャンバス設定
constexpr CanvasConfig canvas = {
    .type = CanvasType::Oled,
    .width = 128,
    .height = 64,
    .bpp = 1,
};

}
```

### プラットフォーム統一

| 操作 | 組込み | Web | Desktop |
|------|--------|-----|---------|
| `ctrl.knob(i)` | 共有メモリ読み取り | JS→WASM | MIDI CC |
| `view.led(i).set()` | 共有メモリ書き込み | Canvas/CSS | Log |
| `view.canvas().pixel()` | 共有メモリ書き込み | Canvas | Window |
| `view.flush()` | syscall→DMA | requestAnimationFrame | Render |

アプリコードは**完全に同一**で、プラットフォーム差はBSP/ランタイムが吸収。

---

## 関連ドキュメント

- [ARCHITECTURE.md](ARCHITECTURE.md) - アーキテクチャ概要
- [UMIP_SPEC.md](UMIP_SPEC.md) - Processor仕様
- [UMIC_SPEC.md](UMIC_SPEC.md) - Controller仕様
- [UMIM_SPEC.md](UMIM_SPEC.md) - バイナリ形式
- [SECURITY_ANALYSIS.md](SECURITY_ANALYSIS.md) - セキュリティとメモリ保護
