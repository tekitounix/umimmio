# ホストアダプター設計

MCUで動くコードをVST/AU/WASMでも動かすための仕組み。

---

## 概要

```
┌─────────────────────────────────────────────────────────┐
│              AudioProcessor（ユーザーコード）             │
│                     100% ポータブル                      │
├─────────────────────────────────────────────────────────┤
│                   Host Adapter Layer                     │
│                                                         │
│  各ホストの違いを吸収し、同一インターフェースを提供        │
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │              Adapter Interface                   │   │
│  │  - process() の呼び出し                          │   │
│  │  - MIDI イベントの変換と配信                      │   │
│  │  - パラメータの同期                              │   │
│  │  - 状態の保存/復元                               │   │
│  └─────────────────────────────────────────────────┘   │
│                                                         │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐      │
│  │   MCU   │ │  VST3   │ │   AU    │ │  WASM   │      │
│  │ Adapter │ │ Adapter │ │ Adapter │ │ Adapter │      │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘      │
└─────────────────────────────────────────────────────────┘
```

---

## 各ホストの違い

| 項目 | MCU (UMI-OS) | VST3 | AU | WASM |
|------|-------------|------|-----|------|
| オーディオコール | ISR→Task notify | processBlock() | render() | JS callback |
| MIDI形式 | umi::midi::Event | Steinberg::Vst::Event | MIDIPacket | Uint8Array |
| パラメータ | 直接アクセス | IEditController | AudioUnitParameter | JS object |
| サンプルレート | 固定(48kHz等) | 可変 | 可変 | 可変 |
| バッファサイズ | 固定(64-256) | 可変 | 可変 | 固定(128等) |
| スレッド | RTOSタスク | ホスト管理 | ホスト管理 | シングル |

---

## Adapter Interface

各アダプターが実装すべきインターフェース。

```cpp
namespace umi::adapter {

template<typename Processor>
struct AdapterBase {
    Processor processor;
    
    // === オーディオ処理 ===
    // ホストから呼ばれる → processor.process() を呼ぶ
    void process_audio(float** out, const float** in,
                       std::size_t frames, std::size_t channels);
    
    // === MIDI ===
    // ホスト形式 → umi::midi::Event に変換 → processor.on_midi()
    void handle_midi(/* ホスト固有の形式 */);
    
    // === パラメータ ===
    void set_param(std::size_t index, float value);
    float get_param(std::size_t index) const;
    
    // === 状態 ===
    std::size_t save_state(std::byte* buf, std::size_t max) const;
    void load_state(const std::byte* buf, std::size_t len);
    
    // === ライフサイクル ===
    void prepare(float sample_rate, std::size_t max_frames);
    void reset();
};

}
```

---

## MCU Adapter (UMI-OS)

カーネルがタスクを管理し、`processor` のメソッドを呼び出す。

```cpp
// adapter/mcu/run.hh
namespace umi::mcu {

template<typename Processor>
[[noreturn]] void run(Processor& proc) {
    // カーネル初期化
    Kernel<HW, MaxTasks> kernel;
    
    // オーディオバッファ（静的確保）
    alignas(4) static float out_buf[2][256];
    alignas(4) static float in_buf[2][256];
    
    // MIDI キュー
    static SpscQueue<midi::Event, 64> midi_queue;
    
    // オーディオタスク（Realtime）
    kernel.create_task({
        .entry = [](void* ctx) {
            auto& p = *static_cast<Processor*>(ctx);
            float* out[2] = {out_buf[0], out_buf[1]};
            const float* in[2] = {in_buf[0], in_buf[1]};
            
            while (true) {
                kernel.wait(Event::AudioReady);
                
                // MIDIイベント処理
                while (auto ev = midi_queue.try_pop()) {
                    p.on_midi(*ev);
                }
                
                // オーディオ処理
                auto start = DWT::cycles();
                p.process(out, in, 256, 2);
                load_monitor.record(DWT::cycles() - start);
            }
        },
        .arg = &proc,
        .prio = Priority::Realtime,
        .name = "audio",
    });
    
    // MIDI Server（High）
    kernel.create_task({
        .entry = midi_server_task,
        .prio = Priority::High,
        .name = "midi",
    });
    
    // DMA ISR（notify のみ）
    HW::set_audio_callback([] {
        kernel.notify_from_isr(audio_task, Event::AudioReady);
    });
    
    kernel.start();  // 戻らない
}

}
```

**使用方法:**
```cpp
#include "my_synth.hh"
#include <umi/mcu/run.hh>

int main() {
    MySynth synth;
    umi::mcu::run(synth);
}
```

---

## VST3 Adapter

Steinberg VST3 SDK とのブリッジ。

```cpp
// adapter/vst3/processor.hh
namespace umi::vst3 {

template<typename Processor>
class Vst3Processor : public Steinberg::Vst::AudioEffect {
    Processor proc_;
    
public:
    // VST3 からのオーディオ処理コール
    tresult process(ProcessData& data) override {
        // MIDI イベント変換
        if (data.inputEvents) {
            for (int32 i = 0; i < data.inputEvents->getEventCount(); ++i) {
                Event vstEvent;
                data.inputEvents->getEvent(i, vstEvent);
                
                // VST3 Event → umi::midi::Event
                if (vstEvent.type == Event::kNoteOnEvent) {
                    proc_.on_midi(midi::Event{
                        .type = midi::Type::NoteOn,
                        .channel = static_cast<uint8_t>(vstEvent.noteOn.channel),
                        .data1 = static_cast<uint8_t>(vstEvent.noteOn.pitch),
                        .data2 = static_cast<uint8_t>(vstEvent.noteOn.velocity * 127),
                    });
                }
                // ... 他のイベント
            }
        }
        
        // オーディオ処理
        float* out[2] = {data.outputs[0].channelBuffers32[0],
                         data.outputs[0].channelBuffers32[1]};
        const float* in[2] = {data.inputs[0].channelBuffers32[0],
                              data.inputs[0].channelBuffers32[1]};
        
        proc_.process(out, in, data.numSamples, 2);
        
        return kResultOk;
    }
    
    // パラメータ
    tresult setParamNormalized(ParamID id, ParamValue value) override {
        auto& info = Processor::params[id];
        float denorm = info.min + value * (info.max - info.min);
        proc_.set_param(id, denorm);
        return kResultOk;
    }
    
    // 状態保存
    tresult getState(IBStream* state) override {
        std::array<std::byte, 4096> buf;
        auto size = proc_.save_state(buf.data(), buf.size());
        state->write(buf.data(), size, nullptr);
        return kResultOk;
    }
};

// ファクトリ生成マクロ不要 - テンプレートで生成
template<typename Processor>
auto create() {
    return new Vst3Processor<Processor>();
}

}
```

**使用方法:**
```cpp
#include "my_synth.hh"
#include <umi/vst3/processor.hh>

// VST3 エントリーポイント
IPluginFactory* GetPluginFactory() {
    return umi::vst3::create_factory<MySynth>("MySynth", "1.0.0");
}
```

---

## AU Adapter

Apple Audio Unit とのブリッジ。

```cpp
// adapter/au/processor.hh
namespace umi::au {

template<typename Processor>
class AUProcessor : public AUAudioUnit {
    Processor proc_;
    
public:
    - (AUInternalRenderBlock)internalRenderBlock {
        return ^AUAudioUnitStatus(
            AudioUnitRenderActionFlags* flags,
            const AudioTimeStamp* timestamp,
            AUAudioFrameCount frameCount,
            NSInteger outputBusNumber,
            AudioBufferList* outputData,
            const AURenderEvent* realtimeEvents,
            AURenderPullInputBlock pullInput
        ) {
            // MIDI イベント処理
            for (auto event = realtimeEvents; event; event = event->head.next) {
                if (event->head.eventType == AURenderEventMIDI) {
                    proc_.on_midi(convert_midi(event->MIDI));
                }
            }
            
            // オーディオ処理
            float* out[2] = {(float*)outputData->mBuffers[0].mData,
                             (float*)outputData->mBuffers[1].mData};
            
            proc_.process(out, nullptr, frameCount, 2);
            
            return noErr;
        };
    }
};

}
```

---

## WASM Adapter

WebAssembly + JavaScript とのブリッジ。

```cpp
// adapter/wasm/processor.hh
namespace umi::wasm {

template<typename Processor>
class WasmProcessor {
    Processor proc_;
    
public:
    // JS から呼ばれる
    void process(uintptr_t out_ptr, uintptr_t in_ptr, 
                 std::size_t frames, std::size_t channels) {
        auto* out = reinterpret_cast<float**>(out_ptr);
        auto* in = reinterpret_cast<const float**>(in_ptr);
        proc_.process(out, in, frames, channels);
    }
    
    void on_midi(uint8_t status, uint8_t data1, uint8_t data2) {
        proc_.on_midi(midi::Event{
            .type = static_cast<midi::Type>(status & 0xF0),
            .channel = static_cast<uint8_t>(status & 0x0F),
            .data1 = data1,
            .data2 = data2,
        });
    }
    
    void set_param(std::size_t index, float value) {
        proc_.set_param(index, value);
    }
};

// Emscripten バインディング
template<typename Processor>
void expose(const char* name) {
    emscripten::class_<WasmProcessor<Processor>>(name)
        .constructor()
        .function("process", &WasmProcessor<Processor>::process)
        .function("onMidi", &WasmProcessor<Processor>::on_midi)
        .function("setParam", &WasmProcessor<Processor>::set_param);
}

}
```

**JavaScript 側:**
```javascript
// AudioWorklet 内
class MySynthProcessor extends AudioWorkletProcessor {
    constructor() {
        super();
        this.synth = new Module.MySynth();
        
        this.port.onmessage = (e) => {
            if (e.data.type === 'midi') {
                this.synth.onMidi(e.data.status, e.data.data1, e.data.data2);
            }
        };
    }
    
    process(inputs, outputs, parameters) {
        const outL = outputs[0][0];
        const outR = outputs[0][1];
        
        // WASM メモリに直接書き込み
        this.synth.process(outPtrs, inPtrs, 128, 2);
        
        return true;
    }
}
```

---

## 移植性を保証する制約

AudioProcessor は以下を守る必要がある:

| 制約 | 理由 |
|------|------|
| **STL 最小限** | MCU では使えないものがある |
| **動的メモリ禁止** | `process()` 内で malloc/new しない |
| **浮動小数点のみ** | double は MCU で遅い場合がある |
| **グローバル状態禁止** | 複数インスタンス対応 |
| **スレッド依存禁止** | ホストがスレッド管理 |

### 使用可能なもの

```cpp
// OK
#include <array>
#include <cstdint>
#include <cmath>        // sinf, cosf など（MCU でも使える）
#include <span>         // C++20
#include <optional>     // C++17

// NG（process() 内では）
#include <vector>       // 動的メモリ
#include <string>       // 動的メモリ
#include <thread>       // スレッド
#include <mutex>        // スレッド
```

---

## パラメータの同期

```cpp
// AudioProcessor でのパラメータ定義
struct MySynth : umi::AudioProcessor<MySynth> {
    static constexpr auto params = std::array{
        umi::ParamInfo{"cutoff", "Cutoff", 20.f, 20000.f, 1000.f},
        umi::ParamInfo{"reso", "Resonance", 0.f, 1.f, 0.5f},
    };
    
    // パラメータ値（atomic でスレッドセーフ）
    std::array<std::atomic<float>, params.size()> param_values_{
        1000.f, 0.5f  // デフォルト値
    };
    
    void set_param(std::size_t i, float v) {
        param_values_[i].store(v, std::memory_order_relaxed);
    }
    
    float get_param(std::size_t i) const {
        return param_values_[i].load(std::memory_order_relaxed);
    }
    
    void process(...) {
        float cutoff = get_param(0);
        float reso = get_param(1);
        // ...
    }
};
```

各アダプターがホスト側のパラメータ変更を `set_param()` に変換。

---

## ファイル構成

```
adapter/
├── common/
│   └── adapter_base.hh    # 共通インターフェース
├── mcu/
│   ├── run.hh             # umi::mcu::run()
│   └── hw_bridge.hh       # HW抽象化
├── vst3/
│   ├── processor.hh       # Vst3Processor
│   ├── controller.hh      # Vst3Controller (UI)
│   └── factory.hh         # プラグイン登録
├── au/
│   ├── processor.hh       # AUProcessor
│   └── view.hh            # AU View (UI)
└── wasm/
    ├── processor.hh       # WasmProcessor
    └── bindings.hh        # Emscripten バインディング
```
