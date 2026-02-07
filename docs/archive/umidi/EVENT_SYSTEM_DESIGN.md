# イベント・状態システム設計

event_state.md の思想を具体化した設計。
入力ソース（MIDI, ボタン, ノブ, CV）に依存しない、データの性質に基づく経路設計。

---

## 三層の世界

| 層 | 役割 | 時間制約 | 書き込み権限 |
|----|------|----------|------------|
| **Audio** | 締め切り世界。音を出す | サンプル精度、遅延不可 | 読み出しのみ |
| **Control** | 意味世界。UIや状態遷移 | 数ms〜数十ms遅延を許容 | Request のみ発行 |
| **System** | 時間と仲裁の世界 | — | 唯一の書き込み権限 |

**核心原則: Audio に効く変更は必ず System を経由して確定する。**

Control や外部入力が直接 Audio の状態を書き換えることはない。
System が時刻を確定し、値を検証し、共有メモリに書き込む。
Audio と Control はそれを読むだけ。

---

## 意味付けの分離

System はメッセージの意味を知らない。意味を与えるのは Controller（アプリ）。

System は「このメッセージをどこに送るか」のルールを機械的に実行するだけ。
ルールは Controller が syscall で登録する（RouteTable）。

```
System が知ること:
  「これは UMP32 で command=0xB0（CC）、cc_number=74」
  「RouteTable を引くと ROUTE_CONTROL」
  → ControlEventQueue に送る

System が知らないこと:
  「CC#74 は filter cutoff である」
  「このアプリではノブ1 が CC#74 にマッピングされている」
```

NoteOn/Off のような MIDI 仕様上意味が確定しているメッセージであっても、
MIDI ルーターアプリでは「転送対象」でしかない。
振り分け先は常にアプリが決める。

---

## 全体構成

```
┌─────────────────────────────────────────────────────┐
│ 入力ソース                                            │
│                                                       │
│   USB MIDI    UART MIDI    ボタン    ノブ/CV    BLE   │
│   (ISR/DMA)  (DMA/poll)  (GPIO)   (ADC/DMA)  (将来)  │
│       │          │          │         │          │    │
│       ▼          ▼          ▼         ▼          ▼    │
│   ┌───────────────────────────────────────────────┐  │
│   │ 正規化 (入力ソースごと)                         │  │
│   │  MIDI: Parser → UMP32                          │  │
│   │  ボタン: debounce → uint16_t (0/0xFFFF)        │  │
│   │  ノブ/CV: ADC → uint16_t (0x0000〜0xFFFF)      │  │
│   └───────────────────────────────────────────────┘  │
│       │                                               │
│       ▼                                               │
│   RawInputQueue (正規化済み、意味付け前、hw_timestamp 付き) │
└─────────────────────────────────────────────────────┘
         │
         ▼
### RawInput — RawInputQueue のエントリ

```cpp
struct RawInput {
    uint32_t hw_timestamp;   // ISR 時点のタイマー値 (DWT->CYCCNT)
    uint8_t source_id;       // 入力ソース (USB=0, UART=1, GPIO=2, ...)
    uint8_t size;            // payload サイズ
    uint8_t payload[6];      // UMP32(4B) or InputEvent(4B) 等
};
// sizeof = 12B
```

hw_timestamp から sample_offset への変換は System Service が行う。
詳細は [JITTER_COMPENSATION.md](JITTER_COMPENSATION.md) を参照。

```
┌─────────────────────────────────────────────────────┐
│ System Service (OS側、特権モード)                      │
│                                                       │
│   1. タイムスタンプ確定 (sample_offset)                 │
│   2. RouteTable 参照 → 経路決定 (意味を知らない)        │
│   3. ParamMapping 参照 → 値変換 (テーブル引くだけ)      │
│   4. ParamSetRequest 処理 → 共有メモリ書き込み          │
│                                                       │
│       ┌──────────┬──────────┬──────────┐              │
│       ▼          ▼          ▼          ▼              │
│   AudioEvent  ParamSet  ControlEvent  Stream          │
│   Queue       (共有)    Queue         (共有)          │
└─────────────────────────────────────────────────────┘
         │          │          │          │
         ▼          ▼          ▼          ▼
┌─────────────────────────────────────────────────────┐
│ Application (アプリ側、非特権モード)                    │
│                                                       │
│   Processor::process()                                │
│     読む: AudioEventQueue, ParamSet, Stream           │
│     書く: 出力バッファのみ                              │
│                                                       │
│   Controller::control()                               │
│     読む: ControlEventQueue, ParamSet, Stream         │
│     書く: ParamSetRequest (syscall 経由)               │
│     設定: RouteTable, ParamMapping (syscall 経由)      │
└─────────────────────────────────────────────────────┘
```

---

## OS/アプリ バイナリ分離

OS とアプリは別々にコンパイルされる。アプリ開発者は OS コードに触れない。

### メモリマップ

```
Flash:
  0x08000000  Kernel (384KB)
  0x08060000  App image (128KB)   ← .umia バイナリ

SRAM:
  0x20000000  Kernel RAM (48KB)   ← MPU: OS のみ書き込み可
  0x2000C000  App RAM (48KB)      ← MPU: アプリ RW
  0x20018000  Shared (16KB)       ← MPU: 両者 RW

CCM:
  0x10000000  Kernel fast RAM (64KB)  ← DMA 不可、OS のみ
```

### アプリが OS に触れる手段

1. **共有メモリ** (.shared セクション、16KB) — 構造化されたデータ交換
2. **Syscall** (SVC 命令) — OS 機能の呼び出し

アプリは OS のメモリを直接読み書きできない（MPU 保護）。
RouteTable や ParamMapping のような設定は **syscall** で OS に渡す。

---

## RouteTable — アプリが振り分けルールを宣言する仕組み

### 設計

Controller（アプリ）が「どのメッセージをどこに送るか」を宣言するテーブル。
System はこのテーブルを機械的に参照するだけで、意味を解釈しない。

### 経路フラグ

```cpp
enum RouteFlags : uint8_t {
    ROUTE_NONE    = 0,
    ROUTE_AUDIO   = 1 << 0,   // AudioEventQueue（状態変更イベント）
    ROUTE_CONTROL = 1 << 1,   // ControlEventQueue
    ROUTE_STREAM  = 1 << 2,   // Stream（全メッセージ記録）
    ROUTE_PARAM   = 1 << 3,   // ParamMapping 経由で共有メモリに書き込み
};
```

複数フラグを同時に立てられる。例: `ROUTE_AUDIO | ROUTE_STREAM`

### テーブル構造

MIDI チャンネルメッセージ: command (8種) × channel (16) = 128 エントリ。
CC 番号ごとの細分化: 128 エントリ。
システムメッセージ: 16 エントリ。

```cpp
struct RouteTable {
    // チャンネルメッセージ振り分け
    // index: command_index (0x80→0, 0x90→1, ..., 0xE0→6, 0xF0→7)
    RouteFlags channel_voice[8][16];   // 128B

    // CC 番号ごとの経路上書き
    // ROUTE_NONE 以外なら channel_voice[3]（CC）を上書き
    RouteFlags cc_override[128];       // 128B

    // システムメッセージ (0xF0-0xFF)
    RouteFlags system[16];             //  16B
};
// 合計: 272B
```

### ルックアップ（System 側の処理）

```cpp
RouteFlags lookup_route(const umidi::UMP32& ump) const {
    if (ump.is_midi1_cv()) {
        uint8_t cmd_idx = (ump.command() >> 4) - 8;
        uint8_t ch = ump.channel();
        RouteFlags flags = table.channel_voice[cmd_idx][ch];

        // CC の場合、cc_override を確認
        if (cmd_idx == 3) {
            RouteFlags ovr = table.cc_override[ump.cc_number()];
            if (ovr != ROUTE_NONE) flags = ovr;
        }
        return flags;
    }
    if (ump.is_system()) {
        return table.system[ump.status() & 0x0F];
    }
    return ROUTE_NONE;
}
```

ARM Cortex-M4 で 5-8 命令。配列アクセスのみ、分岐は CC 判定の 1 回のみ。
272B は L1 キャッシュに収まる。

### デフォルト RouteTable

アプリが何も設定しなくても動くデフォルト:

| メッセージ | デフォルト経路 | 根拠 |
|-----------|--------------|------|
| NoteOn/Off (0x80/0x90) | `ROUTE_AUDIO` | 音の状態変更 |
| Poly Pressure (0xA0) | `ROUTE_AUDIO` | 音の状態変更 |
| CC (0xB0) | `ROUTE_CONTROL` | アプリ依存 |
| Program Change (0xC0) | `ROUTE_CONTROL` | プリセット選択 |
| Channel Pressure (0xD0) | `ROUTE_AUDIO` | 音の状態変更 |
| Pitch Bend (0xE0) | `ROUTE_AUDIO` | 音の状態変更 |
| Clock/Transport (0xF8-FC) | `ROUTE_AUDIO` | テンポ同期 |

### syscall による登録

```cpp
// アプリ側 API (lib/umi/app/umi_app.hh)

/// 振り分けテーブルを OS に登録
inline int set_route_table(const RouteTable& table) noexcept {
    return syscall::call(nr::set_route_table,
                         reinterpret_cast<uint32_t>(&table),
                         sizeof(RouteTable));
}
```

OS 側の syscall handler はダブルバッファの inactive 側に直接書き込む（詳細は下記ダブルバッファ管理を参照）。

OS 側はダブルバッファで管理:

```cpp
RouteTable route_tables[2];   // 544B (272B × 2)
uint8_t active_idx = 0;
bool route_table_pending = false;

// syscall: inactive バッファに書き込む
case nr::set_route_table: {
    const auto* src = reinterpret_cast<const RouteTable*>(arg0);
    memcpy(&route_tables[active_idx ^ 1], src, sizeof(RouteTable));
    route_table_pending = true;
    result = 0;
    break;
}

// Audio ブロック境界で切り替え
void on_audio_block_boundary() {
    if (route_table_pending) {
        active_idx ^= 1;
        route_table_pending = false;
    }
}

const RouteTable& active_route_table() const {
    return route_tables[active_idx];
}
```

- syscall 中の memcpy: 272B → Cortex-M4 で ~70 サイクル（無視できる）
- 切り替えはブロック境界のみ → ブロック途中で経路が変わらない
- 変更頻度: 初期化時、MIDI Learn 時、モード切替時のみ

### アプリからの使用例

**シンセアプリ（初期化時）:**

```cpp
void init() {
    RouteTable rt = default_route_table();

    // CC#1 (Mod) と CC#74 (Filter) を ParamSet 経由で Audio に
    rt.cc_override[1]  = ROUTE_PARAM;
    rt.cc_override[74] = ROUTE_PARAM;

    umi::app::set_route_table(rt);
}
```

**MIDI ルーターアプリ:**

```cpp
void init() {
    RouteTable rt{};  // 全て ROUTE_NONE で初期化

    // 全メッセージを Control に（Controller が転送先を決める）
    for (int cmd = 0; cmd < 8; ++cmd)
        for (int ch = 0; ch < 16; ++ch)
            rt.channel_voice[cmd][ch] = ROUTE_CONTROL;
    for (int i = 0; i < 16; ++i)
        rt.system[i] = ROUTE_CONTROL;

    umi::app::set_route_table(rt);
}
```

**MIDI Learn 開始/終了:**

```cpp
void on_learn_start() {
    RouteTable rt = current_route_table;
    // 全 CC を一時的に Control に（学習用）
    for (int cc = 0; cc < 128; ++cc)
        rt.cc_override[cc] = ROUTE_CONTROL;
    umi::app::set_route_table(rt);
}

void on_learn_complete(uint8_t learned_cc, param_id_t target) {
    RouteTable rt = current_route_table;
    // 学習した CC を ParamSet 経路に
    for (int cc = 0; cc < 128; ++cc)
        rt.cc_override[cc] = ROUTE_NONE;  // デフォルトに戻す
    rt.cc_override[learned_cc] = ROUTE_PARAM;
    umi::app::set_route_table(rt);

    // ParamMapping も更新
    ParamMapping pm = current_param_mapping;
    pm.cc[learned_cc] = { .param_id = target, .min = 0.0f, .max = 1.0f };
    umi::app::set_param_mapping(pm);
}
```

---

## ParamMapping — CC/ノブ → パラメータ値変換

RouteTable で `ROUTE_PARAM` に振り分けられた CC を、
SharedParamState のどのパラメータにどう変換するかのテーブル。

これも Controller が syscall で登録し、System が機械的に実行する。

```cpp
struct ParamMapEntry {
    param_id_t param_id;    // 対象パラメータ ID (INVALID = 未マッピング)
    uint8_t _pad[3];        // 4B アライメント
    float min;              // CC=0 の値
    float max;              // CC=127 の値
};
// sizeof = 12B (param_id 1B + pad 3B + float 4B + float 4B)

struct ParamMapping {
    static constexpr param_id_t INVALID = 0xFF;
    ParamMapEntry cc[128];  // CC 番号でインデクス
};
// 128 × 12B = 1536B
```

System の処理:

```cpp
void handle_cc_param(const umidi::UMP32& ump) {
    const auto& entry = active_param_mapping().cc[ump.cc_number()];
    if (entry.param_id == ParamMapping::INVALID) return;

    float normalized = float(ump.cc_value()) / 127.0f;
    float value = entry.min + normalized * (entry.max - entry.min);
    shared_param_state.values[entry.param_id] = value;
}
```

配列の直接インデクス — O(1)、分岐なし。

syscall:

```cpp
inline int set_param_mapping(const ParamMapping& mapping) noexcept {
    return syscall::call(nr::set_param_mapping,
                         reinterpret_cast<uint32_t>(&mapping),
                         sizeof(ParamMapping));
}
```

ParamMapping も RouteTable と同様にダブルバッファ + ブロック境界切り替え。

---

## InputParamMapping — ハードウェア入力 → パラメータ値変換

ParamMapping が CC 番号をキーとするのに対し、
InputParamMapping はハードウェア入力 ID（ノブ/CV/エンコーダ）をキーとする。

ノブや CV を MIDI CC を経由せず直接パラメータに変換する。

```cpp
struct InputParamMapping {
    ParamMapEntry entries[16];  // input_id でインデクス
};
// 16 × 12B = 192B
```

ParamMapEntry は ParamMapping と同じ型を再利用する。

System の処理:

```cpp
void handle_input_param(uint8_t input_id, uint16_t raw_value) {
    const auto& entry = active_input_mapping().entries[input_id];
    if (entry.param_id == ParamMapping::INVALID) return;

    float normalized = float(raw_value) / 65535.0f;
    float value = entry.min + normalized * (entry.max - entry.min);
    shared_param_state.values[entry.param_id] = value;
}
```

配列の直接インデクス — O(1)、分岐なし。

syscall:

```cpp
inline int set_input_mapping(const InputParamMapping& mapping) noexcept {
    return syscall::call(nr::set_input_mapping,
                         reinterpret_cast<uint32_t>(&mapping),
                         sizeof(InputParamMapping));
}
```

InputParamMapping も RouteTable / ParamMapping と同様にダブルバッファ + ブロック境界切り替え。

---

## AppConfig — 設定の一元化

RouteTable + ParamMapping + InputParamMapping + InputConfig を統合する構造体。
アプリ開発者は AppConfig ひとつで全設定を定義できる。

```cpp
struct AppConfig {
    RouteTable route_table;             // 272B
    ParamMapping param_mapping;         // 1536B
    InputParamMapping input_mapping;    // 192B
    InputConfig inputs[16];             // 128B (8B × 16)
};
// 合計: ~2128B
```

### constexpr 定義

```cpp
// app_config.hh — ビルド時に確定する設定
#include <umi/app.hh>

inline constexpr AppConfig PLAY_CONFIG = [] {
    AppConfig cfg = umi::default_app_config();

    // MIDI CC → パラメータ
    cfg.route_table.cc_override[74] = ROUTE_PARAM;
    cfg.param_mapping.cc[74] = { PARAM_CUTOFF, {}, 0.0f, 1.0f };

    cfg.route_table.cc_override[71] = ROUTE_PARAM;
    cfg.param_mapping.cc[71] = { PARAM_RESONANCE, {}, 0.0f, 1.0f };

    // ハードウェア入力 → パラメータ
    cfg.input_mapping.entries[KNOB_CUTOFF] = { PARAM_CUTOFF, {}, 0.0f, 1.0f };
    cfg.input_mapping.entries[KNOB_RESO]   = { PARAM_RESONANCE, {}, 0.0f, 1.0f };

    // ノブのスムージング設定
    cfg.inputs[KNOB_CUTOFF] = { KNOB_CUTOFF, InputMode::CONTROL_ONLY, 0, 1000, 655 };

    return cfg;
}();
```

### YAML からの生成（オプション）

ビルドシステム（xmake）のスクリプトで YAML → constexpr ヘッダを生成できる:

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

layers:
  play:
    input:
      knob_cutoff: { param: cutoff }
      knob_reso:   { param: resonance }
  edit:
    input:
      knob_cutoff: { param: attack }
      knob_reso:   { param: resonance, range: [0.1, 0.99] }
```

生成コマンド: `xmake umi-config app_config.yaml`
出力: `app_config.hh`（constexpr AppConfig の配列）

YAML は便利だが必須ではない。constexpr ヘッダを直接書いても構わない。

### レイヤー切り替え

複数の constexpr AppConfig を定義し、`umi::set_app_config()` で切り替える:

```cpp
inline constexpr AppConfig PLAY_CONFIG = /* ... */;
inline constexpr AppConfig EDIT_CONFIG = /* ... */;

void switch_layer(Layer layer) {
    switch (layer) {
    case Layer::PLAY: umi::set_app_config(PLAY_CONFIG); break;
    case Layer::EDIT: umi::set_app_config(EDIT_CONFIG); break;
    }
}
```

`umi::set_app_config()` の内部実装:

```cpp
inline int set_app_config(const AppConfig& cfg) noexcept {
    return syscall::call(nr::set_app_config,
                         reinterpret_cast<uint32_t>(&cfg),
                         sizeof(AppConfig));
}
```

OS 側は `set_app_config` syscall で inactive バッファに RouteTable, ParamMapping,
InputParamMapping を一括コピーし、各 InputConfig を設定する。
切り替えはブロック境界で一括反映されるため、レイヤー間で設定が混在しない。

コスト: ~2KB memcpy + フラグ ON。Cortex-M4 で ~600 サイクル（~3.6μs @ 168MHz）。
Audio タスクは DMA 割り込みで最高優先度で動作するため、Control タスクの syscall 中にブロックされることはない。

---

## データの性質による経路分類

入力ソースではなく、**データの性質**で経路を決める。
同じ CC メッセージでもアプリの RouteTable 設定によって経路が変わる。

### 1. AudioEventQueue — 状態変更イベント

順序と時刻が重要で、欠落が状態の不整合を起こすもの。

| 性質 | 制約 |
|------|------|
| 欠落不可 | キュー溢れはエラー |
| サンプル精度 | sample_offset 確定済み |
| 固定長 | 8B (`umidi::Event`) |
| Processor 専用 | Control は読まない |

デフォルトで該当:

- MIDI NoteOn / NoteOff
- Pitch Bend / Aftertouch
- Transport イベント（Start / Stop / Continue）

### 2. ParamSet — 確定済み連続値（共有メモリ）

最新値のみ重要。上書き前提。

| 性質 | 制約 |
|------|------|
| 欠落可（上書き） | 最新値が常に正しい |
| ブロック境界精度 | 次の process() で反映 |
| System のみ書き込み | Processor/Controller は読み出しのみ |
| versioned | dirty フラグまたはバージョン番号 |

RouteTable で `ROUTE_PARAM` が指定されたもの:

- CC → ParamMapping 経由で SharedParamState に書き込み
- ノブ / CV → 同様
- Controller からの ParamSetRequest（System 経由で確定後）

### 3. ControlEventQueue — Controller 向けイベント

UI や状態遷移に使う。数 ms の遅延を許容。

| 性質 | 制約 |
|------|------|
| 欠落可（溢れ時ドロップ） | リトライや UI 更新で補える |
| ms 精度で十分 | sample_offset 不要 |
| Controller 向け | Processor は読まない |

デフォルトで該当:

- ハードウェア入力変化（ボタン、ノブ、エンコーダ、CV → `INPUT_CHANGE`）
- CC（デフォルトは全て Control 行き）
- Program Change
- モード切替

### 4. Stream — 高密度データ（共有メモリ）

必要な場合のみ有効化するオプション経路。

| 性質 | 制約 |
|------|------|
| 単一 writer（System） | 複数 reader 可 |
| reader ごとに cursor | 独立した読み進め |
| 全メッセージ記録 | フィルタなし |

該当する用途:

- MIDI ルーター
- MIDI Learn（生の CC を全部見る）
- デバッグログ

---

## ParamSet / ParamSetRequest

Control が直接 Audio の状態を変えない仕組み。

### ParamSetRequest（Controller → System）

Controller が生成する。意味付け済みだが時刻未確定。
syscall で System に送る。

```cpp
struct ParamSetRequest {
    param_id_t param_id;
    float value;            // 論理値（0.0〜1.0 等）
    ParamSource source;     // 発生源の識別
    ParamOption option;     // 即時 / 補間希望 / hold
};

enum class ParamSource : uint8_t {
    MIDI_CC,        // CC マッピング経由（System が自動生成）
    KNOB,           // 物理ノブ
    CV,             // CV 入力
    UI,             // Controller（UI 操作）
    PRESET,         // プリセットロード
    AUTOMATION,     // DAW オートメーション
};

enum class ParamOption : uint8_t {
    IMMEDIATE,      // 次のブロック境界で即時反映
    RAMP,           // 補間希望（Processor 側で実装）
    HOLD,           // 現在値を維持（リリース時など）
};
```

### ParamSet（System → 共有メモリ）

System が生成する。時刻確定済み、Processor/Controller は読み出しのみ。

```cpp
struct ParamSet {
    param_id_t param_id;
    float value;            // 実値（検証・クランプ済み）
    uint32_t sample_offset; // 確定時刻（0 = ブロック先頭）
    ParamFlags flags;       // immediate / ramp / hold
};

struct ParamFlags {
    bool immediate : 1;
    bool ramp : 1;
    bool hold : 1;
};
```

### 流れ

```
MIDI CC#74 受信（RouteTable で ROUTE_PARAM 指定済み）
  → Parser → UMP32
  → System: RouteTable 参照 → ROUTE_PARAM
  → System: ParamMapping 参照 → cc[74] = { param_id=3, min=0, max=1 }
  → System: value = cc_value/127 × (max-min) + min
  → System: SharedParamState.values[3] = value
  → Processor: params.get(3) → 最新値（次の process()）
  → Controller: params.get(3) → 最新値（UI 表示用）
```

```
Controller が UI からパラメータ変更
  → Controller: syscall::send_param_request({ param_id=3, value=0.5, source=UI })
  → System: 検証 → SharedParamState.values[3] = 0.5
  → 次のブロック境界で Audio に反映
```

**Controller は SharedParamState を直接書き換えない。**
syscall で Request を送り、System が確定する。

### 競合解決

同じパラメータに複数ソースから同時に Request が来た場合:

```
デフォルト: last-write-wins（最後に触ったソースが勝つ）
```

ポリシーは OS 側で設定可能（将来拡張）。

---

## 共有メモリの構造

System が書き込み、Processor / Controller が読む。
全て `.shared` セクション (16KB) に配置。

```cpp
struct SharedParamState {
    static constexpr size_t MAX_PARAMS = 32;

    float values[MAX_PARAMS] = {};
    uint32_t version = 0;
    ParamFlags flags[MAX_PARAMS] = {};

    [[nodiscard]] float get(param_id_t id) const noexcept {
        return id < MAX_PARAMS ? values[id] : 0.0f;
    }
};
```

```cpp
struct SharedChannelState {
    static constexpr size_t CHANNELS = 16;

    struct Channel {
        uint16_t pitch_bend = 8192;
        uint8_t pressure = 0;
    };

    Channel channels[CHANNELS] = {};

    [[nodiscard]] float pitch_bend_f(uint8_t ch) const noexcept {
        if (ch >= CHANNELS) return 0.0f;
        return (float(channels[ch].pitch_bend) - 8192.0f) / 8192.0f;
    }

    [[nodiscard]] float pressure_f(uint8_t ch) const noexcept {
        if (ch >= CHANNELS) return 0.0f;
        return float(channels[ch].pressure) / 127.0f;
    }
};
```

ハードウェア入力（ボタン、ノブ、エンコーダ、CV）の統一状態:

詳細は「ハードウェア入力の統一モデル」セクションを参照。

```cpp
struct SharedInputState {
    static constexpr size_t MAX_INPUTS = 16;
    uint16_t raw[MAX_INPUTS] = {};   // 32B

    [[nodiscard]] float get(uint8_t id) const noexcept;
    [[nodiscard]] uint16_t get_raw(uint8_t id) const noexcept;
    [[nodiscard]] bool is_on(uint8_t id) const noexcept;
};
```

---

## Syscall 追加

現在の syscall 番号: 0-12。以下を追加。

| 番号 | 名前 | 方向 | 用途 |
|------|------|------|------|
| 13 | `set_route_table` | App→OS | RouteTable 登録 (272B コピー) |
| 14 | `set_param_mapping` | App→OS | ParamMapping 登録 (1536B コピー) |
| 15 | `send_param_request` | App→OS | ParamSetRequest 送信 (8B) |
| 16 | `configure_input` | App→OS | InputConfig 登録（ノブ/CV 購読モード） |
| 17 | `read_sysex` | App←OS | SysExBuffer から読み出し ([SYSEX_ROUTING.md](SYSEX_ROUTING.md)) |
| 18 | `send_sysex` | App→OS | SysEx メッセージ送信 ([SYSEX_ROUTING.md](SYSEX_ROUTING.md)) |
| 19 | `set_input_mapping` | App→OS | InputParamMapping 登録 (192B コピー) |
| 20 | `set_app_config` | App→OS | AppConfig 一括適用 (nr 13,14,16,19 を内部で順次実行) |

全て SVC 命令で特権モードに遷移し、カーネル側で処理。

### OS 側のバッファ管理

RouteTable と ParamMapping はダブルバッファ:

```
syscall 呼び出し → pending バッファにコピー → pending フラグ ON
Audio ブロック境界 → active/pending を切り替え → pending フラグ OFF
```

ブロック途中で設定が変わることはない。

ParamSetRequest は SPSC キュー:

```
syscall 呼び出し → RequestQueue に push
System Service → RequestQueue を pop → 検証 → SharedParamState に書き込み
```

---

## ハードウェア入力の統一モデル

全てのハードウェア入力（ボタン、ノブ、エンコーダ、CV）を
**値（状態） + 状態変更イベント** の単一モデルで扱う。

入力の種類ごとに専用の型やイベントを持たない。
エッジ検出（立ち上がり/立ち下がり）や変化量デルタはアプリ側で導出する。

### System 側の処理（全入力共通）

```
入力変化検出（ISR / DMA / ポーリング）
  → System: 正規化 → uint16_t (0x0000〜0xFFFF)
  → SharedInputState.raw[id] 更新
  → 変化があれば ControlEventQueue へ INPUT_CHANGE { id, value }
```

入力ソースごとの正規化:

| 入力 | 正規化 |
|------|--------|
| ボタン (GPIO) | debounce → 0x0000 (OFF) / 0xFFFF (ON) |
| ノブ (ADC 12bit) | `adc_value << 4` |
| エンコーダ | ステップ位置 → 0x0000〜0xFFFF |
| CV (ADC 12bit) | `adc_value << 4` |

### Processor での使い方

```cpp
float cutoff_knob = ctx.input.get(KNOB_CUTOFF);   // float (FPU あり)
bool bypass = ctx.input.is_on(BTN_BYPASS);          // 整数比較のみ
```

- ブロック単位精度で十分な場合は `SharedInputState` から直接読む
- Audio レートの連続値が必要な場合は audio_stream モード（下記）

### Controller での使い方

```cpp
void control(ControlContext& ctx) {
    ControlEvent e;
    while (ctx.events->pop(e)) {
        if (e.type == ControlEventType::INPUT_CHANGE) {
            uint8_t id = e.input.id;
            uint16_t val = e.input.value;

            // エッジ検出（前回値との比較）
            if (id == BTN_MODE && prev[id] <= 0x7FFF && val > 0x7FFF) {
                cycle_mode();  // 立ち上がりエッジ
            }
            prev[id] = val;
        }
    }
}
```

### 購読モード（Audio レート連続値が必要な場合）

```cpp
enum class InputMode : uint8_t {
    CONTROL_ONLY,     // 状態 + イベントのみ（デフォルト）
    AUDIO_STREAM,     // 追加で AudioInputStream に連続値を供給
    BOTH,             // 両方
};

struct InputConfig {
    uint8_t input_id;
    InputMode mode = InputMode::CONTROL_ONLY;
    uint32_t sample_rate = 0;     // audio_stream 時のレート（0 = Audio レート）
    uint16_t smoothing = 0;       // 平滑化強度（0 = なし、ADC ノイズ対策）
    uint16_t threshold = 655;     // INPUT_CHANGE 発火閾値（~1%、uint16_t スケール）
};
```

購読モードは `syscall::configure_input()` で設定。切替はブロック境界で反映。

---

## ClockDomain

Audio と Control で時間基準が異なる。

### Audio ClockDomain

- DMA 割り込み駆動
- sample_rate に同期（48kHz / 44.1kHz）
- 時間単位: サンプル数
- `sample_offset`: バッファ内のサンプル位置（0〜buffer_size-1）

### Control ClockDomain

- タスクスケジューラ駆動（SysTick / SOF）
- 1kHz 程度
- 時間単位: ms
- `delta_time`: 前回からの経過時間

### System の役割

System は両方の ClockDomain を理解している。

- 入力を受け取った時点の Audio ClockDomain 上の位置を算出
- Control 向けにはタイムスタンプ不要（順序のみ）
- Transport 同期（MIDI Clock → Audio Clock への写像）

```
USB SOF (1ms) → sample_offset = (sof - buffer_start_sof) × samples_per_sof
1kHz タスク → 同様に sample_offset 算出
MIDI Clock → BPM/位相追跡 → Audio ClockDomain 上のテンポとして共有
```

---

## System Service の処理フロー

System Service は Server タスク優先度（ISR の次に高い）で動作。

```
1フレーム（= 1 Audio バッファ期間）:

[ISR / DMA]
  → RawInputQueue に入力を蓄積

[System Service タスク] ← DMA half/complete 割り込みで起床
  1. ダブルバッファ切り替え（RouteTable / ParamMapping の pending があれば）
  2. RawInputQueue を全て読み出す
  3. 各入力の sample_offset を確定
  4. RouteTable を参照して振り分け:
     - ROUTE_AUDIO → AudioEventQueue
     - ROUTE_PARAM → ParamMapping 参照 → SharedParamState に書き込み
     - ROUTE_CONTROL → ControlEventQueue
     - ROUTE_STREAM → Stream にコピー
  5. ParamSetRequest キューを読み出す
     → 検証 → SharedParamState に書き込み

[Audio Task] ← DMA 割り込み直後
  process() 呼び出し
  → AudioEventQueue + SharedParamState + SharedChannelState を読む

[Control Task] ← 1kHz
  control() 呼び出し
  → ControlEventQueue + SharedParamState を読む
  → ParamSetRequest / RouteTable 変更を syscall で発行
```

---

## Application から見える API

### Processor::process(AudioContext)

```cpp
struct AudioContext {
    std::span<const sample_t* const> inputs;
    std::span<sample_t* const> outputs;

    // 状態変更イベント（RouteTable で ROUTE_AUDIO に指定されたもの）
    std::span<const umidi::Event> events;

    // 連続値（System が書き込んだ最新値）
    const SharedParamState& params;
    const SharedChannelState& channel;

    // ハードウェア入力状態（ボタン/ノブ/エンコーダ/CV、全て uint16_t 統一）
    const SharedInputState& input;

    uint32_t sample_rate;
    uint32_t buffer_size;
    float dt;
    sample_position_t sample_position;
};
```

```cpp
void process(AudioContext& ctx) {
    float cutoff = ctx.params.get(PARAM_CUTOFF);
    float pb = ctx.channel.pitch_bend_f(0);

    size_t event_idx = 0;
    for (size_t s = 0; s < ctx.buffer_size; ++s) {
        while (event_idx < ctx.events.size() &&
               ctx.events[event_idx].sample_pos <= s) {
            const auto& e = ctx.events[event_idx++];
            if (e.is_note_on()) {
                voices.note_on(e.note(), e.velocity());
            } else if (e.is_note_off()) {
                voices.note_off(e.note());
            }
        }
        output[s] = voices.render(cutoff, pb);
    }
}
```

Processor が知る必要がないこと:
- 入力が MIDI か ノブか CV か
- CC の番号
- RouteTable の内容
- sample_offset の算出方法

### Controller::control(ControlContext)

```cpp
struct ControlContext {
    float delta_time;
    sample_position_t sample_pos;

    // Control 向けイベント（MIDI CC, INPUT_CHANGE, ProgramChange 等）
    ControlEventQueue* events;

    // パラメータ（読み取り用）
    const SharedParamState* params;

    // ハードウェア入力状態（ボタン/ノブ/エンコーダ/CV、全て uint16_t 統一）
    const SharedInputState* input;
};
```

```cpp
void control(ControlContext& ctx) {
    ControlEvent e;
    while (ctx.events->pop(e)) {
        if (e.type == ControlEventType::MIDI && e.midi.is_cc()) {
            if (learn_mode) {
                learn_target_cc = e.midi.cc_number();
            }
        }
    }

    // UI からのパラメータ変更
    if (ui_knob_changed) {
        syscall::send_param_request({
            .param_id = PARAM_CUTOFF,
            .value = ui_knob_value,
            .source = ParamSource::UI,
            .option = ParamOption::RAMP,
        });
    }

    // 表示更新
    display_param(PARAM_CUTOFF, ctx.params->get(PARAM_CUTOFF));
}
```

---

## UMP32 の位置づけ

UMP32 は MIDI メッセージの内部表現。ボタンやノブは UMP32 ではない。

```
AudioEventQueue:
  umidi::Event (8B) = sample_pos(4B) + UMP32(4B)
  → NoteOn/Off, PitchBend, Transport 等

ControlEventQueue:
  ControlEvent (8B) = type(1B) + padding(3B) + payload(4B)
  → MIDI CC, INPUT_CHANGE, ProgramChange, MODE_CHANGE 等
```

```cpp
enum class ControlEventType : uint8_t {
    MIDI,           // MIDI メッセージ（UMP32）
    INPUT_CHANGE,   // 全ハードウェア入力（ボタン/ノブ/エンコーダ/CV）
    MODE_CHANGE,    // システムモード変更
};

struct ControlEvent {
    ControlEventType type;
    uint8_t _pad[3];                    // 4B アライメント
    union {
        umidi::UMP32 midi;              // 4B: MIDI メッセージ
        InputEvent input;               // 4B: { id, _pad, value(u16) }
        struct { uint8_t mode; } mode;  // 1B: モード
    };
};
// 8B — 全イベントが同じサイズ
```

---

## メモリ使用量

### OS 側（カーネル RAM）

| コンポーネント | サイズ | 備考 |
|---------------|--------|------|
| RouteTable × 2 | 544B | ダブルバッファ |
| ParamMapping × 2 | 3072B | ダブルバッファ (1536B × 2) |
| InputParamMapping × 2 | 384B | ダブルバッファ (192B × 2) |
| AudioEventQueue (64) | 512B | umidi::Event 8B × 64 |
| ControlEventQueue (32) | 256B | ControlEvent 8B × 32 |
| ParamSetRequestQueue (16) | 128B | 8B × 16 |
| SysExAssembler × 2 | 520B | USB/UART 各1 ([SYSEX_ROUTING.md](SYSEX_ROUTING.md)) |
| **OS 側合計** | **~5.4KB** | |

### 共有メモリ (.shared セクション)

| コンポーネント | サイズ | 備考 |
|---------------|--------|------|
| SharedParamState (32) | 164B | float×32 + flags×32 + version |
| SharedChannelState (16) | 64B | Channel 4B × 16 |
| SharedInputState (16) | 32B | uint16_t × 16 |
| SysExBuffer | 516B | [SYSEX_ROUTING.md](SYSEX_ROUTING.md) |
| 既存 SharedMemory | ~3KB | Audio バッファ、イベントキュー等 |
| **共有メモリ合計** | **~3.8KB** | 16KB 中 |

---

## USB MIDI 2.0 対応

当面は MIDI 1.0 前提（UMP32 MT=2）。将来の差分:

| 項目 | 変更内容 | 影響範囲 |
|------|---------|---------|
| USB ディスクリプタ | Group Terminal Block 追加 | umiusb |
| パケット受信 | バイトパース不要、UMP 直接キャスト | umidi transport |
| UMP64 MT=4 | velocity 16bit, CC 値 32bit | umidi::Event サイズ変更 |
| ダウンコンバート | UMP64 → UMP32 互換変換 | umidi |
| RouteTable | MT=4 用の追加テーブル | OS + syscall |

AudioContext と SharedParamState の API は変わらない。
Processor から見れば MIDI 2.0 でも同じ `events` / `params` / `channel` を読むだけ。
