# イベント・状態システム アプリケーションガイド

アプリケーション開発者向けの、イベントルーティング・パラメータ管理・ハードウェア入力・SysEx の使い方ガイド。

内部設計の詳細は [docs/umidi/](../umidi/) を参照。

---

## 前提知識

### 三層アーキテクチャ

| 層 | 担当 | アプリからの操作 |
|----|------|-----------------|
| **Audio** | `process()` で音を出す | 読み出しのみ（events, params, input） |
| **Control** | `control()` / `main()` で UI・状態管理 | syscall で設定変更を要求 |
| **System** | OS が仲裁・書き込み | アプリからは不可視 |

アプリが共有状態を直接書き換えることはない。
全ての変更は syscall → System 経由で確定する。

---

## 1. RouteTable — メッセージの振り分け設定

### 概要

「どの MIDI メッセージをどこに送るか」をアプリが宣言するテーブル。
System はこのテーブルを機械的に参照するだけで、メッセージの意味を解釈しない。

### 経路フラグ

```cpp
enum RouteFlags : uint8_t {
    ROUTE_NONE    = 0,
    ROUTE_AUDIO   = 1 << 0,   // process() の events に届く
    ROUTE_CONTROL = 1 << 1,   // control() の events に届く
    ROUTE_STREAM  = 1 << 2,   // Stream（全メッセージ記録）
    ROUTE_PARAM   = 1 << 3,   // ParamMapping 経由で共有パラメータに自動反映
};
```

複数フラグの同時指定が可能（例: `ROUTE_AUDIO | ROUTE_STREAM`）。

### 基本的な使い方

```cpp
#include <umi/app.hh>

void init() {
    RouteTable rt = umi::default_route_table();

    // CC#1 (Mod Wheel) と CC#74 (Filter) を ParamMapping 経由で自動反映
    rt.cc_override[1]  = ROUTE_PARAM;
    rt.cc_override[74] = ROUTE_PARAM;

    umi::set_route_table(rt);
}
```

### デフォルト RouteTable

何も設定しなくても以下のデフォルトが適用される:

| メッセージ | デフォルト経路 |
|-----------|--------------|
| NoteOn/Off | `ROUTE_AUDIO` |
| Pitch Bend / Aftertouch | `ROUTE_AUDIO` |
| CC | `ROUTE_CONTROL` |
| Program Change | `ROUTE_CONTROL` |
| Clock/Transport | `ROUTE_AUDIO` |

### ユースケース別設定例

**シンセアプリ（典型）:**

```cpp
void init() {
    RouteTable rt = umi::default_route_table();

    // フィルター系 CC を ParamMapping 経由で Audio に自動反映
    rt.cc_override[1]  = ROUTE_PARAM;   // Mod Wheel → パラメータ
    rt.cc_override[74] = ROUTE_PARAM;   // Filter Cutoff → パラメータ

    // Sustain ペダルは Audio で直接処理
    rt.cc_override[64] = ROUTE_AUDIO;

    umi::set_route_table(rt);
}
```

**MIDI ルーターアプリ:**

```cpp
void init() {
    RouteTable rt{};  // 全て ROUTE_NONE で初期化

    // 全メッセージを Controller で受け取る
    for (int cmd = 0; cmd < 8; ++cmd)
        for (int ch = 0; ch < 16; ++ch)
            rt.channel_voice[cmd][ch] = ROUTE_CONTROL;
    for (int i = 0; i < 16; ++i)
        rt.system[i] = ROUTE_CONTROL;

    umi::set_route_table(rt);
}
```

---

## 2. ParamMapping — CC → パラメータ自動変換

### 概要

RouteTable で `ROUTE_PARAM` に指定された CC を、どのパラメータにどの範囲で変換するかを定義するテーブル。
System が自動的に変換して SharedParamState に書き込む。

### 基本的な使い方

```cpp
void init() {
    ParamMapping pm{};

    // CC#74 → PARAM_CUTOFF (0.0〜1.0)
    pm.cc[74] = { .param_id = PARAM_CUTOFF, .min = 0.0f, .max = 1.0f };

    // CC#1 → PARAM_MOD_DEPTH (0.0〜0.5)
    pm.cc[1] = { .param_id = PARAM_MOD_DEPTH, .min = 0.0f, .max = 0.5f };

    umi::set_param_mapping(pm);
}
```

### Processor 側での読み取り

ParamMapping で設定されたパラメータは `ctx.params` から読むだけ:

```cpp
void process(umi::AudioContext& ctx) {
    float cutoff = ctx.params.get(PARAM_CUTOFF);      // 0.0〜1.0
    float mod = ctx.params.get(PARAM_MOD_DEPTH);       // 0.0〜0.5

    // Processor は CC 番号を知る必要がない
    for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
        filter.set_cutoff(cutoff + mod * lfo.tick());
        out[i] = filter.tick(in[i]);
    }
}
```

---

## 3. MIDI Learn

RouteTable と ParamMapping を動的に変更することで実現する。

```cpp
// Controller 側（main() または control()）
bool learn_mode = false;
uint8_t learn_target_param = 0;

void control(umi::ControlContext& ctx) {
    ControlEvent e;
    while (ctx.events->pop(e)) {
        if (e.type == ControlEventType::INPUT_CHANGE) {
            // ボタンで MIDI Learn 開始
            if (e.input.id == BTN_LEARN && ctx.input->is_on(BTN_LEARN)) {
                learn_mode = true;
            }
        }

        if (e.type == ControlEventType::MIDI && e.midi.is_cc()) {
            if (learn_mode) {
                // 学習: この CC を現在選択中のパラメータにマッピング
                uint8_t cc = e.midi.cc_number();

                // RouteTable: この CC を ROUTE_PARAM に変更
                RouteTable rt = current_route_table;
                rt.cc_override[cc] = ROUTE_PARAM;
                umi::set_route_table(rt);

                // ParamMapping: CC → パラメータの紐付け
                ParamMapping pm = current_param_mapping;
                pm.cc[cc] = {
                    .param_id = learn_target_param,
                    .min = 0.0f,
                    .max = 1.0f,
                };
                umi::set_param_mapping(pm);

                learn_mode = false;
            }
        }
    }
}
```

---

## 4. process() でのイベント処理

### サンプル精度イベント処理

`ctx.events` には RouteTable で `ROUTE_AUDIO` に指定されたメッセージが
sample_pos 順にソートされて届く。

```cpp
void process(umi::AudioContext& ctx) {
    float* out = ctx.output(0);
    if (!out) return;

    size_t event_idx = 0;
    for (size_t s = 0; s < ctx.buffer_size; ++s) {
        // このサンプル位置までのイベントを処理
        while (event_idx < ctx.events.size() &&
               ctx.events[event_idx].sample_pos <= s) {
            const auto& e = ctx.events[event_idx++];
            if (e.midi.is_note_on()) {
                voices.note_on(e.midi.note(), e.midi.velocity());
            } else if (e.midi.is_note_off()) {
                voices.note_off(e.midi.note());
            }
        }

        out[s] = voices.render();
    }
}
```

### 共有状態の読み取り

ブロック単位精度で十分な連続値は SharedParamState / SharedInputState から直接読む:

```cpp
void process(umi::AudioContext& ctx) {
    // ParamMapping 経由で自動反映された値
    float cutoff = ctx.params.get(PARAM_CUTOFF);

    // Pitch Bend（System が自動更新）
    float pb = ctx.channel.pitch_bend_f(0);

    // ハードウェア入力（ボタン/ノブ — System が自動更新）
    bool bypass = ctx.input.is_on(BTN_BYPASS);
    float knob = ctx.input.get(KNOB_MIX);  // 0.0〜1.0

    for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
        if (bypass) {
            out[i] = in[i];
        } else {
            out[i] = effect.tick(in[i], cutoff, knob);
        }
    }
}
```

Processor が知る必要がないこと:
- 入力が MIDI か ノブか CV か
- CC の番号
- RouteTable / ParamMapping の内容
- sample_offset の算出方法

---

## 5. control() でのイベント処理

Controller には `ROUTE_CONTROL` に指定されたメッセージが ControlEvent として届く。

### ControlEvent の型

```cpp
enum class ControlEventType : uint8_t {
    MIDI,           // MIDI メッセージ（CC, Program Change 等）
    INPUT_CHANGE,   // ハードウェア入力変化（ボタン/ノブ/エンコーダ/CV）
    MODE_CHANGE,    // システムモード変更
};
```

### 基本的な処理

```cpp
void control(umi::ControlContext& ctx) {
    ControlEvent e;
    while (ctx.events->pop(e)) {
        switch (e.type) {
        case ControlEventType::INPUT_CHANGE:
            handle_input(e.input.id, e.input.value);
            break;

        case ControlEventType::MIDI:
            if (e.midi.command() == MidiData::PROGRAM_CHANGE) {
                load_preset(e.midi.bytes[1]);
            }
            break;

        case ControlEventType::MODE_CHANGE:
            update_display_mode(e.mode.mode);
            break;
        }
    }
}
```

### ハードウェア入力の扱い

全てのハードウェア入力（ボタン、ノブ、エンコーダ、CV）は
uint16_t (0x0000〜0xFFFF) に正規化されて届く。

```cpp
void handle_input(uint8_t id, uint16_t value) {
    switch (id) {
    case BTN_MODE:
        // エッジ検出（立ち上がり）
        if (prev_values[id] <= 0x7FFF && value > 0x7FFF) {
            cycle_mode();
        }
        break;

    case KNOB_VOLUME:
        // ノブ: float 変換して UI 更新
        display_value("Volume", float(value) / 65535.0f);
        break;

    case ENC_SELECT:
        // エンコーダ: 前回との差分で方向判定
        int16_t delta = int16_t(value - prev_values[id]);
        if (delta > 0) select_next();
        else if (delta < 0) select_prev();
        break;
    }
    prev_values[id] = value;
}
```

---

## 6. ParamSetRequest — Controller からのパラメータ変更

Controller が Audio に効くパラメータを変更するには syscall で Request を送る。
直接 SharedParamState を書き換えることはできない。

```cpp
// UI ノブを回した → パラメータ変更要求
umi::syscall::send_param_request({
    .param_id = PARAM_CUTOFF,
    .value = 0.75f,
    .source = ParamSource::UI,
    .option = ParamOption::RAMP,     // 補間希望
});
```

### ParamOption

| オプション | 動作 |
|-----------|------|
| `IMMEDIATE` | 次のブロック境界で即時反映 |
| `RAMP` | 補間希望（Processor 側で実装） |
| `HOLD` | 現在値を維持（リリース時など） |

### ParamSource

```cpp
enum class ParamSource : uint8_t {
    MIDI_CC,        // System が自動生成（ParamMapping 経由）
    KNOB,           // 物理ノブ
    CV,             // CV 入力
    UI,             // Controller（UI 操作）
    PRESET,         // プリセットロード
    AUTOMATION,     // DAW オートメーション
};
```

---

## 7. ハードウェア入力の購読モード

デフォルトでは全入力は `CONTROL_ONLY`（SharedInputState + ControlEvent）。
Audio レートの連続値が必要な場合は `AUDIO_STREAM` に切り替える。

```cpp
void init() {
    // CV 入力を Audio レートで読みたい場合
    umi::syscall::configure_input({
        .input_id = CV_PITCH,
        .mode = InputMode::AUDIO_STREAM,
        .sample_rate = 0,          // 0 = Audio レートと同じ
        .smoothing = 0,
        .threshold = 655,          // ~1% 変化で INPUT_CHANGE 発火
    });

    // ADC ノイズが多い入力にスムージング
    umi::syscall::configure_input({
        .input_id = KNOB_CUTOFF,
        .mode = InputMode::CONTROL_ONLY,
        .smoothing = 1000,         // 平滑化強度
        .threshold = 655,
    });
}
```

---

## 8. SysEx の送受信

SysEx は通常の MIDI イベントとは別経路で処理される。
syscall で読み書きする。

### 受信

```cpp
void control(umi::ControlContext& ctx) {
    uint8_t buf[256];
    uint8_t source;
    int len = umi::syscall::read_sysex(buf, sizeof(buf), &source);

    if (len > 0) {
        // SysEx メッセージを処理
        handle_sysex(buf, len, source);
    }
}
```

### 送信

```cpp
void send_preset_dump(uint8_t preset_id) {
    uint8_t sysex[] = {
        0x7E,              // Universal SysEx
        0x7F,              // All devices
        // ... manufacturer / device specific data ...
    };
    umi::syscall::send_sysex(sysex, sizeof(sysex), DEST_USB);
}
```

詳細は [SYSEX_ROUTING.md](../umidi/SYSEX_ROUTING.md) を参照。

---

## 9. AppConfig — 設定の一元化

### 概要

RouteTable, ParamMapping, InputParamMapping, InputConfig を1つの `AppConfig` にまとめて定義・適用できる。

```cpp
struct AppConfig {
    RouteTable route_table;             // メッセージ振り分け
    ParamMapping param_mapping;         // CC → パラメータ変換
    InputParamMapping input_mapping;    // ハードウェア入力 → パラメータ変換
    InputConfig inputs[16];             // 入力モード設定
};
```

### constexpr で定義する

```cpp
// app_config.hh
#include <umi/app.hh>

inline constexpr AppConfig SYNTH_CONFIG = [] {
    AppConfig cfg = umi::default_app_config();

    // CC#74 → Cutoff パラメータに自動変換
    cfg.route_table.cc_override[74] = ROUTE_PARAM;
    cfg.param_mapping.cc[74] = { PARAM_CUTOFF, {}, 0.0f, 1.0f };

    // CC#71 → Resonance
    cfg.route_table.cc_override[71] = ROUTE_PARAM;
    cfg.param_mapping.cc[71] = { PARAM_RESONANCE, {}, 0.0f, 1.0f };

    // ハードウェアノブ → パラメータ（CC を経由しない直接マッピング）
    cfg.input_mapping.entries[KNOB_CUTOFF] = { PARAM_CUTOFF, {}, 0.0f, 1.0f };
    cfg.input_mapping.entries[KNOB_RESO]   = { PARAM_RESONANCE, {}, 0.0f, 1.0f };

    // ノブのスムージング
    cfg.inputs[KNOB_CUTOFF] = { KNOB_CUTOFF, InputMode::CONTROL_ONLY, 0, 1000, 655 };

    return cfg;
}();
```

### 適用

```cpp
int main() {
    static Synth synth;
    umi::register_processor(synth);

    // 設定を一括適用（内部で RouteTable, ParamMapping, InputParamMapping, InputConfig を全てセット）
    umi::set_app_config(SYNTH_CONFIG);

    // ...
}
```

`umi::set_app_config()` は1回の syscall で全設定を OS に渡す。
コスト: ~2KB memcpy、~3.6μs @ 168MHz。Audio をブロックしない。

### RouteFlags の組み合わせ例

フラグは bitwise OR で複数指定できる:

```cpp
// CC#1 を Audio イベントとしても、ParamMapping 経由でも受け取る
cfg.route_table.cc_override[1] = ROUTE_AUDIO | ROUTE_PARAM;

// 全メッセージを Stream に記録しつつ、通常の経路も維持
cfg.route_table.cc_override[74] = ROUTE_PARAM | ROUTE_STREAM;

// NoteOn/Off を Audio と Control 両方に送る（Controller で MIDI モニタ表示等）
cfg.route_table.channel_voice[1][0] = ROUTE_AUDIO | ROUTE_CONTROL;  // NoteOn ch1
```

### InputParamMapping — ノブ/CV の直接マッピング

MIDI CC を経由せず、ハードウェア入力を直接パラメータに変換する:

```cpp
// ノブ3 → Cutoff (0.0〜1.0)
cfg.input_mapping.entries[3] = { PARAM_CUTOFF, {}, 0.0f, 1.0f };

// CV 入力 → Pitch (-2.0〜2.0 オクターブ)
cfg.input_mapping.entries[CV_PITCH] = { PARAM_PITCH, {}, -2.0f, 2.0f };
```

CC マッピングと入力マッピングが同じパラメータを指す場合、last-write-wins（最後に触れたソースが反映される）。

### YAML からの設定生成（オプション）

`xmake umi-config app_config.yaml` で constexpr ヘッダを自動生成できる:

```yaml
# app_config.yaml
defaults: synth

params:
  cutoff:    { id: 0, range: [0.0, 1.0] }
  resonance: { id: 1, range: [0.0, 1.0] }
  attack:    { id: 3, range: [0.001, 2.0] }

inputs:
  knob_cutoff: { id: 3, type: knob, smoothing: 1000 }
  knob_reso:   { id: 4, type: knob, smoothing: 1000 }

routes:
  cc:
    74: { param: cutoff }
    71: { param: resonance }
  input:
    knob_cutoff: { param: cutoff }
    knob_reso:   { param: resonance }
```

YAML は便利だが必須ではない。constexpr ヘッダを直接書いても構わない。

### レイヤー切り替え

1つのボタンやノブに複数の機能を割り当て、レイヤーで切り替える:

```cpp
// レイヤーごとに constexpr AppConfig を定義
inline constexpr AppConfig PLAY_CONFIG = [] {
    AppConfig cfg = umi::default_app_config();
    cfg.input_mapping.entries[KNOB_1] = { PARAM_CUTOFF, {}, 0.0f, 1.0f };
    cfg.input_mapping.entries[KNOB_2] = { PARAM_RESONANCE, {}, 0.0f, 1.0f };
    return cfg;
}();

inline constexpr AppConfig EDIT_CONFIG = [] {
    AppConfig cfg = umi::default_app_config();
    cfg.input_mapping.entries[KNOB_1] = { PARAM_ATTACK, {}, 0.001f, 2.0f };
    cfg.input_mapping.entries[KNOB_2] = { PARAM_RELEASE, {}, 0.01f, 5.0f };
    return cfg;
}();
```

```cpp
// Controller でレイヤー切り替え
void handle_input(uint8_t id, uint16_t value) {
    if (id == BTN_LAYER && prev[id] <= 0x7FFF && value > 0x7FFF) {
        current_layer = (current_layer == Layer::PLAY) ? Layer::EDIT : Layer::PLAY;

        switch (current_layer) {
        case Layer::PLAY: umi::set_app_config(PLAY_CONFIG); break;
        case Layer::EDIT: umi::set_app_config(EDIT_CONFIG); break;
        }
    }
    prev[id] = value;
}
```

切り替えはブロック境界で一括反映されるため、レイヤー間で設定が混在しない。

YAML でもレイヤー定義が可能:

```yaml
layers:
  play:
    input:
      knob_1: { param: cutoff }
      knob_2: { param: resonance }
  edit:
    input:
      knob_1: { param: attack }
      knob_2: { param: release, range: [0.01, 5.0] }
```

→ `PLAY_CONFIG`, `EDIT_CONFIG` が自動生成される。

---

## 10. 完全なアプリケーション例

### シンセサイザー（RouteTable + ParamMapping + MIDI Learn）

```cpp
// synth_full.cc
#include <umi/app.hh>

// パラメータ ID
enum : param_id_t {
    PARAM_CUTOFF = 0,
    PARAM_RESONANCE = 1,
    PARAM_MOD_DEPTH = 2,
    PARAM_ATTACK = 3,
    PARAM_RELEASE = 4,
};

// ハードウェア入力 ID
enum : uint8_t {
    BTN_LEARN = 0,
    BTN_PRESET_PREV = 1,
    BTN_PRESET_NEXT = 2,
    KNOB_MAIN = 3,
};

// --- Processor ---

struct Synth {
    void process(umi::AudioContext& ctx) {
        float* out = ctx.output(0);
        if (!out) return;

        float cutoff = ctx.params.get(PARAM_CUTOFF);
        float reso = ctx.params.get(PARAM_RESONANCE);
        float pb = ctx.channel.pitch_bend_f(0);

        size_t ei = 0;
        for (size_t s = 0; s < ctx.buffer_size; ++s) {
            while (ei < ctx.events.size() && ctx.events[ei].sample_pos <= s) {
                const auto& e = ctx.events[ei++];
                if (e.midi.is_note_on()) {
                    voice.note_on(e.midi.note(), e.midi.velocity());
                } else if (e.midi.is_note_off()) {
                    voice.note_off(e.midi.note());
                }
            }
            filter.set(cutoff, reso);
            out[s] = filter.tick(voice.render(pb));
        }
    }

private:
    VoiceManager voice;
    Filter filter;
};

// --- Controller ---

struct Controller {
    RouteTable route_table;
    ParamMapping param_mapping;
    bool learn_mode = false;
    uint8_t selected_param = PARAM_CUTOFF;
    uint16_t prev_input[16] = {};

    void init() {
        // RouteTable 初期設定
        route_table = umi::default_route_table();
        route_table.cc_override[74] = ROUTE_PARAM;   // CC#74 → Cutoff
        route_table.cc_override[71] = ROUTE_PARAM;   // CC#71 → Resonance
        umi::set_route_table(route_table);

        // ParamMapping 初期設定
        param_mapping.cc[74] = { PARAM_CUTOFF, {}, 0.0f, 1.0f };
        param_mapping.cc[71] = { PARAM_RESONANCE, {}, 0.0f, 1.0f };
        umi::set_param_mapping(param_mapping);
    }

    void control(umi::ControlContext& ctx) {
        ControlEvent e;
        while (ctx.events->pop(e)) {
            switch (e.type) {
            case ControlEventType::INPUT_CHANGE:
                handle_input(e.input.id, e.input.value);
                break;

            case ControlEventType::MIDI:
                if (e.midi.is_cc() && learn_mode) {
                    learn_cc(e.midi.cc_number());
                }
                break;

            default:
                break;
            }
        }

        // UI 表示更新
        display_param(selected_param, ctx.params->get(selected_param));
    }

private:
    void handle_input(uint8_t id, uint16_t value) {
        switch (id) {
        case BTN_LEARN:
            if (prev_input[id] <= 0x7FFF && value > 0x7FFF) {
                learn_mode = true;
            }
            break;

        case KNOB_MAIN:
            // ノブで選択中パラメータを変更
            umi::syscall::send_param_request({
                .param_id = selected_param,
                .value = float(value) / 65535.0f,
                .source = ParamSource::KNOB,
                .option = ParamOption::IMMEDIATE,
            });
            break;
        }
        prev_input[id] = value;
    }

    void learn_cc(uint8_t cc) {
        route_table.cc_override[cc] = ROUTE_PARAM;
        umi::set_route_table(route_table);

        param_mapping.cc[cc] = {
            .param_id = selected_param,
            .min = 0.0f,
            .max = 1.0f,
        };
        umi::set_param_mapping(param_mapping);

        learn_mode = false;
    }
};

// --- main ---

int main() {
    static Synth synth;
    static Controller ctrl;

    umi::register_processor(synth);
    ctrl.init();

    while (true) {
        auto ev = umi::wait_event();
        if (ev.type == umi::EventType::Shutdown) break;
        if (ev.type == umi::EventType::Control) {
            ctrl.control(ev.control_context);
        }
    }
    return 0;
}
```

---

## API 早見表

### Processor (process) で読めるもの

| データ | アクセス | 内容 |
|--------|---------|------|
| `ctx.events` | `std::span<const umidi::Event>` | NoteOn/Off, PitchBend 等（sample_pos 付き） |
| `ctx.params.get(id)` | `float` | ParamMapping/ParamSetRequest で確定した値 |
| `ctx.channel.pitch_bend_f(ch)` | `float` | Pitch Bend (-1.0〜1.0) |
| `ctx.channel.pressure_f(ch)` | `float` | Channel Pressure (0.0〜1.0) |
| `ctx.input.get(id)` | `float` | ハードウェア入力 (0.0〜1.0) |
| `ctx.input.is_on(id)` | `bool` | ボタン ON/OFF |
| `ctx.input.get_raw(id)` | `uint16_t` | ハードウェア入力 (0x0000〜0xFFFF) |

### Controller (control) で読めるもの

| データ | アクセス | 内容 |
|--------|---------|------|
| `ctx.events->pop(e)` | `ControlEvent` | CC, INPUT_CHANGE, MODE_CHANGE |
| `ctx.params->get(id)` | `float` | パラメータ現在値 |
| `ctx.input->get(id)` | `float` | ハードウェア入力 |

### Controller (control) から呼べる syscall

| syscall | 用途 |
|---------|------|
| `set_route_table(rt)` | メッセージ振り分け設定 |
| `set_param_mapping(pm)` | CC → パラメータ変換設定 |
| `send_param_request(req)` | パラメータ変更要求 |
| `configure_input(cfg)` | 入力モード設定 |
| `set_input_mapping(im)` | ハードウェア入力 → パラメータ変換設定 |
| `set_app_config(cfg)` | AppConfig 一括適用（RouteTable + ParamMapping + InputParamMapping + InputConfig） |
| `read_sysex(buf, len, &src)` | SysEx 受信 |
| `send_sysex(data, len, dest)` | SysEx 送信 |

---

## 禁止事項

### process() 内で禁止

- ヒープ確保 (`new`, `malloc`, `std::vector` の grow)
- ブロッキング (`mutex`, `semaphore`, syscall の wait 系)
- 例外 (`throw`)
- I/O (`printf`, LED/ディスプレイ操作)
- 共有状態への書き込み（読み出しのみ）

### control() 内で禁止

- SharedParamState への直接書き込み（必ず `send_param_request` 経由）
- Audio レートの処理

---

## 関連ドキュメント

- [APPLICATION.md](APPLICATION.md) — ユースケース実装ガイド
- [API_CONTEXT.md](API_CONTEXT.md) — Context API 設計
- [EVENT_SYSTEM_DESIGN.md](../umidi/EVENT_SYSTEM_DESIGN.md) — イベントシステム内部設計
- [SYSEX_ROUTING.md](../umidi/SYSEX_ROUTING.md) — SysEx / MIDI CI 経路設計
- [JITTER_COMPENSATION.md](../umidi/JITTER_COMPENSATION.md) — ジッター補正設計
