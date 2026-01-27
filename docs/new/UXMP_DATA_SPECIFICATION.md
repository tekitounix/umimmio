# UXMP-DATA User Data Exchange 詳細仕様書

バージョン: 0.3.0 (Draft)
ステータス: 設計段階

## 1. 概要

本仕様書はUXMP (UMI Extended MIDI Protocol) の User Data Exchange プロトコルの詳細を定義する。
パラメータ、プリセット、パターン、ソング、サンプルデータの**統一フォーマット**を策定し、
異なるメーカーのデバイス間でデータ交換を可能にすることを目的とする。

### 1.1 設計原則

1. **統一データブロック構造** - すべてのデータ型が同一の基本構造を使用
2. **インデックスベースのパラメータ** - データ部にIDを持たず、定義テーブルでインデックスの意味を決定
3. **最小データサイズ** - 使用しないパラメータはデータを占有しない
4. **疎/密表現の選択** - POSITIONパラメータの有無でデータ形式を切り替え
5. **標準パラメータID** - MIDI互換の16ビットID空間で異機種間互換性を保証
6. **完全な独立性** - 各データ型は単体で完結し、他への依存なしに使用可能
7. **前方/後方互換性** - 定義テーブルの拡張で新パラメータに対応
8. **自己記述型ベンダー拡張** - 登録不要、情報表示によるユーザー判断

### 1.2 統一データブロック構造 (★核心)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    統一データブロック構造                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  すべてのユーザーデータ（パターン、プリセット、オートメーション等）が         │
│  同じ基本構造を共有する。                                                    │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │  DataBlock Header                                                    │  │
│  │    block_type     : uint8   // データ種別                           │  │
│  │    param_count    : uint8   // パラメータ数                          │  │
│  │    entry_count    : uint16  // エントリ数                            │  │
│  │    flags          : uint16  // フラグ                                │  │
│  │    reserved       : uint16  // 予約                                  │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │  Parameter Definition Table (ParamDef × param_count)                 │  │
│  │    id             : uint16  // パラメータID (★MIDI互換)            │  │
│  │    flags          : uint8   // bit0: optional, bit1: signed,        │  │
│  │                             // bit2: extended (16bit)               │  │
│  │    reserved       : uint8   // 予約                                 │  │
│  │    min            : int16   // 最小値                                │  │
│  │    max            : int16   // 最大値                                │  │
│  │    default_val    : int16   // デフォルト値                          │  │
│  │  ★8bitが標準、flags.extended=1で16bitに拡張                         │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │  Vendor Extension (オプション、flags.has_vendor_ext = 1 の場合)      │  │
│  │    vendor_name    : string  // ベンダー名 (表示用)                  │  │
│  │    product_name   : string  // 製品名 (表示用)                      │  │
│  │    VendorParamDef[]         // ベンダー固有パラメータ定義 (表示用)  │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │  Data Section (entry_count × param_count × size)                     │  │
│  │    純粋な値の配列。IDなし。インデックス位置で意味が決まる。          │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 1.3 構造体定義

```c
// ブロック種別
enum class BlockType : uint8_t {
    STEP_PATTERN    = 0x01,  // シーケンサーパターン
    PATCH           = 0x02,  // シンセパッチ
    AUTOMATION      = 0x03,  // オートメーション
    WAVETABLE       = 0x04,  // 波形テーブル
    MAPPING         = 0x05,  // MIDIマッピング
    SAMPLE_META     = 0x06,  // サンプルメタデータ
    MODULATION      = 0x07,  // モジュレーションデータ
    // 0x80-0xFF: ベンダー固有
};

// パラメータ定義 (★8bit標準、16bit拡張対応)
struct ParamDef {
    uint16_t id;          // パラメータID (ParamId名前空間参照)
    uint8_t  flags;       // bit0: optional, bit1: signed, bit2: extended (16bit)
    uint8_t  reserved;    // 予約 (パディング)
    int16_t  min;         // 最小値
    int16_t  max;         // 最大値
    int16_t  default_val; // デフォルト値
};
static_assert(sizeof(ParamDef) == 10);

// flags ビット定義
// bit0: optional - このパラメータは省略可能
// bit1: signed   - 符号付き値 (min/maxの解釈に影響)
// bit2: extended - 16bit拡張 (0=8bit, 1=16bit)
// bit3-7: 予約

// データサイズは flags.extended で決定:
//   extended=0 → 8bit (1バイト)
//   extended=1 → 16bit (2バイト)

// データブロックヘッダー
struct DataBlockHeader {
    uint8_t  magic[4];       // "UXDB" (UXmp Data Block)
    uint8_t  block_type;     // BlockType
    uint8_t  param_count;    // パラメータ定義数
    uint16_t entry_count;    // エントリ数
    uint16_t flags;          // bit0: has_position, bit1: compressed, bit2: has_vendor_ext
    uint16_t version;        // フォーマットバージョン
    uint32_t data_offset;    // データセクションへのオフセット
    uint32_t vendor_offset;  // ベンダー拡張へのオフセット (0=なし)
    uint32_t total_size;     // 全体サイズ
    // ParamDef defs[param_count] follows
    // VendorExtension follows at vendor_offset (if has_vendor_ext)
    // uint8_t data[] follows at data_offset
};
```

### 1.4 密表現と疎表現

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    密表現 (Dense) vs 疎表現 (Sparse)                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  【密表現】POSITIONパラメータなし                                           │
│    - インデックス = ステップ位置                                            │
│    - すべてのステップが順番に格納される                                     │
│    - ドラムパターン等、多くのステップが埋まる場合に効率的                   │
│                                                                             │
│    例: 16ステップドラム [NOTE, VEL, GATE]                                  │
│    index 0 → step 0: [36, 100, 50]                                         │
│    index 1 → step 1: [0, 0, 0]                                             │
│    index 2 → step 2: [38, 80, 75]                                          │
│    ...                                                                      │
│    index 15 → step 15: [42, 90, 60]                                        │
│                                                                             │
│  【疎表現】POSITIONパラメータあり                                           │
│    - インデックス ≠ ステップ位置                                            │
│    - 音があるステップのみ格納                                               │
│    - メロディパターン等、空ステップが多い場合に効率的                       │
│                                                                             │
│    例: 64ステップ中4音 [POSITION, NOTE, VEL]                               │
│    index 0 → [0, 60, 100]   // step 0: C4                                  │
│    index 1 → [16, 64, 90]   // step 16: E4                                 │
│    index 2 → [32, 67, 85]   // step 32: G4                                 │
│    index 3 → [48, 72, 80]   // step 48: C5                                 │
│                                                                             │
│    データサイズ: 4 × 3 = 12バイト (vs 密表現: 64 × 3 = 192バイト)          │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 1.5 ドキュメント構成

| 章 | 内容 |
|----|------|
| 2 | 標準パラメータ定義 |
| 3 | パターン仕様 |
| 4 | プリセット仕様 |
| 5 | オートメーション仕様 |
| 6 | 参照とリンク |
| 7 | 変換と互換性 |

---

## 2. パラメータID名前空間 (UXMP-PARAM)

### 2.1 16ビットパラメータID体系

パラメータIDはMIDIとの互換性を最優先し、**CC番号をそのままIDとして使用**できる設計。
MIDI送信時の変換処理を最小化し、組み込み環境での実行効率を重視。

| ID範囲 | 用途 | 備考 |
|--------|------|------|
| 0x0000-0x007F | MIDI CC | ★CC番号直接マッピング |
| 0x0080-0x00FF | MIDI Voice | NOTE, VELOCITY, PITCH_BEND等 |
| 0x0100-0x01FF | RPN | 14bit値 |
| 0x0200-0x5FFF | NRPN | 14bit NRPN番号 |
| 0x6000-0x7FFF | Reserved | 将来の拡張用 |
| 0x8000-0x8FFF | Step Sequencer | POSITION, GATE, PROBABILITY等 |
| 0x9000-0x9FFF | Synth | OSC, FILTER, ENV, LFO等 |
| 0xA000-0xAFFF | Sample/Clip | サンプル再生パラメータ |
| 0xB000-0xBFFF | Tracker | INSTRUMENT, FX_CMD等 |
| 0xC000-0xCFFF | Automation | TIME, VALUE, CURVE等 |
| 0xD000-0xEFFF | Reserved | 将来の拡張用 |
| 0xF000-0xFFFE | Vendor | ベンダー固有 (登録不要) |
| 0xFFFF | INVALID | 無効/未定義 |

**よく使用するID例:**

```c
// MIDI CC (直接マッピング)
0x0007  // CC#7 Volume
0x0047  // CC#71 Cutoff
0x0048  // CC#72 Resonance

// MIDI Voice
0x0080  // NOTE
0x0081  // VELOCITY
0x0083  // PITCH_BEND (14bit, extended=1)

// Step Sequencer
0x8000  // POSITION (疎表現用)
0x8001  // GATE
0x8004  // PROBABILITY
```

※ 完全なID定義は **付録A** を参照

### 2.2 MIDI変換の効率化

```c
// パラメータID → MIDI送信 (最速の変換)
void send_param_as_midi(uint8_t channel, uint16_t id, uint16_t value) {
    if (id < 0x80) {
        // CC直接送信 (変換不要)
        send_midi_cc(channel, id, value & 0x7F);
    }
    else if (id < 0x86) {
        // Voice Message
        switch (id) {
            case 0x80: send_note_on(channel, value >> 8, value & 0x7F); break;
            case 0x83: send_pitch_bend(channel, value); break;
            case 0x84: send_program_change(channel, value & 0x7F); break;
        }
    }
    else if (id < 0x200) {
        // RPN (14bit)
        send_rpn(channel, id & 0xFF, value);
    }
    else if (id < 0x6000) {
        // NRPN (14bit)
        send_nrpn(channel, id - 0x0200, value);
    }
    // 0x8000以降は内部パラメータ (MIDI送信なし)
}
```

### 2.3 パラメータID使用ガイドライン

| ID範囲 | 用途 | 備考 |
|--------|------|------|
| 0x0000-0x007F | MIDI CC | ★CC番号直接マッピング |
| 0x0080-0x00FF | MIDI Voice | ノート、ベロシティ等 |
| 0x0100-0x01FF | RPN | 14bit値 |
| 0x0200-0x5FFF | NRPN | 14bit NRPN番号 |
| 0x6000-0x7FFF | Reserved | 将来の拡張用 |
| 0x8000-0x8FFF | Step Sequencer | ゲート、確率等 |
| 0x9000-0x9FFF | Synth | オシレーター、フィルター等 |
| 0xA000-0xAFFF | Sample/Clip | サンプル再生 |
| 0xB000-0xBFFF | Tracker | インストゥルメント、エフェクト |
| 0xC000-0xCFFF | Automation | オートメーション |
| 0xD000-0xEFFF | Reserved | 将来の拡張用 |
| 0xF000-0xFFFF | Vendor | ベンダー固有 (登録不要) |

---

## 3. ベンダー拡張

### 3.1 設計思想

- ベンダーIDの登録は不要
- 非標準のベンダーパラメータは受信側で無視される
- `fallback_id` を指定することで、非対応デバイスでも標準パラメータにフォールバック可能
- パラメータ情報（名前、範囲）は表示用に保持

### 3.2 VendorExtension構造体

```c
// ベンダー拡張ヘッダー
struct VendorExtension {
    uint8_t  vendor_name_len;   // ベンダー名の長さ
    uint8_t  product_name_len;  // 製品名の長さ
    uint16_t param_count;       // ベンダー固有パラメータ数
    // char vendor_name[vendor_name_len] follows  // 例: "TechnoMaker"
    // char product_name[product_name_len] follows // 例: "GrooveBox X"
    // VendorParamDef params[param_count] follows
};

// ベンダー固有パラメータ定義
struct VendorParamDef {
    uint16_t local_id;        // ローカルID (0x0000-0x0FFF)
                              // データ内でのIDは 0xF000 | local_id
    uint16_t fallback_id;     // フォールバック先ID (0=なし)
                              // 非対応デバイスで使用される標準パラメータID
    uint8_t  flags;           // bit0: optional, bit1: signed, bit2: extended (16bit)
    uint8_t  name_len;        // パラメータ名の長さ
    int16_t  min;             // 最小値
    int16_t  max;             // 最大値
    int16_t  default_val;     // デフォルト値
    // char name[name_len] follows  // 例: "Grit"
};
// データサイズは flags.extended で決定 (ParamDefと同様)
```

### 3.3 フォールバック動作

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    フォールバック動作                                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ベンダーパラメータ 0xF001 "Grit" (fallback_id = 0x47)                      │
│                                                                             │
│  受信側がベンダー拡張に対応している場合:                                     │
│    → 0xF001 としてそのまま処理                                              │
│                                                                             │
│  受信側が非対応の場合:                                                       │
│    → fallback_id (0x47 = CC#71 Cutoff) に割り当て                          │
│    → 値はそのまま使用される                                                 │
│                                                                             │
│  fallback_id = 0xFFFF の場合:                                               │
│    → パラメータは無視される                                                  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.4 使用例

```c
// 例: "TechnoMaker" が "GrooveBox X" 用のカスタムパラメータを定義

VendorExtension ext = {
    .vendor_name_len = 10,    // "TechnoMaker"
    .product_name_len = 11,   // "GrooveBox X"
    .param_count = 2,
};
// followed by: "TechnoMaker" "GrooveBox X"

VendorParamDef params[] = {
    // local_id=0x001, fallback=CC#71, name="Grit"
    // 非対応デバイスではCC#71 (Cutoff) として動作
    {0x001, 0x47, 0, 4, 0, 127, 64},  // followed by "Grit"

    // local_id=0x002, fallback=INVALID (無視), name="Warmth"
    // 非対応デバイスでは無視される
    {0x002, 0xFFFF, 0, 6, 0, 127, 32},  // followed by "Warmth"
};
```

---

## 4. 標準パラメータ定義テンプレート

### 4.1 標準ステップパターン定義

最も一般的な使用パターンに対する標準定義。
これらをデフォルトとして使用することで、異機種間の互換性を保証する。

**注**: ParamDef構造体は `{id, flags, reserved, min, max, default_val}` の形式。
- flags.extended=0: 8bitデータ
- flags.extended=1: 16bitデータ (POSITIONなど)

#### 4.1.1 ドラムトラック標準定義

```c
// DRUM_MINIMAL: 最小構成 (2パラメータ)
// データサイズ: 2バイト/ステップ
const ParamDef DRUM_MINIMAL[] = {
    // {id, flags, reserved, min, max, default_val}
    {0x8002, 0, 0, 0, 1,   0},    // TRIGGER (0x8002)
    {0x0081, 0, 0, 0, 127, 100},  // VELOCITY (0x81)
};

// DRUM_STANDARD: 標準構成 (4パラメータ)
// データサイズ: 4バイト/ステップ
const ParamDef DRUM_STANDARD[] = {
    {0x0080, 0,    0, 0,   127, 36},   // NOTE (0x80): キック=36
    {0x0081, 0,    0, 0,   127, 100},  // VELOCITY (0x81)
    {0x8001, 0,    0, 0,   127, 64},   // GATE (0x8001): 50%
    {0x8003, 0x02, 0, -64, 63,  0},    // MICRO_TIMING (0x8003): signed
};

// DRUM_EXTENDED: 拡張構成 (6パラメータ)
// データサイズ: 6バイト/ステップ
const ParamDef DRUM_EXTENDED[] = {
    {0x0080, 0,    0, 0,   127, 36},
    {0x0081, 0,    0, 0,   127, 100},
    {0x8001, 0,    0, 0,   127, 64},
    {0x8003, 0x02, 0, -64, 63,  0},
    {0x8004, 0,    0, 0,   127, 127},  // PROBABILITY (0x8004): 100%
    {0x8005, 0,    0, 0,   255, 0},    // CONDITION (0x8005): ALWAYS
};
```

#### 4.1.2 メロディトラック標準定義

```c
// MELODY_MINIMAL: 最小構成 (密表現)
// データサイズ: 2バイト/ステップ
const ParamDef MELODY_MINIMAL[] = {
    {0x0080, 0, 0, 0, 127, 60},   // NOTE (0x80): C4
    {0x0081, 0, 0, 0, 127, 100},  // VELOCITY (0x81)
};

// MELODY_STANDARD: 標準構成 (密表現)
// データサイズ: 4バイト/ステップ
const ParamDef MELODY_STANDARD[] = {
    {0x0080, 0,    0, 0,   127, 60},
    {0x0081, 0,    0, 0,   127, 100},
    {0x8001, 0,    0, 0,   127, 64},   // GATE (0x8001)
    {0x8003, 0x02, 0, -64, 63,  0},    // MICRO_TIMING (0x8003)
};

// MELODY_SPARSE: 疎表現 (ステップ位置付き)
// データサイズ: 5バイト/エントリ (POSITION=2byte + 3byte)
const ParamDef MELODY_SPARSE[] = {
    {0x8000, 0x04, 0, 0, 65535, 0},   // POSITION (0x8000): extended=1 → 16bit
    {0x0080, 0,    0, 0, 127,   60},  // NOTE (0x80)
    {0x0081, 0,    0, 0, 127,   100}, // VELOCITY (0x81)
    {0x8001, 0,    0, 0, 127,   64},  // GATE (0x8001)
};
```

#### 4.1.3 トラッカートラック標準定義

```c
// TRACKER_MINIMAL: LSDJ互換 (4パラメータ)
// データサイズ: 4バイト/ステップ
const ParamDef TRACKER_MINIMAL[] = {
    {0x0080, 0, 0, 0, 127, 0xFF},  // NOTE (0x80): 0xFF=なし
    {0xB000, 0, 0, 0, 127, 0xFF},  // INSTRUMENT (0xB000)
    {0xB010, 0, 0, 0, 255, 0},     // FX1_CMD (0xB010)
    {0xB011, 0, 0, 0, 255, 0},     // FX1_VAL (0xB011)
};

// TRACKER_STANDARD: M8互換 (9パラメータ)
// データサイズ: 9バイト/ステップ
const ParamDef TRACKER_STANDARD[] = {
    {0x0080, 0, 0, 0, 127, 0xFF},  // NOTE (0x80)
    {0xB001, 0, 0, 0, 127, 0xFF},  // VOLUME (0xB001)
    {0xB000, 0, 0, 0, 127, 0xFF},  // INSTRUMENT (0xB000)
    {0xB010, 0, 0, 0, 255, 0},     // FX1_CMD (0xB010)
    {0xB011, 0, 0, 0, 255, 0},     // FX1_VAL (0xB011)
    {0xB012, 0, 0, 0, 255, 0},     // FX2_CMD (0xB012)
    {0xB013, 0, 0, 0, 255, 0},     // FX2_VAL (0xB013)
    {0xB014, 0, 0, 0, 255, 0},     // FX3_CMD (0xB014)
    {0xB015, 0, 0, 0, 255, 0},     // FX3_VAL (0xB015)
};
```

### 4.2 標準定義ID

```c
// 標準定義を識別するためのID
// standard_def_id != CUSTOM の場合、ParamDef配列を省略可能
enum class StandardDefId : uint8_t {
    CUSTOM          = 0x00,  // カスタム定義 (ParamDef配列が続く)

    // ドラム系
    DRUM_MINIMAL    = 0x01,
    DRUM_STANDARD   = 0x02,
    DRUM_EXTENDED   = 0x03,

    // メロディ系
    MELODY_MINIMAL  = 0x10,
    MELODY_STANDARD = 0x11,
    MELODY_SPARSE   = 0x12,
    MELODY_FULL     = 0x13,

    // トラッカー系
    TRACKER_MINIMAL = 0x20,
    TRACKER_STANDARD= 0x21,
    TRACKER_FULL    = 0x22,

    // シンセパラメータ
    SYNTH_BASIC     = 0x30,
    SYNTH_FULL      = 0x31,

    // オートメーション
    AUTOMATION_BASIC= 0x40,

    // サンプル/クリップ
    SAMPLE_BASIC    = 0x50,
    SAMPLE_EXTENDED = 0x51,
    CLIP_BASIC      = 0x52,
};
```

---

## 5. パターン仕様 (UXMP-PATTERN)

### 5.1 パターン構造

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Pattern File Structure                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │  Pattern Header                                                      │  │
│  │    magic: "UXPT"                                                    │  │
│  │    version, track_count, pattern_length, tempo, time_sig...         │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │  Track Directory (TrackHeader × track_count)                         │  │
│  │    各トラックのメタデータと DataBlock へのオフセット                 │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │  Track DataBlocks                                                    │  │
│  │    Track 0: DataBlock (統一フォーマット)                             │  │
│  │    Track 1: DataBlock                                                │  │
│  │    ...                                                               │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │  Parameter Locks (オプション)                                        │  │
│  │    ステップごとのサウンドパラメータオーバーライド                    │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │  Reference Table (オプション)                                        │  │
│  │    Preset/Sample への参照                                            │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 5.2 パターンヘッダー

```c
struct PatternHeader {
    uint8_t  magic[4];          // "UXPT"
    uint16_t version;           // フォーマットバージョン
    uint8_t  track_count;       // トラック数
    uint8_t  flags;             // bit0: has_references, bit1: has_plocks
    uint16_t pattern_length;    // パターン長 (ステップ)
    uint16_t time_sig_num;      // 拍子分子
    uint16_t time_sig_den;      // 拍子分母
    uint32_t tempo_x100;        // テンポ × 100 (12000 = 120.00 BPM)
    uint8_t  resolution;        // PPQN
    uint8_t  swing;             // スウィング量 (0-127)
    uint32_t track_dir_offset;  // トラックディレクトリへのオフセット
    uint32_t plock_offset;      // Parameter Lock へのオフセット (0=なし)
    uint32_t ref_table_offset;  // 参照テーブルへのオフセット (0=なし)
    uint32_t total_size;        // 全体サイズ
    uint32_t crc32;             // CRC32
    uint8_t  name_length;       // パターン名の長さ
    uint8_t  reserved[3];       // 予約
    // char name[] follows
};
```

### 5.3 トラックヘッダー

```c
struct TrackHeader {
    uint8_t  track_id;          // トラックID (0-255)
    uint8_t  track_type;        // TrackType
    uint8_t  midi_channel;      // MIDIチャンネル (0-15)
    uint8_t  standard_def_id;   // StandardDefId (0=custom)
    uint16_t flags;             // bit0: muted, bit1: solo, bit2: polyrhythm
    uint16_t length_steps;      // トラック長 (ステップ)
    uint32_t data_offset;       // DataBlock へのオフセット
    uint8_t  time_scale;        // クロック倍率 (0x40=1x)
    uint8_t  direction;         // PlayDirection
    uint8_t  reserved[2];       // 予約
};

enum class TrackType : uint8_t {
    DRUM     = 0x00,  // ドラム/パーカッション
    MELODIC  = 0x01,  // メロディック
    BASS     = 0x02,  // ベース
    CHORD    = 0x03,  // コード
    SAMPLE   = 0x04,  // サンプル再生
    CV       = 0x10,  // CV/Gate
    MIDI     = 0x20,  // 外部MIDI
    MOD      = 0x40,  // モジュレーション
    TRACKER  = 0x80,  // トラッカー形式
};

enum class PlayDirection : uint8_t {
    FORWARD  = 0x00,
    REVERSE  = 0x01,
    PINGPONG = 0x02,
    RANDOM   = 0x03,
};
```

### 5.4 トラックデータ (DataBlock)

各トラックは統一DataBlockフォーマットを使用。

```c
// トラックデータの構造
// standard_def_id != CUSTOM の場合:
//   - ParamDef配列は省略可能 (標準定義を使用)
//   - データセクションのみ
//
// standard_def_id == CUSTOM の場合:
//   - DataBlockHeader
//   - ParamDef × param_count
//   - Data Section

// 例: DRUM_STANDARD を使用する16ステップトラック
// TrackHeader.standard_def_id = DRUM_STANDARD
// TrackHeader.length_steps = 16
//
// Data Section (16 × 4 = 64 bytes):
// step 0:  [36, 100, 64, 0]   // kick, vel=100, gate=50%, timing=0
// step 1:  [0,  0,   0,  0]   // rest
// step 2:  [38, 80,  64, 0]   // snare
// step 3:  [0,  0,   0,  0]   // rest
// ...
```

### 5.5 条件トリガー

```c
enum class Condition : uint8_t {
    ALWAYS       = 0x00,  // 常に再生
    NEVER        = 0x01,  // 再生しない
    FILL         = 0x02,  // フィル時のみ
    NOT_FILL     = 0x03,  // フィル時以外
    FIRST        = 0x04,  // 最初のループのみ
    NOT_FIRST    = 0x05,  // 2回目以降

    // 確率 (PROBABILITY パラメータと組み合わせ)
    PROBABILITY  = 0x10,

    // Elektron スタイル
    LOOP_1_2     = 0x20,  // 2回に1回 (1回目)
    LOOP_2_2     = 0x21,  // 2回に1回 (2回目)
    LOOP_1_3     = 0x22,  // 3回に1回 (1回目)
    LOOP_2_3     = 0x23,
    LOOP_3_3     = 0x24,
    LOOP_1_4     = 0x25,
    // ... 1_8まで

    // ベンダー拡張
    VENDOR       = 0x80,
};
```

### 5.6 使用例

#### 5.6.1 シンプルなドラムパターン

```c
// 16ステップ、4トラック (Kick, Snare, HiHat, OpenHat)
// 標準定義 DRUM_STANDARD を使用

PatternHeader pattern = {
    .magic = "UXPT",
    .version = 0x0100,
    .track_count = 4,
    .pattern_length = 16,
    .tempo_x100 = 12000,  // 120 BPM
};

TrackHeader tracks[4] = {
    {.track_id=0, .track_type=DRUM, .standard_def_id=DRUM_STANDARD, .length_steps=16},
    {.track_id=1, .track_type=DRUM, .standard_def_id=DRUM_STANDARD, .length_steps=16},
    {.track_id=2, .track_type=DRUM, .standard_def_id=DRUM_STANDARD, .length_steps=16},
    {.track_id=3, .track_type=DRUM, .standard_def_id=DRUM_STANDARD, .length_steps=16},
};

// Track 0 (Kick) データ: [NOTE, VEL, GATE, TIMING] × 16
uint8_t kick_data[16][4] = {
    {36, 100, 64, 0},  // step 0: kick
    {0,  0,   0,  0},  // step 1: rest
    {0,  0,   0,  0},  // step 2: rest
    {0,  0,   0,  0},  // step 3: rest
    {36, 100, 64, 0},  // step 4: kick
    // ...
};
```

#### 5.6.2 疎なメロディパターン

```c
// 64ステップ中4音のみ
// 標準定義 MELODY_SPARSE を使用 (POSITIONパラメータあり)

TrackHeader melody_track = {
    .track_id = 0,
    .track_type = MELODIC,
    .standard_def_id = MELODY_SPARSE,
    .length_steps = 4,  // 実際のエントリ数
};

// [POSITION(2byte), NOTE, VEL, GATE] × 4
uint8_t melody_data[4][5] = {
    {0, 0,  60, 100, 64},   // step 0: C4
    {0, 16, 64, 90,  64},   // step 16: E4
    {0, 32, 67, 85,  64},   // step 32: G4
    {0, 48, 72, 80,  64},   // step 48: C5
};
// データサイズ: 20バイト (vs 密表現: 64 × 4 = 256バイト)
```

#### 5.6.3 カスタムパラメータを使用

```c
// 独自のパラメータ構成
TrackHeader custom_track = {
    .track_id = 0,
    .track_type = DRUM,
    .standard_def_id = CUSTOM,  // カスタム定義を使用
    .length_steps = 16,
};

// カスタム定義: [NOTE, VEL, PROBABILITY, RETRIG_COUNT]
// {id, flags, reserved, min, max, default_val}
ParamDef custom_defs[4] = {
    {0x0080, 0, 0, 0, 127, 36},   // NOTE (0x80)
    {0x0081, 0, 0, 0, 127, 100},  // VELOCITY (0x81)
    {0x8004, 0, 0, 0, 127, 127},  // PROBABILITY (0x8004)
    {0x8006, 0, 0, 0, 16,  0},    // RETRIG_COUNT (0x8006)
};

// データ: 16 × 4 = 64 bytes
uint8_t data[16][4] = {
    {36, 100, 127, 0},   // step 0: kick, 100% prob, no retrig
    {36, 80,  64,  4},   // step 1: kick, 50% prob, 4× retrig
    // ...
};
```

---

## 6. プリセット仕様 (UXMP-PRESET)

### 6.1 プリセット構造

プリセットも統一DataBlockフォーマットを使用。

```c
struct PresetHeader {
    uint8_t  magic[4];          // "UXPR"
    uint16_t version;           // フォーマットバージョン
    uint16_t device_family;     // デバイスファミリーID
    uint16_t device_model;      // デバイスモデルID
    uint8_t  standard_def_id;   // StandardDefId (0=custom)
    uint8_t  flags;             // bit0: has_references
    uint16_t param_count;       // パラメータ数
    uint32_t data_offset;       // データセクションへのオフセット
    uint32_t ref_table_offset;  // 参照テーブルへのオフセット (0=なし)
    uint32_t total_size;        // 全体サイズ
    uint32_t crc32;             // CRC32
    uint8_t  name_length;       // プリセット名の長さ
    uint8_t  reserved[3];       // 予約
    // char name[] follows
    // ParamDef defs[] follows (if standard_def_id == CUSTOM)
    // uint8_t data[] follows at data_offset
};
```

### 6.2 シンセプリセット標準定義

```c
// SYNTH_BASIC: 基本シンセパラメータ
// {id, flags, reserved, min, max, default_val}
// flags.extended=1 (0x04) で16bit
const ParamDef SYNTH_BASIC[] = {
    {0x9000, 0,    0, 0, 7,   0},     // OSC_WAVEFORM (0x9000): saw
    {0x9001, 0,    0, 0, 127, 64},    // OSC_PITCH (0x9001): center
    {0x9010, 0,    0, 0, 127, 100},   // FILTER_CUTOFF (0x9010)
    {0x9011, 0,    0, 0, 127, 0},     // FILTER_RESO (0x9011)
    {0x9020, 0x04, 0, 0, 999, 10},    // ENV_ATTACK (0x9020): 10ms, extended=1
    {0x9021, 0x04, 0, 0, 999, 100},   // ENV_DECAY (0x9021): 100ms, extended=1
    {0x9022, 0,    0, 0, 127, 80},    // ENV_SUSTAIN (0x9022)
    {0x9023, 0x04, 0, 0, 999, 200},   // ENV_RELEASE (0x9023): 200ms, extended=1
    {0x9030, 0,    0, 0, 127, 32},    // LFO_RATE (0x9030)
    {0x9031, 0,    0, 0, 127, 0},     // LFO_DEPTH (0x9031)
};
```

---

## 7. オートメーション仕様 (UXMP-AUTO)

### 7.1 オートメーション構造

```c
// オートメーション標準定義
// {id, flags, reserved, min, max, default_val}
// flags.extended=1 (0x04) で16bit
const ParamDef AUTOMATION_BASIC[] = {
    {0xC000, 0x04, 0, 0, 65535, 0},  // TIME (0xC000): ティック位置, extended=1
    {0xC001, 0x04, 0, 0, 65535, 0},  // PARAM_TARGET (0xC001): ターゲットID, extended=1
    {0xC002, 0x04, 0, 0, 65535, 0},  // VALUE (0xC002): 値, extended=1
    {0xC003, 0,    0, 0, 7,     0},  // CURVE (0xC003): 補間カーブ
};

enum class AutoCurve : uint8_t {
    STEP    = 0,  // 即時変化
    LINEAR  = 1,  // 線形補間
    EASE_IN = 2,  // イーズイン
    EASE_OUT= 3,  // イーズアウト
    S_CURVE = 4,  // S字カーブ
};
```

---

## 8. 参照とリンク

### 8.1 参照タイプ

```c
enum class RefType : uint8_t {
    NONE     = 0x00,  // 参照なし
    LOCAL_ID = 0x01,  // ローカルID (デバイス内)
    UUID     = 0x02,  // UUID (グローバル)
    INLINE   = 0x03,  // インライン埋め込み
    EXTERNAL = 0x04,  // 外部ファイルパス
};

struct ResourceRef {
    uint8_t  ref_type;        // RefType
    uint8_t  resource_type;   // ResourceType
    uint16_t flags;           // bit0: required
    union {
        uint16_t local_id;    // LOCAL_ID
        uint8_t  uuid[16];    // UUID
        uint32_t inline_offset; // INLINE
    };
    uint16_t fallback_id;     // フォールバック先
};
```

### 8.2 参照解決

```
参照先が見つかる → 参照先を使用
参照先が見つからない → フォールバック動作
  - Pattern: デフォルトサウンドで再生
  - Preset: 内蔵波形を使用
  - Sample: 無音
```

---

## 9. 変換と互換性

### 9.1 サイズ比較

```
16ステップ × 8トラック パターン:

従来の固定フォーマット:
  MINIMAL (2 bytes):   256 bytes
  STANDARD (4 bytes):  512 bytes
  MELODIC (6 bytes):   768 bytes
  EXTENDED (8 bytes): 1024 bytes
  FULL (12 bytes):    1536 bytes

統一フォーマット:
  DRUM_MINIMAL (2 params):   16 × 8 × 2 + 2 × 8  =  272 bytes
  DRUM_STANDARD (4 params):  16 × 8 × 4 + 4 × 8  =  544 bytes
  MELODY_SPARSE (4 notes):   4 × 5 + 4 × 8       =   52 bytes (★最小)
  TRACKER_STANDARD (9 params): 16 × 8 × 9 + 9 × 8 = 1224 bytes

メリット:
  - 疎表現で大幅なサイズ削減
  - 必要なパラメータのみを含めるため無駄がない
  - 定義テーブルのオーバーヘッドは微小
```

### 9.2 相互変換

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         相互変換マトリクス                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  【密 → 疎】                                                                │
│    1. 値がデフォルト以外のステップを抽出                                    │
│    2. POSITIONパラメータを追加                                              │
│    3. 抽出したステップのみを格納                                            │
│                                                                             │
│  【疎 → 密】                                                                │
│    1. 全ステップ分の配列を確保                                              │
│    2. デフォルト値で初期化                                                  │
│    3. POSITIONに従って値を配置                                              │
│    4. POSITIONパラメータを削除                                              │
│                                                                             │
│  【異なるパラメータセット間】                                               │
│    1. 共通パラメータはそのまま変換                                          │
│    2. 存在しないパラメータはデフォルト値を使用                              │
│    3. 余分なパラメータは破棄（警告を出力）                                  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 9.3 他フォーマットとの互換

| フォーマット | 変換方法 |
|-------------|---------|
| Standard MIDI | NOTE/VEL/GATE をMIDI Note On/Off に変換 |
| LSDJ | TRACKER_MINIMAL + Chain/Phrase構造 |
| M8 Tracker | TRACKER_STANDARD |
| Elektron | DRUM_EXTENDED + P-Lock セクション |
| Ableton Clip | MELODY_SPARSE + Automation |

---

## 付録A: パラメータID定義 (C++コード)

```c++
namespace ParamId {
    // =====================================
    // MIDI CC (0x00-0x7F) ★直接マッピング
    // =====================================
    // id < 0x80 の場合、そのままCC番号として送信可能
    constexpr uint16_t CC_MOD_WHEEL      = 0x0001;  // CC#1
    constexpr uint16_t CC_VOLUME         = 0x0007;  // CC#7
    constexpr uint16_t CC_PAN            = 0x000A;  // CC#10
    constexpr uint16_t CC_SUSTAIN        = 0x0040;  // CC#64
    constexpr uint16_t CC_CUTOFF         = 0x0047;  // CC#71
    constexpr uint16_t CC_RESONANCE      = 0x0048;  // CC#72

    // =====================================
    // MIDI Voice (0x80-0xFF)
    // =====================================
    constexpr uint16_t NOTE              = 0x0080;
    constexpr uint16_t VELOCITY          = 0x0081;
    constexpr uint16_t PRESSURE          = 0x0082;
    constexpr uint16_t PITCH_BEND        = 0x0083;  // 14bit, extended=1
    constexpr uint16_t PROGRAM           = 0x0084;
    constexpr uint16_t CHANNEL           = 0x0085;

    // =====================================
    // RPN/NRPN (0x01xx-0x5FFF)
    // =====================================
    constexpr uint16_t RPN_BASE          = 0x0100;
    constexpr uint16_t NRPN_BASE         = 0x0200;

    // =====================================
    // Step Sequencer (0x8xxx)
    // =====================================
    constexpr uint16_t POSITION          = 0x8000;
    constexpr uint16_t GATE              = 0x8001;
    constexpr uint16_t TRIGGER           = 0x8002;
    constexpr uint16_t MICRO_TIMING      = 0x8003;
    constexpr uint16_t PROBABILITY       = 0x8004;
    constexpr uint16_t CONDITION         = 0x8005;
    constexpr uint16_t RETRIG_COUNT      = 0x8006;
    constexpr uint16_t RETRIG_RATE       = 0x8007;

    // =====================================
    // Synth (0x9xxx), Sample (0xAxxx), Tracker (0xBxxx), Automation (0xCxxx)
    // =====================================
    constexpr uint16_t OSC_WAVEFORM      = 0x9000;
    constexpr uint16_t FILTER_CUTOFF     = 0x9010;
    constexpr uint16_t ENV_ATTACK        = 0x9020;
    constexpr uint16_t INSTRUMENT        = 0xB000;
    constexpr uint16_t TIME              = 0xC000;

    // =====================================
    // Vendor / Special
    // =====================================
    constexpr uint16_t VENDOR_BASE       = 0xF000;
    constexpr uint16_t INVALID           = 0xFFFF;
}
```

### A.1 パラメータID詳細一覧

#### MIDI CC (0x00-0x7F)

| ID | 名前 | 範囲 | 説明 |
|----|------|------|------|
| 0x00-0x7F | CC_* | 0-127 | CC番号そのまま使用 |

#### MIDI Voice (0x80-0xFF)

| ID | 名前 | 範囲 | 説明 |
|----|------|------|------|
| 0x80 | NOTE | 0-127 | MIDIノート |
| 0x81 | VELOCITY | 0-127 | ベロシティ |
| 0x82 | PRESSURE | 0-127 | アフタータッチ |
| 0x83 | PITCH_BEND | ±8191 | ピッチベンド (extended=1) |
| 0x84 | PROGRAM | 0-127 | プログラムチェンジ |

#### Step Sequencer (0x8xxx)

| ID | 名前 | 範囲 | 説明 |
|----|------|------|------|
| 0x8000 | POSITION | 0-65535 | ステップ位置 (疎表現用) |
| 0x8001 | GATE | 0-127 | ゲート長 (%) |
| 0x8002 | TRIGGER | 0-1 | トリガーフラグ |
| 0x8003 | MICRO_TIMING | ±63 | マイクロタイミング |
| 0x8004 | PROBABILITY | 0-127 | 確率 (%) |
| 0x8005 | CONDITION | 0-255 | 条件トリガー |
| 0x8006 | RETRIG_COUNT | 0-16 | リトリガー回数 |

#### Synth (0x9xxx) / Tracker (0xBxxx) / Automation (0xCxxx)

| ID | 名前 | 範囲 | 説明 |
|----|------|------|------|
| 0x9000 | OSC_WAVEFORM | 0-7 | 波形 |
| 0x9010 | FILTER_CUTOFF | 0-127 | カットオフ |
| 0x9020-23 | ENV_ADSR | 0-999 | エンベロープ |
| 0xB000 | INSTRUMENT | 0-255 | インストゥルメント |
| 0xB010-15 | FX_CMD/VAL | 0-255 | エフェクト |
| 0xC000 | TIME | 0-65535 | 時間位置 |

#### Vendor (0xFxxx)

| ID | 名前 | 説明 |
|----|------|------|
| 0xF000-0xFFFE | VENDOR_* | ベンダー固有 (登録不要) |
| 0xFFFF | INVALID | 無効/未定義 |

---

## 付録B: 標準定義一覧

| ID | 名前 | パラメータ数 | 用途 |
|----|------|-------------|------|
| 0x00 | CUSTOM | 可変 | カスタム定義 (ParamDef配列必須) |
| 0x01 | DRUM_MINIMAL | 2 | 最小ドラム |
| 0x02 | DRUM_STANDARD | 4 | 標準ドラム |
| 0x03 | DRUM_EXTENDED | 6 | 拡張ドラム |
| 0x10 | MELODY_MINIMAL | 2 | 最小メロディ (密) |
| 0x11 | MELODY_STANDARD | 4 | 標準メロディ (密) |
| 0x12 | MELODY_SPARSE | 4 | メロディ (疎) |
| 0x20 | TRACKER_MINIMAL | 4 | LSDJ互換 |
| 0x21 | TRACKER_STANDARD | 9 | M8互換 |
| 0x30 | SYNTH_BASIC | 10 | 基本シンセ |
| 0x40 | AUTOMATION_BASIC | 4 | オートメーション |
| 0x50 | SAMPLE_BASIC | 3 | サンプル再生 (基本) |
| 0x51 | SAMPLE_EXTENDED | 6 | サンプル再生 (拡張) |
| 0x52 | CLIP_BASIC | 4 | クリップ再生 |

---

## 付録C: ベンダー拡張実装ガイド

### 送信側の実装

```c
// 1. VendorExtensionを作成
VendorExtension ext;
ext.vendor_name_len = strlen("MyCompany");
ext.product_name_len = strlen("MySynth");
ext.param_count = 2;
// followed by: "MyCompany" "MySynth"

// 2. ベンダーパラメータを定義
VendorParamDef params[] = {
    {0x001, 1, 0, 0, 127, 0, strlen("Warmth")},  // "Warmth"
    {0x002, 1, 0, 0, 127, 64, strlen("Drive")},  // "Drive"
};

// 3. ParamDefでベンダーパラメータを参照
// {id, flags, reserved, min, max, default_val}
ParamDef pattern_params[] = {
    {0x0080, 0, 0, 0, 127, 60},   // NOTE (0x80)
    {0x0081, 0, 0, 0, 127, 100},  // VELOCITY (0x81)
    {0xF001, 0, 0, 0, 127, 0},    // Warmth (vendor)
    {0xF002, 0, 0, 0, 127, 64},   // Drive (vendor)
};
```

### 受信側の実装

```c
// ベンダーパラメータの処理 (自動マッチングなし)
void process_vendor_param(uint16_t id, int16_t value,
                          const VendorExtension* ext) {
    if (id < 0xF000) {
        // 標準パラメータ: 通常処理
        apply_param(id, value);
        return;
    }

    // ベンダーパラメータ: 情報を取得して表示
    uint16_t local_id = id & 0x0FFF;
    const VendorParamDef* def = find_vendor_param(ext, local_id);

    if (def) {
        // UIに情報を表示
        show_unknown_param_info(def->name, def->min, def->max,
                                def->default_val, ext->vendor_name);

        // 処理オプション:
        // 1. デフォルト値を使用
        // 2. ユーザーのマッピング設定があれば適用
        // 3. 値を保持（再エクスポート用）
    }
}
```

---

## 付録D: 参考文献

1. MIDI 2.0 Specification (MMA/AMEI)
2. MIDI-CI Specification 1.2
3. USB MIDI 2.0 Specification
4. Elektron Pattern Format (リバースエンジニアリング)
5. M8 Tracker Documentation
6. LSDJ Manual
7. XM/IT Module Format Specifications
