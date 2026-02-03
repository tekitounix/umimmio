# umios-architecture 実装計画

## 概要

`docs/umios-architecture/` の設計仕様と現在の `examples/stm32f4_kernel/` + `examples/synth_app/` 実装の差分を分析し、段階的に実装する計画。

---

## 差分サマリ

### 実装済み ✅
- 基本syscall: exit(0), yield(1), wait_event(2), get_time(3), get_shared(4), register_proc(5)
- 新syscall: set_app_config(20, Nr 21-24統合済み), send_param_request(25)
- Syscall呼出規約: r12=nr に統一 (app/syscall.hh + kernel svc_handler_impl)
- AudioContext: inputs/outputs/events/timing + params/channel/input_state ポインタ
- SharedMemory: SharedParamState (values[32]+changed_flags+version), SharedChannelState (16ch), SharedInputState (raw[16])
- EventRouter + RouteTable: RawInput → RouteTable → AudioEventQueue/ControlEventQueue/SharedParamState
- ParamMapping + 非正規化パイプライン: CC → normalize → denormalize → SharedParamState
- ControlEvent + ControlEventQueue: MIDI/INPUT_CHANGE/MODE_CHANGE/SYSEX_RECEIVED
- AppConfig: ParamMapping + InputParamMapping + InputConfig[16]
- AppLoader: .umia形式、AppHeader検証、MPU設定
- RTOS: 4タスク、PendSVコンテキストスイッチ、FPU lazy stacking
- MIDI: USB MIDI → EventRouter → RouteTable ルーティング (レガシーパス削除済み)
- umidi::Parser: MIDI 1.0 byte → UMP32 (StreamParser として使用)
- コルーチンランタイム: Task<T>, Scheduler
- synth_app: RouteTable/ParamMapping 設定、ctx.params 参照による新API使用

### 未実装 / 延期

| 項目 | ドキュメント | 状態 | 備考 |
|------|------------|------|------|
| ダブルバッファ管理 | 03-event-system | 延期 | set_app_config はスタブ実装 |
| SysExAssembler | 05-midi | 延期 | Shell経由で部分実装 |
| InputConfig (deadzone/smoothing/threshold) | 03-event-system | 延期 | 構造体定義済み、ロジック未実装 |
| UART MIDI対応 | 05-midi | 延期 | ハードウェア依存、EventRouter完成後に追加可能 |
| WASM/Plugin Backend更新 | — | 延期 | 組込み側安定後 |
| ReadSysex/SendSysex syscall | 05-midi | 延期 | ドキュメントで "Future" |

---

## フェーズ計画

### Phase 0: Syscall呼出規約の統一 (r12=nr)
**目的**: ドキュメント仕様 (r0-r3=args, r12=syscall_nr) に統一。現在2つの規約が共存している不整合を解消。

**現状分析:**
- `lib/umios/backend/cm/platform/syscall.hh` — **既にr12規約** (r12=nr, r0-r3=args)
- `lib/umios/app/syscall.hh` — **旧r0規約** (r0=nr, r1-r4=args)
- `examples/synth_app/` は `app/syscall.hh` を使用 → r0規約
- カーネルの `svc_handler_impl` は `sp[0]` (=r0) から番号を読む → r0規約前提
- Cortex-M4 SVC例外フレーム: `{r0, r1, r2, r3, r12, lr, pc, xpsr}` → **sp[4] = r12**

**変更ファイル:**
1. `lib/umios/app/syscall.hh` — `call()` をr12規約に変更
   - r0=nr → r12=nr, r0=a0, r1=a1, r2=a2, r3=a3
   - `get_time_usec()` も同様に修正
   - 理想的には `backend/cm/platform/syscall.hh` の関数を使うように統合
2. `examples/stm32f4_kernel/src/kernel.cc` — `svc_handler_impl()` を変更
   - `sp[0]` → `sp[4]` でsyscall番号取得 (r12の位置)
   - `sp[0]`〜`sp[3]` をarg0〜arg3として使用
3. `examples/stm32f4_kernel/src/arch.cc` — `yield()` のインラインasm修正
   - 現在: `mov r0, %0; svc 0` → 変更: `mov r12, %0; svc 0`
4. `lib/umios/app/umi_app.hh` — `register_processor()` 等のsyscall呼出を確認

**テスト:** `xmake test` + `xmake build stm32f4_kernel && xmake build synth_app` + フラッシュ動作確認

**注意:** OS/アプリ両方を同時に更新する必要あり (片方だけではsyscall不一致で動作不能)

---

### Phase 1: SharedMemory構造体の再構築
**目的**: ドキュメント準拠の共有メモリレイアウトへ移行

**変更ファイル:**
- `lib/umios/core/shared_state.hh` (新規作成)
- `lib/umios/kernel/loader.hh` (SharedMemory更新)
- `examples/stm32f4_kernel/src/kernel.cc` (params読み書き箇所修正)
- `examples/synth_app/src/main.cc` (SharedMemory参照修正)

**実装内容:**
```cpp
// lib/umios/core/shared_state.hh
struct SharedParamState {        // 164B
    float values[32];            // 128B: 非正規化済みパラメータ値
    uint32_t changed_flags;      // 4B: ビットマスク
    uint32_t version;            // 4B: ブロックごとのバージョン
    uint8_t _pad[28];
};

struct SharedChannelState {      // 64B
    struct Channel {
        uint8_t program;
        uint8_t pressure;
        int16_t pitch_bend;      // -8192 ~ 8191
    };
    Channel channels[16];
};

struct SharedInputState {        // 32B
    uint16_t raw[16];            // 0x0000–0xFFFF
};
```

SharedMemoryの `atomic<float> params[32]` を `SharedParamState param_state` に置換。`SharedChannelState`, `SharedInputState` を追加。

**テスト:** `xmake test` + フラッシュ動作確認

---

### Phase 2: AudioContext拡張
**依存:** Phase 1

**変更ファイル:**
- `lib/umios/core/audio_context.hh` (フィールド追加)
- `examples/stm32f4_kernel/src/kernel.cc` (AudioContext構築時にポインタ設定)

**実装内容:**
AudioContextに以下を追加:
```cpp
const SharedParamState* params = nullptr;
const SharedChannelState* channel = nullptr;
const SharedInputState* input = nullptr;
uint32_t output_event_count = 0;
```

既存コードはこれらを使わないのでデフォルトnullptrで後方互換。

**テスト:** `xmake test` + 既存synth_appの動作確認

---

### Phase 3: EventRouter基盤
**依存:** Phase 1, 2
**最大のフェーズ — サブフェーズに分割**

#### Phase 3a: データ構造定義
**新規作成:** `lib/umios/core/event_router.hh`

```cpp
struct RawInput {                // 12B
    uint32_t hw_timestamp;       // μs
    uint8_t source_id;           // USB=0, UART=1, GPIO=2
    uint8_t size;
    uint8_t payload[6];          // UMP32 or InputEvent
};

enum RouteFlags : uint8_t {
    ROUTE_NONE        = 0,
    ROUTE_AUDIO       = 1,
    ROUTE_CONTROL     = 2,
    ROUTE_STREAM      = 4,
    ROUTE_PARAM       = 8,
    ROUTE_CONTROL_RAW = 16,
};

struct RouteTable {              // 272B
    RouteFlags channel_voice[8][16];   // 128B (cmd×ch)
    RouteFlags control_change[128];    // 128B (CC#)
    RouteFlags system[16];             // 16B (0xF0-0xFF)
};
```

#### Phase 3b: EventRouterロジック実装
**変更:** `lib/umios/core/event_router.hh`

```cpp
class EventRouter {
    RouteTable* active_table_;
    // receive(RawInput) → RouteTable参照 → 各キューへ分配
    void receive(const RawInput& input);
};
```

AudioEventQueue (64 × Event SPSC), ControlEventQueue (32 × ControlEvent SPSC) へ分配。

#### Phase 3c: カーネル統合
**変更:** `examples/stm32f4_kernel/src/kernel.cc`
- USB MIDIコールバックを RawInput → EventRouter 経由に変更
- ボタンイベントも EventRouter 経由に
- 旧パスは `#ifdef LEGACY_MIDI_PATH` で一時的に残す

**新規テスト:** `tests/test_event_router.cc`
**テスト:** ユニットテスト + フラッシュしてMIDI動作確認

---

### Phase 4: パラメータマッピング + 非正規化パイプライン
**依存:** Phase 1, 3

**新規作成:** `lib/umios/core/param_mapping.hh`

```cpp
struct ParamMapEntry {           // 12B
    uint8_t param_id;
    uint8_t _pad[3];
    float range_low;
    float range_high;
};

struct ParamMapping {            // 1536B
    ParamMapEntry entries[128];  // CC# → param
};

struct InputParamMapping {       // 192B
    ParamMapEntry entries[16];   // hardware input → param
};

struct InputConfig {             // 8B
    uint8_t input_id;
    uint8_t mode;                // DISABLED/PARAM_ONLY/EVENT_ONLY/PARAM_AND_EVENT
    uint16_t deadzone;
    uint16_t smoothing;
    uint16_t threshold;
};

struct AppConfig {               // ~2128B
    RouteTable route_table;
    ParamMapping param_mapping;
    InputParamMapping input_mapping;
    InputConfig inputs[16];
};
```

EventRouterの `ROUTE_PARAM` パスで ParamMapping → denormalize → SharedParamState 書き込み。

**非正規化パイプライン処理フロー:**
```
CC#74 value=100 受信
  ↓ normalize: 100 / 127 = 0.787
  ↓ ParamMapEntry[74]: range_low=0.0, range_high=1.0
  ↓ mapped = 0.0 + 0.787 * (1.0 - 0.0) = 0.787
  ↓ ParamDescriptor[CUTOFF]: min=20, max=20000, curve=Log
  ↓ denormalize(0.787) = 20 * pow(20000/20, 0.787) = 4536 Hz
  ↓ SharedParamState.values[PARAM_CUTOFF] = 4536.0f
```

**EventRouter内の実装イメージ:**
```cpp
void EventRouter::apply_param_mapping(uint8_t cc_num, uint8_t cc_val) {
    auto& entry = active_mapping_->entries[cc_num];
    if (entry.param_id == PARAM_NONE) return;

    float normalized = static_cast<float>(cc_val) / 127.0f;
    float mapped = entry.range_low + normalized * (entry.range_high - entry.range_low);
    float value = param_descriptors_[entry.param_id].denormalize(mapped);

    shared_->param_state.values[entry.param_id] = value;
    shared_->param_state.changed_flags |= (1u << entry.param_id);
}
```

**テスト:** CC → param値変換のユニットテスト

---

### Phase 5: 新Syscall (20-25)
**依存:** Phase 4

**変更ファイル:**
- `lib/umios/app/syscall.hh` (nr::set_app_config=20 ~ nr::send_param_request=25 追加)
- `lib/umios/kernel/syscall/syscall_numbers.hh` (カーネル側番号追加)
- `examples/stm32f4_kernel/src/kernel.cc` (svc_handler_impl拡張)

**実装内容:**
| Nr | 名前 | 動作 |
|----|------|------|
| 20 | SetAppConfig | AppConfigをアプリメモリからコピー、ダブルバッファへ書込、ブロック境界でスワップ |
| 21 | SetRouteTable | RouteTableのみ更新 |
| 22 | SetParamMapping | ParamMappingのみ更新 |
| 23 | SetInputMapping | InputParamMappingのみ更新 |
| 24 | ConfigureInput | InputConfig更新 |
| 25 | SendParamRequest | ParamSetRequestキューへプッシュ |

**ダブルバッファ管理:** active/inactiveの2面。syscallはinactiveに書込、オーディオブロック境界でatomic swap。

**テスト:** syscall呼出 → 設定反映のテスト

---

### Phase 6: ControlEvent + Controller API
**依存:** Phase 3, 5

**新規作成:** `lib/umios/core/control_event.hh`

```cpp
enum class ControlEventType : uint8_t {
    MIDI, INPUT_CHANGE, MODE_CHANGE, SYSEX_RECEIVED
};

struct ControlEvent {            // 8B
    ControlEventType type;
    uint8_t source_id;
    uint16_t data0;
    uint32_t data1;
};
```

**変更:** `lib/umios/app/umi_app.hh` に Controller用API追加
**変更:** `examples/synth_app/src/main.cc` を新APIに移行

**移行後のアプリケーション例 (synth_app):**
```cpp
// --- パラメータ定義 ---
enum Param : uint8_t { CUTOFF = 0, RESONANCE = 1, ATTACK = 2, RELEASE = 3 };

constexpr ParamDescriptor params[] = {
    {CUTOFF,    "Cutoff",    1000.0f, 20.0f, 20000.0f, ParamCurve::Log},
    {RESONANCE, "Resonance", 0.5f,    0.0f,  1.0f,     ParamCurve::Linear},
    {ATTACK,    "Attack",    10.0f,   0.1f,  5000.0f,  ParamCurve::Log},
    {RELEASE,   "Release",   200.0f,  1.0f,  10000.0f, ParamCurve::Log},
};

// --- ルーティング設定 ---
constexpr AppConfig PLAY_CONFIG = {
    .route_table = {
        .channel_voice = { [NOTE_ON_IDX] = {[0 ... 15] = ROUTE_AUDIO} },
        .control_change = {
            [74] = ROUTE_PARAM,          // CC#74 → Cutoff (ParamMapping経由)
            [71] = ROUTE_PARAM,          // CC#71 → Resonance
            [1]  = ROUTE_CONTROL,        // Mod wheel → Controller通知
        },
    },
    .param_mapping = {
        .entries = {
            [74] = {CUTOFF,    0.0f, 1.0f},   // CC#74 → Cutoff全範囲
            [71] = {RESONANCE, 0.0f, 1.0f},   // CC#71 → Resonance全範囲
        },
    },
};

// --- Processor (オーディオスレッド) ---
class SynthProcessor {
    void process(AudioContext& ctx) {
        // SharedParamStateから直接読むだけ (EventRouterが非正規化済み)
        float cutoff = ctx.params->values[CUTOFF];
        float reso   = ctx.params->values[RESONANCE];
        synth.set_filter(cutoff, reso);

        // MIDI note on/offはinput_eventsから (ROUTE_AUDIO経由)
        for (auto& ev : ctx.input_events) {
            if (ev.type == EventType::Midi)
                synth.handle_midi(ev.midi.bytes, ev.midi.size);
        }
        for (uint32_t i = 0; i < ctx.buffer_size; ++i)
            ctx.output(0)[i] = synth.process_sample();
    }
};

// --- Controller (コントロールスレッド) ---
int main() {
    SynthProcessor proc;
    umi::register_processor(proc);
    umi::set_app_config(PLAY_CONFIG);   // syscall 20: 全設定を一括適用

    while (true) {
        auto bits = umi::wait_event(event::Control | event::Timer);
        if (bits & event::Control) {
            ControlEvent ev;
            while (pop_control_event(ev)) {
                if (ev.type == ControlEventType::INPUT_CHANGE) {
                    // Mod wheel等、ROUTE_CONTROLで届いたCC
                }
            }
        }
    }
}
```

**テスト:** synth_appでControlEvent受信確認

---

### Phase 7: MIDI拡張 (UMP32 + StreamParser)
**依存:** Phase 3

**変更:** `lib/umidi/` 内

- UMP32: 型付き4バイトラッパー (既存umidiの拡張)
- StreamParser: MIDI 1.0 byte → UMP32 (200B code, 5B state)
- SysExAssembler: 256Bバッファ/トランスポート

**テスト:** StreamParser byte→UMP変換のユニットテスト

---

## 延期事項

### UART MIDI対応
**理由:** ハードウェア依存が大きく、EventRouter完成後に段階的に追加可能。

### WASM/Plugin Backend更新
**理由:** 組込み側の安定後にバックエンド側を更新。

### ReadSysex/SendSysex syscall (32-33)
**理由:** ドキュメントで "Future" と記載。

---

## 実装順序と依存関係

```
Phase 0 (Syscall r12統一)
          │
Phase 1 (SharedMemory) ──→ Phase 2 (AudioContext)
          │
          └──→ Phase 3 (EventRouter) ──→ Phase 4 (ParamMapping)
                       │                        │
                       │                        └──→ Phase 5 (Syscalls)
                       │                                    │
                       └──→ Phase 7 (MIDI)      Phase 6 (Controller)
```

## 検証方法 (CLAUDE.md準拠: build → flash → debugger verification)

各フェーズ完了時、以下を**すべて**実施:
1. `xmake test` — ホストユニットテスト全パス
2. `xmake build stm32f4_kernel && xmake build synth_app` — ビルド成功
3. `xmake flash-kernel && xmake flash-synth-app` — 実機フラッシュ
4. デバッガ (pyOCD/GDB) で以下を確認:
   - カーネル起動完了 (RTOS started)
   - アプリロード成功 (AppState::Running)
   - syscall動作確認 (Phase 0ではwait_event/get_time等)
   - オーディオ出力 (DMAカウンタ増加、process()呼出)
5. MIDI入力テスト (Phase 3以降: USB MIDIノートオン → 発音確認)

**ビルド成功だけでは完了としない。**

## リスク

| リスク | 対策 |
|--------|------|
| Phase 3でMIDIパスを壊す | レガシーパスを`#ifdef`で残し、切替可能に |
| SharedMemoryサイズ変更でリンカスクリプトが合わない | Phase 1でリンカスクリプトも確認・更新 |
| ダブルバッファのRAM不足 (~5KB追加) | カーネルRAM 48KBのうち使用量を事前計算 |
| AppConfigの2128BがSVCハンドラ内でmemcpy遅い | ~3.6μs@168MHz、許容範囲 |
