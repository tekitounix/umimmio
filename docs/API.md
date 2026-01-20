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

## 共有メモリとハードウェアI/O

アプリケーションはカーネルと**共有メモリ**経由で通信します。
直接ハードウェアにアクセスすることはできません（MPUで保護）。

### アーキテクチャ

```
+------------------+     +------------------+     +------------------+
|  Hardware        |     |  Kernel + BSP    |     |  Application     |
|  (ADC, GPIO,     | --> |  (IRQ handlers,  | --> |  (main, process) |
|   Encoder, I2S)  |     |   DMA, drivers)  |     |                  |
+------------------+     +------------------+     +------------------+
                              |
                              v
                    +------------------+
                    |  Shared Memory   |
                    |  - AudioBuffer   |
                    |  - EventQueue    |
                    |  - ParamBlock    |
                    |  - HardwareState |
                    |  - DisplayBuffer |
                    +------------------+
```

### 共有メモリの実装

#### メモリ配置とハードウェア構成

共有メモリの物理配置はBSPが担当。APIは共通。

| ハードウェア構成 | 共有メモリ配置 | 注意点 |
|------------------|----------------|--------|
| 内蔵SRAMのみ | `.shared` セクション | サイズ制約 |
| 内蔵 + SDRAM | オーディオはSDRAM | キャッシュ設定 |
| CCM + SRAM | 高速データはCCM | DMA不可 |

#### BSPによる抽象化

```cpp
// lib/bsp/<board>/shared_memory.hh

// BSPがボード固有の配置を定義
namespace bsp {

// リンカスクリプトと連携
extern SharedRegion __shared_region __attribute__((section(".shared")));

// 初期化（キャッシュ、MPU設定含む）
void init_shared_memory() {
    // SDRAM初期化（必要なら）
    init_sdram();
    
    // MPU設定: 共有領域をnon-cacheable or write-through
    mpu::configure_region(MPU_REGION_SHARED, {
        .base = &__shared_region,
        .size = sizeof(SharedRegion),
        .access = mpu::RW_RW,
        .attributes = mpu::SHARED_DEVICE,  // キャッシュ無効
    });
}

}
```

#### リンカスクリプト例（SDRAM使用時）

```ld
/* boards/my_board/linker.ld */
MEMORY {
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1M
    SRAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
    SDRAM (rwx) : ORIGIN = 0xD0000000, LENGTH = 8M
}

SECTIONS {
    /* オーディオバッファはSDRAMに配置 */
    .shared_audio (NOLOAD) : {
        . = ALIGN(32);  /* キャッシュライン境界 */
        *(.shared_audio)
    } > SDRAM
    
    /* イベント/パラメータは高速SRAMに配置 */
    .shared_fast (NOLOAD) : {
        *(.shared_fast)
    } > SRAM
}
```

#### DMAとの整合性

```cpp
// オーディオバッファはDMAと共有
// キャッシュコヒーレンシを保証

// 方法1: Non-cacheable領域（推奨）
// → MPUでSHARED_DEVICEに設定

// 方法2: 手動キャッシュ操作（パフォーマンス重視）
void before_dma_read() {
    SCB_InvalidateDCache_by_Addr(audio_buf, size);
}
void after_dma_write() {
    SCB_CleanDCache_by_Addr(audio_buf, size);
}
```

### 共有メモリ構造

```cpp
// カーネルが管理、アプリからはAPIでアクセス
struct SharedRegion {
    // オーディオバッファ（process()で直接アクセス）
    struct {
        float output[2][BUFFER_SIZE];
        float input[2][BUFFER_SIZE];
    } audio;
    
    // イベントキュー（wait_event()で取得）
    struct {
        Event buffer[64];
        std::atomic<uint32_t> head, tail;
    } events;
    
    // パラメータ（get_param/set_paramでアクセス）
    struct {
        std::atomic<float> values[MAX_PARAMS];
    } params;
    
    // ハードウェア状態（カーネルが更新、アプリは読み取り専用）
    struct {
        uint16_t adc[8];        // ADC値 (0-4095)
        int16_t encoders[4];    // エンコーダ累積値
        uint32_t buttons;       // ボタン状態ビットマップ
        uint32_t timestamp_us;  // 最終更新時刻
    } hw_state;
    
    // ディスプレイバッファ（アプリが書き込み、カーネルが転送）
    struct {
        uint8_t oled[128 * 64 / 8];  // 1bpp
        uint16_t lcd[320 * 240];     // RGB565
        uint32_t dirty;              // 更新フラグ
    } display;
};
```

---

## ハードウェア入力

### 入力デバイスの種類

| デバイス | データ型 | 更新方式 | 典型的な用途 |
|----------|----------|----------|--------------|
| ADC (ポット) | 0-4095 | ポーリング/閾値 | ノブ、スライダー |
| エンコーダ | 相対値 (delta) | IRQ | 無限回転ノブ |
| ボタン | ON/OFF | IRQ | スイッチ、タクト |
| タッチパッド | x, y, pressure | ポーリング | XY pad |
| ジャイロ/加速度 | 3軸 float | ポーリング | モーションコントロール |
| 距離センサ | mm | ポーリング | 非接触コントロール |

### 入力取得方法

#### 方法1: イベント駆動（推奨）

カーネルがハードウェア変化を検出してイベント通知。
**低CPU負荷、応答性良好。**

```cpp
int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    while (true) {
        auto ev = umi::wait_event();
        
        switch (ev.type) {
        case umi::EventType::Shutdown:
            return 0;
            
        // エンコーダ（相対値）
        case umi::EventType::EncoderRotate:
            // id: エンコーダ番号, delta: 回転量（正=時計回り）
            adjust_param(ev.encoder.id, ev.encoder.delta);
            break;
            
        // ボタン
        case umi::EventType::ButtonPress:
            handle_button_down(ev.button.id);
            break;
        case umi::EventType::ButtonRelease:
            handle_button_up(ev.button.id);
            break;
            
        // ADC（閾値を超えて変化した場合のみ通知）
        case umi::EventType::AdcChange:
            // channel: ADCチャンネル, value: 0.0-1.0 正規化値
            set_param_from_knob(ev.adc.channel, ev.adc.value);
            break;
            
        // タッチ
        case umi::EventType::TouchBegin:
        case umi::EventType::TouchMove:
        case umi::EventType::TouchEnd:
            handle_touch(ev.touch.x, ev.touch.y, ev.touch.pressure);
            break;
        }
    }
}
```

#### 方法2: ポーリング（高精度用）

連続的な値の変化を高精度で追跡する場合。
**CPU負荷高め、レイテンシ制御可能。**

```cpp
int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    // 前回値を保持
    std::array<uint16_t, 8> prev_adc{};
    
    while (true) {
        // タイムアウト付きイベント待機（1ms）
        auto ev = umi::wait_event(1000);  // 1000μs = 1ms
        
        if (ev.type != umi::EventType::Timeout) {
            // イベント処理
            handle_event(ev);
        }
        
        // 1msごとにADCをポーリング
        auto hw = umi::get_hw_state();
        for (int i = 0; i < 8; ++i) {
            int16_t diff = hw.adc[i] - prev_adc[i];
            if (std::abs(diff) > 8) {  // ノイズ除去
                float normalized = hw.adc[i] / 4095.0f;
                apply_adc_value(i, normalized);
                prev_adc[i] = hw.adc[i];
            }
        }
    }
}
```

#### 方法3: コルーチンで分離（推奨）

イベント処理とポーリングを別タスクに分離。

```cpp
// イベント処理タスク
umi::Task<void> event_task(Synth& synth) {
    while (true) {
        auto ev = co_await umi::wait_event_async();
        if (ev.type == umi::EventType::Shutdown) co_return;
        synth.handle_event(ev);
    }
}

// 高速ADCポーリングタスク（モジュレーション用）
umi::Task<void> modulation_task(Synth& synth) {
    while (true) {
        co_await umi::sleep(500us);  // 2kHz
        auto hw = umi::get_hw_state();
        synth.apply_modwheel(hw.adc[MOD_WHEEL_CH] / 4095.0f);
    }
}

// ディスプレイ更新タスク
umi::Task<void> display_task(Synth& synth) {
    while (true) {
        co_await umi::sleep(33ms);  // 30fps
        update_display(synth);
    }
}

int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    umi::Scheduler<4> sched;
    sched.spawn(event_task(synth));
    sched.spawn(modulation_task(synth));
    sched.spawn(display_task(synth));
    sched.run();
    
    return 0;
}
```

### ADC値の処理

```cpp
// 正規化（0.0-1.0）
float normalize_adc(uint16_t raw) {
    return raw / 4095.0f;
}

// 対数スケール（周波数パラメータ用）
float adc_to_freq(uint16_t raw, float min_hz, float max_hz) {
    float norm = raw / 4095.0f;
    return min_hz * std::pow(max_hz / min_hz, norm);
}

// デッドゾーン付き（ピッチベンド用）
float adc_with_deadzone(uint16_t raw, float deadzone = 0.02f) {
    float norm = (raw / 4095.0f) * 2.0f - 1.0f;  // -1.0 to 1.0
    if (std::abs(norm) < deadzone) return 0.0f;
    return norm;
}

// ヒステリシス（チャタリング防止）
class AdcWithHysteresis {
    uint16_t value_ = 0;
    uint16_t threshold_ = 16;
public:
    bool update(uint16_t raw) {
        if (std::abs(raw - value_) > threshold_) {
            value_ = raw;
            return true;  // 変化あり
        }
        return false;
    }
    uint16_t value() const { return value_; }
};
```

---

## ハードウェア出力

### 出力デバイスの種類

| デバイス | インターフェース | 更新方式 | API |
|----------|------------------|----------|-----|
| 単色LED | GPIO | 即時 | `set_led()` |
| RGB LED | PWM / WS2812 | 即時 | `set_rgb()` |
| 7セグメント | GPIO/SPI | 即時 | `set_7seg()` |
| OLED (SSD1306等) | I2C/SPI | バッファ | `draw_*()` + `flush()` |
| LCD (ILI9341等) | SPI/Parallel | バッファ | `draw_*()` + `flush()` |
| モーター/サーボ | PWM | 即時 | `set_pwm()` |

### LED制御

```cpp
// 単色LED
umi::set_led(LED_POWER, true);    // ON
umi::set_led(LED_MIDI, false);    // OFF
umi::toggle_led(LED_STATUS);      // トグル

// LED点滅（コルーチン）
umi::Task<void> blink_task() {
    while (true) {
        umi::toggle_led(LED_STATUS);
        co_await umi::sleep(500ms);
    }
}
```

### RGB LED

```cpp
// 単一RGB LED
umi::set_rgb(LED_MAIN, 255, 0, 0);     // 赤
umi::set_rgb(LED_MAIN, 0, 255, 0);     // 緑
umi::set_rgb(LED_MAIN, 0, 0, 255);     // 青

// HSV指定
umi::set_hsv(LED_MAIN, 120, 255, 128); // 緑、彩度MAX、明るさ50%

// WS2812ストリップ（複数LED）
umi::RgbStrip strip(NUM_LEDS);
strip.set(0, {255, 0, 0});
strip.set(1, {0, 255, 0});
strip.fill({0, 0, 255});  // 全LED同色
strip.show();             // DMA転送開始

// エフェクト
umi::Task<void> rainbow_task(umi::RgbStrip& strip) {
    uint8_t hue = 0;
    while (true) {
        for (int i = 0; i < strip.size(); ++i) {
            strip.set_hsv(i, (hue + i * 10) % 256, 255, 128);
        }
        strip.show();
        hue += 2;
        co_await umi::sleep(20ms);
    }
}
```

### 7セグメント/数値表示

```cpp
// 数値表示
umi::display_number(channel, 440);     // "440"
umi::display_number(channel, 3.14f);   // "3.14"
umi::display_number(channel, -12);     // "-12"

// 文字表示（限定的）
umi::display_text(channel, "LOAD");    // "LoAd"

// フォーマット指定
umi::display_number(channel, 127, {
    .digits = 3,
    .leading_zeros = true,   // "127"
    .decimal_point = 0,      // なし
});
```

### OLED/LCDディスプレイ

```cpp
// ディスプレイコンテキスト取得
auto& disp = umi::get_display();

// 基本描画（バッファに描画）
disp.clear();
disp.fill(umi::Color::Black);

// テキスト
disp.set_font(umi::Font::Small);  // 6x8
disp.draw_text(0, 0, "UMI Synth");
disp.set_font(umi::Font::Large);  // 12x16
disp.draw_text(0, 16, "440 Hz");

// 図形
disp.draw_rect(10, 10, 50, 30, umi::Color::White);
disp.fill_rect(12, 12, 46, 26, umi::Color::Gray);
disp.draw_line(0, 0, 127, 63, umi::Color::White);
disp.draw_circle(64, 32, 20, umi::Color::White);

// ビットマップ
disp.draw_bitmap(0, 0, logo_data, 32, 32);

// バッファをハードウェアに転送
disp.flush();  // 非同期DMA転送
```

### LCDグラフィックス（カラー）

```cpp
auto& lcd = umi::get_lcd();  // RGB565

// 色指定
constexpr auto RED   = umi::rgb565(255, 0, 0);
constexpr auto GREEN = umi::rgb565(0, 255, 0);
constexpr auto BLUE  = umi::rgb565(0, 0, 255);

// 描画
lcd.fill(umi::Color::Black);
lcd.fill_rect(10, 10, 100, 50, RED);
lcd.draw_text(20, 20, "Hello", umi::Font::Large, GREEN);

// 波形表示
void draw_waveform(std::span<const float> samples) {
    auto& lcd = umi::get_lcd();
    int w = lcd.width();
    int h = lcd.height();
    int mid = h / 2;
    
    lcd.fill_rect(0, 0, w, h, umi::Color::Black);
    
    for (int x = 0; x < w && x < samples.size(); ++x) {
        int y = mid - static_cast<int>(samples[x] * mid);
        lcd.draw_pixel(x, y, GREEN);
    }
    lcd.flush();
}

// 部分更新（高速化）
lcd.set_window(0, 0, 100, 50);  // 更新領域を限定
lcd.fill(RED);
lcd.flush_window();
```

### ディスプレイ更新の最適化

```cpp
// ダブルバッファリング
auto& disp = umi::get_display();
disp.enable_double_buffer(true);

// フレームレート制御
umi::Task<void> display_task(Synth& synth) {
    while (true) {
        // 30fps
        co_await umi::sleep(33ms);
        
        auto& disp = umi::get_display();
        disp.clear();
        draw_ui(synth, disp);
        disp.swap();  // バッファ切り替え + DMA転送
    }
}

// ダーティ領域のみ更新
disp.mark_dirty(0, 0, 64, 16);   // 変更領域をマーク
disp.flush_dirty();              // マーク領域のみ転送
```

### PWM出力

```cpp
// 汎用PWM（0.0-1.0）
umi::set_pwm(PWM_CH0, 0.5f);  // 50%デューティ

// サーボモーター（角度指定）
umi::set_servo(SERVO_CH0, 90.0f);  // 90度

// モーター速度（-1.0 to 1.0）
umi::set_motor(MOTOR_CH0, 0.75f);   // 正転75%
umi::set_motor(MOTOR_CH0, -0.5f);   // 逆転50%
```

### process() 内でのハードウェアアクセス

**禁止**: process() 内でsyscallは使用できません。

代わりにProcessContextに渡されるデータを使用:

```cpp
void MyProcessor::process(umi::ProcessContext& ctx) {
    // ✅ OK: ctx経由でオーディオ/MIDIにアクセス
    auto* out = ctx.output(0);
    for (const auto& ev : ctx.events()) { ... }
    
    // ❌ NG: syscallは使えない
    // auto hw = umi::get_hw_state();  // 禁止！
    // umi::set_led(0, true);          // 禁止！
    
    // ✅ OK: パラメータはアトミックに読み取り可
    float cutoff = ctx.param(PARAM_CUTOFF);
    
    // ✅ OK: Control Taskにイベント送信（メーター等）
    if (frame_count % 1024 == 0) {
        ctx.send_to_control(umi::Event::meter(0, peak_level));
    }
}
```

### プラットフォーム対応

| API | 組込み | Web (WASM) | Desktop |
|-----|--------|------------|---------|
| `wait_event()` | syscall | Asyncify | std::thread |
| `get_hw_state()` | 共有メモリ | JS経由 | MIDI/OSC |
| `set_led()` | GPIO | CSS/Canvas | なし |
| `set_rgb()` | PWM/WS2812 | CSS | なし |
| `get_display()` | SPI/I2C | Canvas | Window |
| `ctx.output()` | DMAバッファ | AudioWorklet | PortAudio |

---

## 関連ドキュメント

- [ARCHITECTURE.md](ARCHITECTURE.md) - アーキテクチャ概要
- [UMIP_SPEC.md](UMIP_SPEC.md) - Processor仕様
- [UMIC_SPEC.md](UMIC_SPEC.md) - Controller仕様
- [UMIM_SPEC.md](UMIM_SPEC.md) - バイナリ形式
- [SECURITY_ANALYSIS.md](SECURITY_ANALYSIS.md) - セキュリティとメモリ保護
