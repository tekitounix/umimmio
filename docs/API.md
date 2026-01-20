# UMI API リファレンス

UMI の主要な API を解説します。

---

## 目次

1. [アプリケーション API](#アプリケーション-api)
2. [Kernel API (組込み)](#kernel-api-組込み)
3. [DSP モジュール](#dsp-モジュール)
4. [エラーハンドリング](#エラーハンドリング)

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

struct Volume {
    float gain = 1.0f;
    
    void process(umi::ProcessContext& ctx) {
        auto* out = ctx.output(0);
        auto* in = ctx.input(0);
        for (uint32_t i = 0; i < ctx.frames(); ++i) {
            out[i] = in[i] * gain;
        }
    }
};

int main() {
    static Volume vol;
    umi::register_processor(vol);
    
    while (umi::wait_event().type != umi::EventType::Shutdown) {}
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

## 関連ドキュメント

- [ARCHITECTURE.md](ARCHITECTURE.md) - アーキテクチャ概要
- [UMIP_SPEC.md](UMIP_SPEC.md) - Processor仕様
- [UMIC_SPEC.md](UMIC_SPEC.md) - Controller仕様
- [UMIM_SPEC.md](UMIM_SPEC.md) - バイナリ形式
