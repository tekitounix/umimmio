# アプリケーション API

統一 `main()` モデルで使用するAPI。組込み/Web共通。

```cpp
#include <umi/app.hh>
```

---

## ライフサイクル

| 関数 | 説明 |
|------|------|
| `umi::register_processor(proc)` | Processorをカーネルに登録 |
| `umi::wait_event()` | イベント待機（ブロッキング） |
| `umi::send_event(event)` | イベント送信 |
| `umi::log(msg)` | ログ出力 |
| `umi::get_time()` | 現在時刻取得（μs） |

---

## 最小実装例

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

## ProcessContext

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

### 使用例

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

## Event

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

## パラメータ操作

```cpp
// Control Task から Processor のパラメータを変更
umi::set_param(PARAM_VOLUME, 0.8f);
float val = umi::get_param(PARAM_VOLUME);
```

---

## コルーチン (C++20)

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

## 関連ドキュメント

- [API.md](API.md) - API インデックス
- [API_UI.md](API_UI.md) - UI API
- [API_DSP.md](API_DSP.md) - DSPモジュール
- [API_KERNEL.md](API_KERNEL.md) - Kernel API
