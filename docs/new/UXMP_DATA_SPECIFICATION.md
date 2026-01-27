# UXMP-DATA User Data Exchange 詳細仕様書

バージョン: 0.6.0 (Draft)
ステータス: 設計段階
最終更新: 2026-01-28

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
9. **厳密なバイナリ規約** - エンディアン、アラインメント、文字列を明文化

### 1.2 バイナリ表現規約 (★実装必須)

本仕様のすべてのバイナリデータは以下の規約に従う。

| 項目 | 規約 |
|------|------|
| エンディアン | **リトルエンディアン** (x86/ARM互換) |
| アラインメント | **パックド構造体** (1バイト境界、パディングなし) |
| 文字列 | **UTF-8 NFC正規化**、長さプレフィックス付き、NULL終端なし |
| 整数型 | 符号なし: uint8/16/32、符号付き: int8/16/32 |
| CRC32 | **IEEE 802.3** (zlib互換)、下記CRC計算規則参照 |

```c
// コンパイラ指示 (パックド構造体)
#pragma pack(push, 1)

// 16bit値のバイト順序例
uint16_t value = 0x1234;
// メモリ: [0x34, 0x12] (リトルエンディアン)

// 文字列の格納例: "Hello"
// length: 5 (uint8_t)
// data: 'H', 'e', 'l', 'l', 'o' (NULL終端なし)

#pragma pack(pop)
```

**CRC32計算規則 (★実装必須):**

全フォーマットで統一された方式を使用する。

1. **対象範囲**: ファイル先頭 (magic) から `total_size` バイト全体
2. **CRCフィールドの扱い**: 計算時は `crc32` フィールドを **0x00000000** として計算
3. **アルゴリズム**: IEEE 802.3 (zlib互換、初期値0xFFFFFFFF、最終XOR 0xFFFFFFFF)

```c
// CRC32計算例
uint32_t calculate_crc32(const uint8_t* data, uint32_t total_size,
                         uint32_t crc_field_offset) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < total_size; i++) {
        uint8_t byte = data[i];
        // CRCフィールド位置 (4バイト) は0として計算
        if (i >= crc_field_offset && i < crc_field_offset + 4) {
            byte = 0x00;
        }
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}
```

**バージョン互換ポリシー:**
- メジャーバージョン変更: 後方互換性なし
- マイナーバージョン変更: 後方互換あり（新フィールドは末尾追加）
- 未知のフラグ/フィールドは無視して処理を継続

### 1.2.1 受信側検証要件 (★実装必須)

受信側は以下の検証を**必ず**実行すること。検証失敗時はデータを拒否する。

| 検証項目 | 条件 |
|---------|------|
| Magic | 期待値と完全一致 |
| total_size | 実受信サイズ以下 |
| data_offset | < total_size |
| vendor_offset | 0 または < total_size |
| param_count × sizeof(ParamDef) | data_offset以内に収まる |
| entry_count × param_size | total_size - data_offset 以内 |
| 文字列長 | 残りバイト数を超えない |
| CRC32 | 計算値と一致 |

```c
// 検証例
bool validate_header(const DataBlockHeader* h, size_t received_size) {
    // Magic確認
    if (memcmp(h->magic, "UXDB", 4) != 0) return false;
    // サイズ境界確認
    if (h->total_size > received_size) return false;
    if (h->data_offset >= h->total_size) return false;
    if (h->vendor_offset != 0 && h->vendor_offset >= h->total_size) return false;
    // ParamDef配列がdata_offsetより前に収まるか
    size_t param_table_end = sizeof(DataBlockHeader) + h->param_count * sizeof(ParamDef);
    if (param_table_end > h->data_offset) return false;
    // CRC検証
    uint32_t crc_offset = offsetof(DataBlockHeader, /* crc位置 */);
    if (calculate_crc32((uint8_t*)h, h->total_size, crc_offset) != h->crc32) return false;
    return true;
}
```

### 1.3 統一データブロック構造 (★核心)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    統一データブロック構造                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  すべてのユーザーデータ（パターン、プリセット、オートメーション等）が         │
│  同じ基本構造を共有する。                                                    │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │  DataBlock Header (24バイト固定)                                     │  │
│  │    magic[4]       : uint8×4 // "UXDB"                               │  │
│  │    block_type     : uint8   // データ種別                           │  │
│  │    param_count    : uint8   // パラメータ数                          │  │
│  │    entry_count    : uint16  // エントリ数                            │  │
│  │    flags          : uint16  // フラグ                                │  │
│  │    version        : uint16  // フォーマットバージョン                │  │
│  │    data_offset    : uint32  // データセクションへのオフセット        │  │
│  │    vendor_offset  : uint32  // ベンダー拡張へのオフセット (0=なし)  │  │
│  │    total_size     : uint32  // 全体サイズ                            │  │
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

### 1.4 構造体定義

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

// ParamDef.flags ビット定義
// bit0: optional - 互換性用ヒント (下記参照)
// bit1: signed   - 符号付き値 (min/maxの解釈に影響)
// bit2: extended - 16bit拡張 (0=8bit, 1=16bit)
// bit3-7: 予約

// ★optional フラグの意味 (★実装必須):
// optionalは「ParamDef自体を定義から省略してもよい」というヒント。
// エントリ単位での欠損は許可しない。Data Sectionは常に固定サイズ配列。
//
// 用途:
//   - 送信側: optional=1のパラメータはParamDef配列から省略可能
//   - 受信側: 受信したParamDefに無いパラメータはデフォルト値を使用
//   - 後方互換: 旧バージョンで未知のパラメータをoptional=1で定義
//
// 禁止事項:
//   - エントリ単位で値を欠損させることは不可
//   - すべてのエントリは param_count × entry_count の固定配列

// データサイズは flags.extended で決定:
//   extended=0 → 8bit (1バイト)
//   extended=1 → 16bit (2バイト)

// ★値型境界条件 (★実装必須):
// ParamDefの min/max/default_val は以下の制約を満たすこと。
// 違反した場合、受信側はデータを拒否してよい。
//
// | extended | signed | min範囲      | max範囲      | default_val |
// |----------|--------|--------------|--------------|-------------|
// | 0        | 0      | [0, 255]     | [0, 255]     | [min, max]  |
// | 0        | 1      | [-128, 127]  | [-128, 127]  | [min, max]  |
// | 1        | 0      | [0, 65535]   | [0, 65535]   | [min, max]  |
// | 1        | 1      | [-32768, 32767] | [-32768, 32767] | [min, max] |
//
// 追加制約:
//   - min <= max (等しい場合は固定値)
//   - default_val は必ず [min, max] の範囲内

// ★値の格納規則:
// - min/max/default_val は常に int16_t で定義
// - 8bitデータ格納時:
//   - 符号なし (signed=0): 値を uint8_t にキャスト (0-255)
//   - 符号付き (signed=1): 値を int8_t にキャスト (-128..+127)
//   - 範囲外の場合はクランプ (min/maxで制限)
// - 16bitデータ格納時: そのまま int16_t/uint16_t で格納
//
// 例: MICRO_TIMING (signed=1, extended=0, min=-64, max=+63)
//     値 -10 → int8_t として 0xF6 を格納

// ★検証コード例:
bool validate_param_def(const ParamDef& p) {
    bool is_signed = (p.flags & 0x02) != 0;
    bool is_extended = (p.flags & 0x04) != 0;

    int32_t type_min, type_max;
    if (is_extended) {
        type_min = is_signed ? -32768 : 0;
        type_max = is_signed ? 32767 : 65535;
    } else {
        type_min = is_signed ? -128 : 0;
        type_max = is_signed ? 127 : 255;
    }

    // 範囲チェック
    if (p.min < type_min || p.min > type_max) return false;
    if (p.max < type_min || p.max > type_max) return false;
    if (p.min > p.max) return false;
    if (p.default_val < p.min || p.default_val > p.max) return false;

    return true;
}

// データブロックヘッダー
struct DataBlockHeader {
    uint8_t  magic[4];       // "UXDB" (UXmp Data Block)
    uint8_t  block_type;     // BlockType
    uint8_t  param_count;    // パラメータ定義数
    uint16_t entry_count;    // エントリ数
    uint16_t flags;          // 下記参照
    uint16_t version;        // フォーマットバージョン
    uint32_t data_offset;    // データセクションへのオフセット
    uint32_t vendor_offset;  // ベンダー拡張へのオフセット (0=なし)
    uint32_t total_size;     // 全体サイズ
    // ParamDef defs[param_count] follows
    // VendorExtension follows at vendor_offset (if has_vendor_ext)
    // uint8_t data[] follows at data_offset
};

// ★flags ビット定義 (DataBlockHeader / TrackHeader 共通)
//
// | bit | DataBlockHeader | TrackHeader | 説明 |
// |-----|-----------------|-------------|------|
// | 0   | 予約 (0)        | muted       | ミュート |
// | 1   | 予約 (0)        | solo        | ソロ |
// | 2   | sparse          | sparse      | 疎表現 (★下記整合条件参照) |
// | 3   | compressed      | compressed  | 圧縮 (将来拡張) |
// | 4   | has_vendor_ext  | has_vendor_ext | ベンダー拡張あり |
// | 5-15| 予約 (0)        | 予約 (0)    | 将来用 |
//
// ★sparse フラグと POSITION の整合条件 (必須):
//   sparse=1 の場合: ParamDef[0].id == 0x8000 (POSITION) 必須
//   sparse=0 の場合: ParamDef[0].id != 0x8000 必須
//   整合しない場合、受信側はデータを拒否する
//
// ★vendor_offset と has_vendor_ext の整合条件 (必須):
//   has_vendor_ext=1 の場合: vendor_offset != 0 必須
//   has_vendor_ext=0 の場合: vendor_offset == 0 必須
//   整合しない場合、受信側はデータを拒否する
//
// 注: TrackHeader使用時はDataBlockHeader省略可能。
//     その場合TrackHeader.flagsがDataBlockHeader.flagsの役割を果たす。
//     DataBlockHeader未使用時、bit0-1は実質無視される。
```

### 1.5 密表現と疎表現

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

#### 1.5.1 疎表現の厳密規則 (★実装必須)

疎表現 (sparse=1) を使用する場合、以下の規則に**必ず**従うこと。

| 項目 | 規則 |
|------|------|
| POSITION配置 | ParamDef配列の**先頭** (index 0) に必須 |
| POSITION型 | `id=0x8000`, `extended=1` (uint16_t), `signed=0` |
| POSITION範囲 | [0, length_steps - 1] |
| 順序 | エントリはPOSITION**昇順**で格納 |
| 重複 | 同一POSITIONの複数エントリを**許可** |
| flags整合性 | `flags.sparse=1` と `ParamDef[0].id=0x8000` は必ず一致 |

```c
// 疎表現の検証
bool validate_sparse(const TrackHeader* t, const ParamDef* defs) {
    bool is_sparse = (t->flags & 0x04) != 0;

    if (is_sparse) {
        // POSITIONが先頭に必須
        if (defs[0].id != 0x8000) return false;
        // POSITIONはuint16 (extended=1, signed=0)
        if ((defs[0].flags & 0x06) != 0x04) return false;
    } else {
        // 密表現ではPOSITIONが先頭にあってはならない
        if (defs[0].id == 0x8000) return false;
    }
    return true;
}

// エントリ順序の検証
bool validate_sparse_order(const uint8_t* data, uint32_t entry_count,
                           uint32_t param_size) {
    uint16_t prev_pos = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        // POSITIONは先頭2バイト (little-endian)
        uint16_t pos = data[i * param_size] | (data[i * param_size + 1] << 8);
        if (i > 0 && pos < prev_pos) return false;  // 昇順違反
        prev_pos = pos;
    }
    return true;
}
```

**同一POSITION複数エントリの用途:**
- 和音 (同じステップに複数ノート)
- ドラムの同時発音 (キック + ハイハット)
- 受信側は入力順を保持するが、安定ソートで処理してもよい

### 1.6 ドキュメント構成

| 章 | 内容 |
|----|------|
| 2 | パラメータID名前空間 |
| 3 | ベンダー拡張 |
| 4 | 標準パラメータ定義テンプレート |
| 5 | パターン仕様 |
| 6 | プリセット仕様 |
| 7 | オートメーション仕様 |
| 8 | 参照とリンク |
| 9 | 変換と互換性 |
| 10 | MIDIマッピング仕様 |
| 11 | JSON表現 (UXMP-JSON) |

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
| 0xF000-0xFFFE | Vendor | ベンダー固有 (登録不要) |
| 0xFFFF | INVALID | 無効/未定義 |

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
    uint16_t local_id;        // ローカルID (0x0000-0x0FFE) ★0x0FFFは禁止
                              // データ内でのIDは 0xF000 | local_id
                              // 注: 0x0FFF は 0xFFFF (INVALID) と衝突するため禁止
    uint16_t fallback_id;     // フォールバック先ID (0xFFFF=なし/無視)
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

### 3.5 ParamDef と VendorExtension の関係 (★実装必須)

ParamDef配列にベンダーID (0xF000-0xFFFE) を使用する場合の規則。

| 条件 | 動作 |
|------|------|
| ParamDefに0xFxxxがあり、VendorExtensionに対応するVendorParamDefがある | VendorParamDefの情報 (名前、範囲) を表示に使用 |
| ParamDefに0xFxxxがあり、VendorExtensionに対応がない | 受信側は値を保持するが、名前は "Unknown (0xFxxx)" と表示 |
| VendorExtensionにVendorParamDefがあり、ParamDefに対応がない | VendorParamDefは無視される (表示用メタデータのみ) |

```c
// 検証: ParamDefのベンダーIDがVendorExtensionに存在するか
const char* get_vendor_param_name(uint16_t param_id,
                                   const VendorExtension* ext) {
    if (param_id < 0xF000 || param_id > 0xFFFE) return NULL;
    if (ext == NULL) return "Unknown";

    uint16_t local_id = param_id & 0x0FFF;
    // VendorParamDef配列を検索
    for (uint16_t i = 0; i < ext->param_count; i++) {
        if (ext->params[i].local_id == local_id) {
            return ext->params[i].name;  // 名前を返す
        }
    }
    return "Unknown";  // 対応なし
}
```

**設計意図:**
- VendorExtensionは**表示用メタデータ**であり、データ処理の必須条件ではない
- ParamDefにベンダーIDがあれば、VendorExtensionが無くてもデータは有効
- 受信側はベンダー値を保持し、再エクスポート時に復元可能にすること

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

// TRACKER_FULL: 拡張トラッカー (12パラメータ)
// データサイズ: 12バイト/ステップ
const ParamDef TRACKER_FULL[] = {
    {0x0080, 0, 0, 0, 127, 0xFF},  // NOTE (0x80)
    {0xB001, 0, 0, 0, 127, 0xFF},  // VOLUME (0xB001)
    {0xB000, 0, 0, 0, 127, 0xFF},  // INSTRUMENT (0xB000)
    {0xB010, 0, 0, 0, 255, 0},     // FX1_CMD
    {0xB011, 0, 0, 0, 255, 0},     // FX1_VAL
    {0xB012, 0, 0, 0, 255, 0},     // FX2_CMD
    {0xB013, 0, 0, 0, 255, 0},     // FX2_VAL
    {0xB014, 0, 0, 0, 255, 0},     // FX3_CMD
    {0xB015, 0, 0, 0, 255, 0},     // FX3_VAL
    {0x8004, 0, 0, 0, 127, 127},   // PROBABILITY (0x8004)
    {0x8003, 0x02, 0, -64, 63, 0}, // MICRO_TIMING (0x8003): signed
    {0x8005, 0, 0, 0, 255, 0},     // CONDITION (0x8005)
};
```

#### 4.1.4 拡張定義

```c
// MELODY_FULL: 完全メロディ (密表現、8パラメータ)
// データサイズ: 8バイト/ステップ
const ParamDef MELODY_FULL[] = {
    {0x0080, 0,    0, 0,   127, 60},   // NOTE (0x80)
    {0x0081, 0,    0, 0,   127, 100},  // VELOCITY (0x81)
    {0x8001, 0,    0, 0,   127, 64},   // GATE (0x8001)
    {0x8003, 0x02, 0, -64, 63,  0},    // MICRO_TIMING (0x8003): signed
    {0x8004, 0,    0, 0,   127, 127},  // PROBABILITY (0x8004)
    {0x8005, 0,    0, 0,   255, 0},    // CONDITION (0x8005)
    {0x0083, 0x04, 0, -8192, 8191, 0}, // PITCH_BEND (0x83): extended=1
    {0x0001, 0,    0, 0,   127, 0},    // CC#1 MOD_WHEEL
};

// SYNTH_FULL: 完全シンセ (16パラメータ)
// データサイズ: 可変 (8bit×12 + 16bit×4 = 20バイト)
const ParamDef SYNTH_FULL[] = {
    {0x9000, 0,    0, 0, 7,     0},    // OSC_WAVEFORM
    {0x9001, 0,    0, 0, 127,   64},   // OSC_PITCH
    {0x9002, 0,    0, 0, 127,   0},    // OSC_DETUNE
    {0x9003, 0,    0, 0, 127,   64},   // OSC_MIX
    {0x9010, 0,    0, 0, 127,   100},  // FILTER_CUTOFF
    {0x9011, 0,    0, 0, 127,   0},    // FILTER_RESO
    {0x9012, 0,    0, 0, 7,     0},    // FILTER_TYPE
    {0x9013, 0,    0, 0, 127,   0},    // FILTER_ENV_AMT
    {0x9020, 0x04, 0, 0, 999,   10},   // ENV_ATTACK: extended=1
    {0x9021, 0x04, 0, 0, 999,   100},  // ENV_DECAY: extended=1
    {0x9022, 0,    0, 0, 127,   80},   // ENV_SUSTAIN
    {0x9023, 0x04, 0, 0, 999,   200},  // ENV_RELEASE: extended=1
    {0x9030, 0,    0, 0, 127,   32},   // LFO_RATE
    {0x9031, 0,    0, 0, 127,   0},    // LFO_DEPTH
    {0x9032, 0,    0, 0, 7,     0},    // LFO_WAVEFORM
    {0x9033, 0,    0, 0, 127,   0},    // LFO_TARGET
};

// SAMPLE_BASIC: サンプル再生 (3パラメータ)
// データサイズ: 3バイト/エントリ
const ParamDef SAMPLE_BASIC[] = {
    {0xA000, 0, 0, 0, 255, 0},    // SAMPLE_ID
    {0x0081, 0, 0, 0, 127, 100},  // VELOCITY
    {0x0080, 0, 0, 0, 127, 60},   // NOTE (ピッチ)
};

// SAMPLE_EXTENDED: サンプル拡張 (6パラメータ)
// データサイズ: 8バイト/エントリ (8bit×4 + 16bit×2)
const ParamDef SAMPLE_EXTENDED[] = {
    {0xA000, 0,    0, 0, 255,   0},     // SAMPLE_ID
    {0x0081, 0,    0, 0, 127,   100},   // VELOCITY
    {0x0080, 0,    0, 0, 127,   60},    // NOTE
    {0x8001, 0,    0, 0, 127,   64},    // GATE
    {0xA001, 0x04, 0, 0, 65535, 0},     // START_OFFSET: extended=1
    {0xA002, 0x04, 0, 0, 65535, 65535}, // END_OFFSET: extended=1
};

// CLIP_BASIC: クリップ再生 (4パラメータ)
// データサイズ: 6バイト/エントリ (8bit×2 + 16bit×2)
const ParamDef CLIP_BASIC[] = {
    {0x8000, 0x04, 0, 0, 65535, 0},  // POSITION: extended=1
    {0xA010, 0,    0, 0, 255,   0},  // CLIP_ID
    {0x0081, 0,    0, 0, 127,   100},// VELOCITY
    {0xA011, 0x04, 0, 0, 65535, 0},  // CLIP_OFFSET: extended=1
};

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
    uint16_t flags;             // bit0: muted, bit1: solo, bit2: sparse
    uint16_t length_steps;      // パターン全体のステップ数 (論理長)
    uint16_t entry_count;       // 実際のデータエントリ数
                                // 密表現: entry_count == length_steps
                                // 疎表現: entry_count <= length_steps
    uint16_t reserved;          // 予約
    uint32_t data_offset;       // DataBlock へのオフセット
    uint8_t  time_scale;        // クロック倍率 (0x40=1x)
    uint8_t  direction;         // PlayDirection
    uint8_t  param_count;       // パラメータ数 (standard_def_id=0時に参照)
    uint8_t  reserved2;         // 予約
};
// sizeof(TrackHeader) == 20

// flags ビット定義 (★DataBlockHeader.flagsと統一):
// bit0: muted          - ミュート (トラック固有)
// bit1: solo           - ソロ (トラック固有)
// bit2: sparse         - 疎表現 (= has_position, POSITIONパラメータあり)
// bit3: compressed     - 圧縮 (将来拡張)
// bit4: has_vendor_ext - ベンダー拡張あり (カスタム定義時のみ有効)
// bit5-15: 予約

// 注: standard_def_id != CUSTOM の場合、TrackHeader.flags が
//     DataBlockHeader.flags の代わりとなる

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

**標準定義使用時のデータ構造:**

| standard_def_id | ParamDef配列 | DataBlockHeader | 備考 |
|----------------|--------------|-----------------|------|
| != CUSTOM | 省略 | 省略 | TrackHeaderで情報を持つ |
| == CUSTOM | 必須 | 必須 | 完全なDataBlock構造 |

```c
// ★標準定義使用時 (standard_def_id != CUSTOM):
//   TrackHeader が entry_count, param_count, flags(sparse) を持つ
//   data_offset から直接データが始まる (ヘッダー/定義なし)
//
//   メモリレイアウト:
//   [TrackHeader] → [Data Section: entry_count × param_size bytes]

// ★カスタム定義使用時 (standard_def_id == CUSTOM):
//   data_offset に DataBlockHeader が配置される
//
//   メモリレイアウト:
//   [TrackHeader] → [DataBlockHeader] → [ParamDef × param_count] → [Data]

// 例: DRUM_STANDARD を使用する16ステップトラック
TrackHeader track = {
    .standard_def_id = DRUM_STANDARD,
    .flags = 0,                // 密表現
    .length_steps = 16,        // パターン長
    .entry_count = 16,         // = length_steps (密表現)
    .param_count = 4,          // DRUM_STANDARDは4パラメータ
};

// Data Section (16 × 4 = 64 bytes):
// step 0:  [36, 100, 64, 0]   // kick, vel=100, gate=50%, timing=0
// step 1:  [0,  0,   0,  0]   // rest
// step 2:  [38, 80,  64, 0]   // snare
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
    .flags = 0x04,           // sparse=1
    .length_steps = 64,      // パターン全体のステップ数
    .entry_count = 4,        // 実際のデータエントリ数
};

// [POSITION(2byte, little-endian), NOTE, VEL, GATE] × 4
// POSITION は uint16_t リトルエンディアン
uint8_t melody_data[] = {
    0x00, 0x00, 60, 100, 64,   // step 0: C4  (POSITION=0x0000)
    0x10, 0x00, 64, 90,  64,   // step 16: E4 (POSITION=0x0010)
    0x20, 0x00, 67, 85,  64,   // step 32: G4 (POSITION=0x0020)
    0x30, 0x00, 72, 80,  64,   // step 48: C5 (POSITION=0x0030)
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

## 10. MIDIマッピング仕様 (UXMP-MAP)

MIDI Learn機能で作成したパラメータマッピングを標準化し、デバイス間で共有可能にする。

### 10.1 マッピング構造

```c
struct MappingHeader {
    uint8_t  magic[4];          // "UXMP"
    uint16_t version;           // フォーマットバージョン
    uint16_t mapping_count;     // マッピング数
    uint16_t flags;             // bit0: bidirectional
    uint16_t reserved;
    // MappingEntry entries[mapping_count] follows
};

struct MappingEntry {
    // ソース (MIDIコントローラー側)
    uint8_t  src_channel;       // MIDIチャンネル (0-15, 0xFF=any)
    uint8_t  src_type;          // MappingSourceType
    uint16_t src_param;         // CC番号/NRPN番号等

    // ターゲット (内部パラメータ)
    uint16_t dst_param;         // ターゲットパラメータID
    uint8_t  dst_track;         // トラック番号 (0xFF=global)
    uint8_t  flags;             // bit0: invert, bit1: relative

    // 値変換
    int16_t  src_min;           // ソース範囲
    int16_t  src_max;
    int16_t  dst_min;           // ターゲット範囲
    int16_t  dst_max;
};

// ★MappingEntry.flags ビット定義:
// bit0: invert   - 値を反転 (dst_max - (result - dst_min))
// bit1: relative - 相対値モード (下記参照)
// bit2-7: 予約 (0)

// ★値変換規則 (★実装必須):
//
// 1. src_min == src_max の場合:
//    - 無効なマッピング、受信側は無視してよい
//    - 送信側はこの状態を生成しないこと
//
// 2. relativeフラグ (bit1) の意味:
//    - relative=0: 絶対値モード (通常)
//      入力値をsrc範囲からdst範囲に線形マッピング
//    - relative=1: 相対値モード (エンコーダー向け)
//      入力値は2の補数デルタとして解釈 (0x40基準)
//      例: 入力0x41 → +1, 入力0x3F → -1
//      デルタをdst_min/dst_maxでクランプして現在値に加算
//
// 3. bidirectional (MappingHeader.flags bit0):
//    - bidirectional=1 の場合、dst→srcの逆マッピングも有効
//    - 衝突 (同一dstに複数src) 時は最初のエントリを優先

enum class MappingSourceType : uint8_t {
    CC          = 0x00,  // Control Change
    NOTE        = 0x01,  // Note On/Off
    VELOCITY    = 0x02,  // Note Velocity
    AFTERTOUCH  = 0x03,  // Channel Pressure
    POLY_AT     = 0x04,  // Polyphonic Aftertouch
    PITCH_BEND  = 0x05,  // Pitch Bend
    PROGRAM     = 0x06,  // Program Change
    RPN         = 0x10,  // RPN (14bit)
    NRPN        = 0x11,  // NRPN (14bit)
};
```

### 10.2 使用例

```c
// CC#1 (ModWheel) → フィルターカットオフ (全トラック)
MappingEntry mod_to_cutoff = {
    .src_channel = 0xFF,        // Any channel
    .src_type    = CC,
    .src_param   = 0x01,        // CC#1
    .dst_param   = 0x9010,      // FILTER_CUTOFF
    .dst_track   = 0xFF,        // Global
    .flags       = 0,
    .src_min = 0, .src_max = 127,
    .dst_min = 0, .dst_max = 127,
};

// CC#74 → LFO Rate (トラック0のみ、値反転)
MappingEntry cc74_to_lfo = {
    .src_channel = 0,
    .src_type    = CC,
    .src_param   = 0x4A,        // CC#74
    .dst_param   = 0x9030,      // LFO_RATE
    .dst_track   = 0,
    .flags       = 0x01,        // invert
    .src_min = 0, .src_max = 127,
    .dst_min = 127, .dst_max = 0,  // 反転
};

// NRPN#100 → ベンダーパラメータ
MappingEntry nrpn_to_vendor = {
    .src_channel = 0,
    .src_type    = NRPN,
    .src_param   = 100,
    .dst_param   = 0xF001,      // Vendor param
    .dst_track   = 0xFF,
    .flags       = 0,
    .src_min = 0, .src_max = 16383,
    .dst_min = 0, .dst_max = 127,
};
```

### 10.3 マッピング処理

```c
void process_midi_input(uint8_t channel, uint8_t type,
                        uint16_t param, uint16_t value) {
    for (const auto& map : mappings) {
        // チャンネルマッチ
        if (map.src_channel != 0xFF && map.src_channel != channel)
            continue;
        // タイプ・パラメータマッチ
        if (map.src_type != type || map.src_param != param)
            continue;

        // 値変換 (線形補間)
        int32_t normalized = (value - map.src_min) * 65536
                           / (map.src_max - map.src_min);
        int16_t dst_value = map.dst_min
                          + normalized * (map.dst_max - map.dst_min) / 65536;

        // 反転フラグ
        if (map.flags & 0x01)
            dst_value = map.dst_max - (dst_value - map.dst_min);

        // パラメータ適用
        apply_param(map.dst_track, map.dst_param, dst_value);
    }
}
```

### 10.4 MIDI Learnフロー

```
┌─────────────────────────────────────────────────────────────────┐
│                    MIDI Learn フロー                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. ユーザーが「Learn」ボタンを押す                              │
│  2. ターゲットパラメータを選択 (UIで操作)                        │
│  3. MIDIコントローラーを操作                                     │
│  4. 受信したMIDIメッセージを記録:                                │
│       src_channel, src_type, src_param                          │
│  5. MappingEntryを生成:                                         │
│       - dst_param = 選択されたパラメータID                       │
│       - src/dst_min/max = パラメータの定義から取得               │
│  6. マッピングリストに追加                                       │
│                                                                 │
│  ★エクスポート時はMappingHeader + entries[]として保存           │
│  ★インポート時は既存マッピングとマージまたは置換                 │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 11. JSON表現 (UXMP-JSON)

Web/プラグイン環境でのデータ交換を容易にするためのJSON表現を定義する。

### 11.1 設計原則

1. **バイナリ⇔JSON 1:1対応** - 情報の欠落なく相互変換可能
2. **人間可読性** - デバッグ・手動編集が容易
3. **Web標準準拠** - JSON Schema、Base64 (RFC 4648)
4. **メタデータ拡張** - UUID、タイムスタンプ、作者情報

### 11.2 共通メタデータ

すべてのUXMP-JSONオブジェクトは以下のメタデータを持つことができる。

```json
{
  "$schema": "https://uxmp.dev/schema/v1/pattern.json",
  "uxmp_version": "1.0",
  "meta": {
    "uuid": "550e8400-e29b-41d4-a716-446655440000",
    "name": "Funky Beat",
    "author": "Artist Name",
    "created_at": "2026-01-28T12:00:00Z",
    "modified_at": "2026-01-28T15:30:00Z",
    "source": {
      "device": "GrooveBox X",
      "vendor": "TechnoMaker",
      "software": "UXMP Web Editor 1.0"
    },
    "tags": ["drum", "techno", "128bpm"]
  }
}
```

| フィールド | 型 | 必須 | 説明 |
|-----------|-----|------|------|
| $schema | string | 推奨 | JSON Schemaリファレンス |
| uxmp_version | string | 必須 | UXMP-JSONバージョン |
| meta.uuid | string | 推奨 | UUID v4 (RFC 4122) |
| meta.name | string | 任意 | 表示名 |
| meta.author | string | 任意 | 作者名 |
| meta.created_at | string | 任意 | ISO 8601形式 |
| meta.modified_at | string | 任意 | ISO 8601形式 |
| meta.source | object | 任意 | 作成元情報 |
| meta.tags | string[] | 任意 | 検索用タグ |

### 11.3 パターンJSON (UXMP-PATTERN)

```json
{
  "$schema": "https://uxmp.dev/schema/v1/pattern.json",
  "uxmp_version": "1.0",
  "type": "pattern",
  "meta": { "..." },
  "pattern": {
    "length": 16,
    "tempo": 120.00,
    "time_signature": [4, 4],
    "resolution": 24,
    "swing": 0,
    "tracks": [
      {
        "id": 0,
        "name": "Kick",
        "type": "drum",
        "channel": 9,
        "standard_def": "DRUM_STANDARD",
        "muted": false,
        "solo": false,
        "length_steps": 16,
        "params": ["NOTE", "VELOCITY", "GATE", "MICRO_TIMING"],
        "data": [
          [36, 100, 64, 0],
          [0, 0, 0, 0],
          [0, 0, 0, 0],
          [0, 0, 0, 0],
          [36, 100, 64, 0]
        ]
      },
      {
        "id": 1,
        "name": "Melody",
        "type": "melodic",
        "channel": 0,
        "standard_def": "MELODY_SPARSE",
        "sparse": true,
        "length_steps": 64,
        "params": ["POSITION", "NOTE", "VELOCITY", "GATE"],
        "data": [
          [0, 60, 100, 64],
          [16, 64, 90, 64],
          [32, 67, 85, 64],
          [48, 72, 80, 64]
        ]
      }
    ]
  }
}
```

#### パラメータ名対応表

JSON内ではパラメータIDの代わりに名前を使用可能。

| JSON名 | ID | 説明 |
|--------|-----|------|
| "NOTE" | 0x0080 | MIDIノート |
| "VELOCITY" | 0x0081 | ベロシティ |
| "GATE" | 0x8001 | ゲート長 |
| "POSITION" | 0x8000 | ステップ位置 |
| "MICRO_TIMING" | 0x8003 | マイクロタイミング |
| "PROBABILITY" | 0x8004 | 確率 |
| "CC:7" | 0x0007 | CC#7 (Volume) |
| "CC:71" | 0x0047 | CC#71 (Cutoff) |
| "VENDOR:0x001" | 0xF001 | ベンダーパラメータ |

### 11.4 プリセットJSON (UXMP-PRESET)

```json
{
  "$schema": "https://uxmp.dev/schema/v1/preset.json",
  "uxmp_version": "1.0",
  "type": "preset",
  "meta": { "..." },
  "preset": {
    "device_family": 1,
    "device_model": 2,
    "standard_def": "SYNTH_BASIC",
    "params": {
      "OSC_WAVEFORM": 2,
      "OSC_PITCH": 64,
      "FILTER_CUTOFF": 100,
      "FILTER_RESO": 20,
      "ENV_ATTACK": 10,
      "ENV_DECAY": 100,
      "ENV_SUSTAIN": 80,
      "ENV_RELEASE": 200,
      "LFO_RATE": 32,
      "LFO_DEPTH": 0
    },
    "vendor_params": {
      "vendor": "TechnoMaker",
      "product": "SynthX",
      "params": {
        "Warmth": { "value": 64, "fallback": "CC:71" },
        "Drive": { "value": 32, "fallback": null }
      }
    }
  }
}
```

### 11.5 マッピングJSON (UXMP-MAP)

```json
{
  "$schema": "https://uxmp.dev/schema/v1/mapping.json",
  "uxmp_version": "1.0",
  "type": "mapping",
  "meta": { "..." },
  "mapping": {
    "bidirectional": false,
    "entries": [
      {
        "source": {
          "channel": "*",
          "type": "CC",
          "param": 1
        },
        "target": {
          "param": "FILTER_CUTOFF",
          "track": "*"
        },
        "transform": {
          "src_range": [0, 127],
          "dst_range": [0, 127],
          "invert": false,
          "relative": false
        }
      },
      {
        "source": {
          "channel": 0,
          "type": "NRPN",
          "param": 100
        },
        "target": {
          "param": "VENDOR:0x001",
          "track": 0
        },
        "transform": {
          "src_range": [0, 16383],
          "dst_range": [0, 127]
        }
      }
    ]
  }
}
```

### 11.5.1 JSON正規化規則 (★実装推奨)

差分管理やハッシュ比較を安定させるための正規化規則。

| 項目 | 規則 |
|------|------|
| 文字列エンコーディング | UTF-8 |
| Unicode正規化 | **NFC** (Canonical Decomposition, followed by Canonical Composition) |
| 数値 | 整数は整数として出力 (小数点なし)、範囲外は不正 |
| params配列順序 | バイナリ定義テーブル (ParamDef) の順序と**一致**させる |
| data配列順序 | エントリ順序を保持 (sparse時はPOSITION昇順) |
| オブジェクトキー順序 | アルファベット順 (optional、差分安定性のため) |
| 空白・インデント | 2スペースインデント推奨 (minified可) |

```javascript
// 正規化出力例
function normalizeUxmpJson(obj) {
  // キーをソートして再構築
  return JSON.stringify(obj, Object.keys(obj).sort(), 2);
}

// params配列の順序チェック
function validateParamsOrder(track) {
  const expectedOrder = getStandardDefParams(track.standard_def);
  for (let i = 0; i < track.params.length; i++) {
    if (track.params[i] !== expectedOrder[i]) {
      console.warn(`params order mismatch at index ${i}`);
    }
  }
}
```

**バリデーション規則:**
- data配列の各要素は params.length と同じ長さの配列
- 数値は対応するParamDefのmin/max範囲内
- sparse=true の場合、data[*][0] (POSITION) は昇順

### 11.6 バイナリ⇔JSON変換

#### JSON → バイナリ

```javascript
// Web環境での変換例
function patternToUxmpBinary(json) {
  const buffer = new ArrayBuffer(calculateSize(json));
  const view = new DataView(buffer);
  let offset = 0;

  // Magic "UXPT"
  view.setUint8(offset++, 0x55); // 'U'
  view.setUint8(offset++, 0x58); // 'X'
  view.setUint8(offset++, 0x50); // 'P'
  view.setUint8(offset++, 0x54); // 'T'

  // Version (little-endian)
  view.setUint16(offset, 0x0100, true);
  offset += 2;

  // ... 以下、構造体に従って変換
  return buffer;
}
```

#### バイナリ → JSON

```javascript
function uxmpBinaryToPattern(buffer) {
  const view = new DataView(buffer);
  let offset = 0;

  // Magic確認
  const magic = String.fromCharCode(
    view.getUint8(offset++),
    view.getUint8(offset++),
    view.getUint8(offset++),
    view.getUint8(offset++)
  );
  if (magic !== 'UXPT') throw new Error('Invalid magic');

  // Version (little-endian)
  const version = view.getUint16(offset, true);
  offset += 2;

  // ... 以下、構造体に従って解析
  return { type: 'pattern', pattern: { ... } };
}
```

### 11.7 Base64エンコーディング

URL共有やクリップボード経由でのデータ交換用。

```
uxmp://pattern/<base64url>
uxmp://preset/<base64url>
uxmp://mapping/<base64url>
```

#### エンコード規則

1. バイナリデータをBase64url (RFC 4648 §5) でエンコード
2. パディング `=` は省略可能
3. 圧縮オプション: `uxmp://pattern/z/<deflate+base64url>`

```javascript
// Base64url エンコード
function toUxmpUrl(type, binary) {
  const base64 = btoa(String.fromCharCode(...new Uint8Array(binary)))
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '');
  return `uxmp://${type}/${base64}`;
}

// Base64url デコード
function fromUxmpUrl(url) {
  const match = url.match(/^uxmp:\/\/(\w+)\/(.+)$/);
  if (!match) throw new Error('Invalid UXMP URL');

  const [, type, base64url] = match;
  const base64 = base64url.replace(/-/g, '+').replace(/_/g, '/');
  const binary = Uint8Array.from(atob(base64), c => c.charCodeAt(0));

  return { type, binary };
}
```

### 11.8 Diff/Merge規則

パターンの差分管理・マージ用のガイドライン。

#### 11.8.1 識別子

- `meta.uuid` が同一 → 同一データの異なるバージョン
- `meta.uuid` が異なる → 別データ

#### 11.8.2 マージ戦略

| 要素 | 戦略 |
|------|------|
| meta.modified_at | 新しい方を採用 |
| track.data | ステップ単位で比較・マージ |
| params | 後の値で上書き |
| vendor_params | 保持 (未知のものも維持) |

```javascript
// 簡易マージ例
function mergePatterns(base, theirs, mine) {
  // 3-way merge
  const result = JSON.parse(JSON.stringify(base));

  for (let i = 0; i < result.pattern.tracks.length; i++) {
    const baseTrack = base.pattern.tracks[i];
    const theirTrack = theirs.pattern.tracks[i];
    const myTrack = mine.pattern.tracks[i];

    result.pattern.tracks[i].data = mergeTrackData(
      baseTrack.data, theirTrack.data, myTrack.data
    );
  }

  result.meta.modified_at = new Date().toISOString();
  return result;
}
```

### 11.9 Web API統合例

```javascript
// Fetch APIでパターンを取得
async function fetchPattern(patternId) {
  const res = await fetch(`/api/patterns/${patternId}`, {
    headers: { 'Accept': 'application/vnd.uxmp+json' }
  });
  return await res.json();
}

// パターンをアップロード
async function uploadPattern(pattern) {
  const res = await fetch('/api/patterns', {
    method: 'POST',
    headers: { 'Content-Type': 'application/vnd.uxmp+json' },
    body: JSON.stringify(pattern)
  });
  return await res.json();
}

// バイナリ形式でダウンロード
async function downloadPatternBinary(patternId) {
  const res = await fetch(`/api/patterns/${patternId}`, {
    headers: { 'Accept': 'application/vnd.uxmp' }
  });
  return await res.arrayBuffer();
}
```

### 11.10 MIME Type

| 形式 | MIME Type |
|------|-----------|
| JSON | `application/vnd.uxmp+json` |
| バイナリ | `application/vnd.uxmp` |

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
// {local_id, fallback_id, flags, name_len, min, max, default_val}
VendorParamDef params[] = {
    {0x001, 0x47,   0, 6, 0, 127, 0},   // "Warmth" (fallback=CC#71)
    {0x002, 0xFFFF, 0, 5, 0, 127, 64},  // "Drive" (fallback=無視)
};
// followed by: "Warmth" "Drive"

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
