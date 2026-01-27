# UXMP 概念モデル整理

概念モデルを明確化し、データ構造と実装提案を分離して整理する。

---

## 1. 概念モデル（何が何を持つか）

### 1.1 Step（ステップ）

**定義**
```
Step = values[]
```

- **純粋な値の配列**であり、IDを持たない
- 各要素の意味は外側の Pattern が決める
- Step 単体では意味を持たないデータ

**ポイント**
- Dense の場合: Step の index が位置
- Sparse の場合: values[0] が POSITION となり、位置は値で表現される

**対応する構造体**: なし（Pattern 内の配列要素として格納）

**ファイル交換用ラッパー**: `StepFile`

```c
struct StepFile {
    uint8_t  magic[4];          // "UXST"
    uint16_t version;
    uint8_t  standard_def_id;   // StandardDefId (0=custom)
    uint8_t  param_count;       // パラメータ数
    uint32_t total_size;
    uint32_t crc32;
    uint16_t step_index;        // 対象ステップのインデックス
    uint16_t reserved;
    ParamDef defs[param_count]; // standard_def_id == 0 の場合のみ
    uint16_t values[param_count]; // ステップデータ
};
```

**用途**
- 特定ステップの外部編集（エディタから1ステップだけ送信）
- パターン内の部分更新
- ステップ単位のコピー＆ペースト

---

### 1.2 Pattern（パターン）★フレーズ交換単位

**定義**
```
Pattern = StepSequence + ParameterSetDefinition
        = Step[] + ParamDef[]
```

- **StepSequence**: Step の配列（純粋データ）
- **ParameterSetDefinition**: Step の values[] の順序と意味を決める定義
  - ParamDef[] そのもの、または StandardDefId による参照

**役割**
1. ステップデータを保持する
2. ステップに意味を与える（パラメータとの対応を明示）
3. **交換可能な最小単位**として自己完結する

**Dense と Sparse**

| 表現 | 特徴 |
|------|------|
| Dense | index = 位置、全ステップを持つ |
| Sparse | values[0] = POSITION、イベント列のみ持つ |

**Pattern が持つもの**
- ParamDef[] または StandardDefId（ステップの意味）
- Step[]（データ）
- length_steps（論理長）
- sparse フラグ
- preset_ref（音色参照、オプション）

**Pattern が持たないもの**
- テンポ → Song の責務
- 拍子 → Song の責務
- MIDIチャンネル → Track の責務

**対応する構造体**: `Pattern`

```c
struct Pattern {
    uint8_t  standard_def_id;   // StandardDefId (0=custom)
    uint8_t  param_count;       // パラメータ数
    uint16_t flags;             // bit0: sparse
    uint16_t length_steps;      // 論理長（パターンが表すステップ範囲）
    uint16_t step_count;        // 実際のステップ数
    uint16_t preset_ref;        // Preset への参照 (0xFFFF=なし、Track の preset_ref をオーバーライド)
    ParamDef defs[param_count]; // standard_def_id == 0 の場合のみ
    uint16_t steps[step_count][param_count]; // 全て16bit固定
};

// step_size = param_count * 2（全パラメータ16bit固定のため）
// steps へのアクセス: &steps[0] = (uint8_t*)(pattern + 1) + sizeof(uint16_t) + param_count * sizeof(ParamDef)
```

**ファイル交換用ラッパー**: `PatternFile`

```c
struct PatternFile {
    uint8_t  magic[4];          // "UXPN"
    uint16_t version;
    uint16_t reserved;
    uint32_t total_size;
    uint32_t crc32;
    Pattern  pattern;           // 本体
};
// Pool 内では Pattern のみ、単体ファイルでは PatternFile でラップ
```

---

### 1.3 Track（トラック）

**定義**
```
Track = PatternReference[] + 再生属性
      = [Pattern0, Pattern1, Pattern2, ...] + 属性
```

- **PatternReference[]**: どの Pattern をどの順番で再生するかの参照配列
- **再生属性**: MIDIチャンネル、音色、ミュート状態など

**役割**
- 複数の Pattern の**再生順序**を決める
- Pattern そのものは持たない（参照のみ）
- 音色・チャンネルなどの固定属性を持つ

**対応する構造体**: `TrackEntry` + `PatternRef`

```c
struct TrackEntry {
    uint8_t  track_id;
    uint8_t  midi_channel;      // 0-15
    uint16_t flags;             // bit0: muted, bit1: solo
    uint16_t pattern_ref_count; // 参照するパターン数
    uint16_t preset_ref;        // Preset への参照 (0xFFFF=なし)
    uint8_t  time_scale;        // クロック倍率 (0x40=1x)
    uint8_t  direction;         // PlayDirection
    uint16_t reserved;
    uint32_t pattern_refs_offset; // PatternRef[] へのオフセット
};

struct PatternRef {
    uint16_t pattern_id;        // PatternPool 内の ID
    uint16_t repeat_count;      // 繰り返し回数 (0=無限)
    uint16_t start_step;        // 開始ステップ (0=先頭から)
    uint16_t reserved;
};
```

---

### 1.4 Song（ソング）

**定義**
```
Song = Track[] + グローバル属性
```

- **Track[]**: トラックの配列
- **グローバル属性**: テンポ、拍子、セクション配置など

**役割**
- 複数の Track を束ねる
- 楽曲全体の構造を定義する

**対応する構造体**: `SongHeader`

```c
struct SongHeader {
    uint8_t  magic[4];          // "UXSG"
    uint16_t version;
    uint8_t  track_count;
    uint8_t  flags;
    uint32_t tempo_x100;        // デフォルトテンポ × 100
    uint16_t time_sig_num;
    uint16_t time_sig_den;
    uint32_t track_dir_offset;  // TrackEntry[] へのオフセット
    uint32_t tempo_map_offset;  // テンポマップへのオフセット (0=なし)
    uint32_t total_size;
    uint32_t crc32;
    uint8_t  name_length;
    uint8_t  reserved[3];
    // char name[name_length] follows
    // TrackEntry tracks[track_count] follows at track_dir_offset
};
```

---

### 1.5 Project（プロジェクト）

**定義**
```
Project = Song[] + SharedPools
```

- **Song[]**: ソングへの参照
- **SharedPools**: 独立交換可能な資源プール

**SharedPools の構成**
- PatternPool: 共有パターン
- PresetPool: 共有プリセット
- SamplePool: 共有サンプル
- MappingPool: 共有マッピング

**役割**
- 複数の Song を管理
- 資源の共有と参照管理

**対応する構造体**: `ProjectHeader` + `PoolHeader` + `PoolEntry`

```c
struct ProjectHeader {
    uint8_t  magic[4];          // "UXPJ"
    uint16_t version;
    uint8_t  song_count;
    uint8_t  flags;
    uint16_t default_preset_ref; // デフォルト Preset
    uint16_t reserved;
    uint32_t song_dir_offset;    // SongRef[] へのオフセット
    uint32_t pattern_pool_offset;
    uint32_t preset_pool_offset;
    uint32_t sample_pool_offset; // 0=なし
    uint32_t mapping_pool_offset;// 0=なし
    uint32_t total_size;
    uint32_t crc32;
    uint8_t  name_length;
    uint8_t  reserved2[3];
    // char name[name_length] follows
};

struct PoolHeader {
    uint8_t  magic[4];          // "UXPL"
    uint8_t  resource_type;     // ResourceType
    uint8_t  reserved;
    uint16_t entry_count;
    uint32_t entries_offset;    // PoolEntry[] へのオフセット
    uint32_t total_size;
};

struct PoolEntry {
    uint16_t id;                // Pool 内 ID
    uint16_t flags;
    uint32_t data_offset;       // 実データへのオフセット
    uint32_t data_size;
};
```

---

### 1.6 Preset（プリセット）

**定義**
```
Preset = ParameterSet
       = { id -> value } + traits参照
```

- パラメータIDから値への写像
- 概念としては**辞書**
- 実装形式は順序配列でも可

**役割**
- 音色設定を Pattern や Track から独立して交換可能にする
- Pattern、Track、Song のいずれからも参照可能

**対応する構造体**: `PresetHeader`（Pattern と同構造）

```c
struct PresetHeader {
    uint8_t  magic[4];          // "UXPS"
    uint16_t version;
    uint8_t  standard_def_id;   // StandardDefId (0=custom)
    uint8_t  param_count;
    uint16_t device_family;     // デバイスファミリーID
    uint16_t device_model;      // デバイスモデルID
    uint32_t data_offset;
    uint32_t total_size;
    uint32_t crc32;
    uint8_t  name_length;
    uint8_t  reserved[3];
    // char name[name_length] follows
    // ParamDef defs[param_count] follows (if standard_def_id == 0)
    // uint8_t data[] follows at data_offset
};
```

---

### 1.7 Parameter（パラメータ）

**定義**
```
Parameter = (id, value, traits)
```

- **id**: 16bit ParamId
- **value**: 16bit 整数値（**全パラメータ固定**）
- **traits**: 値の意味と制約を定義するメタ情報

**値は全て16bit**
- 交換フォーマットとしてのシンプルさを優先し、全パラメータを 16bit (0-65535) に統一
- PITCH_BEND (14bit)、高精度CC (14bit) なども単一パラメータで表現可能
- これにより step_size = param_count * 2 で固定となり、可変長の複雑さを排除
- 組み込みデバイスでは内部実装で 8bit に変換して保持することも可能

**traits の内容**
- 型情報: signed
- 範囲: min, max (0-65535)
- 初期値: default_val
- 互換: optional

**対応する構造体**: `ParamDef`

```c
struct ParamDef {
    uint16_t id;
    uint16_t flags;             // bit0: optional, bit1: signed
    uint16_t min;
    uint16_t max;
    uint16_t default_val;
    uint16_t reserved;
};
// 12バイト
```

---

## 2. 階層構造

```
Project (ProjectHeader)
  ├─ Song[] (SongHeader)
  │    └─ Track[] (TrackEntry)
  │         └─ PatternRef[]
  │              └─ → Pattern (参照)
  │
  └─ SharedPools (PoolHeader + PoolEntry[])
       ├─ PatternPool → Pattern[] (PatternHeader)
       ├─ PresetPool → Preset[] (PresetHeader)
       ├─ SamplePool → Sample[]
       └─ MappingPool → Mapping[]

Pattern (PatternHeader, 自己完結、交換最小単位)
  ├─ ParamDef[] (ステップの意味)
  └─ Step[] (純粋データ、Data Section)
       └─ values[]
```

---

## 3. 紐付けと参照解決

### 3.1 参照の粒度

| レベル | 参照元 | 参照先 | 用途 |
|--------|--------|--------|------|
| Pattern | Pattern.preset_ref | Preset | パターン固有の音色 |
| Track | TrackEntry.preset_ref | Preset | トラック共通の音色 |
| Project | ProjectHeader.default_preset_ref | Preset | デフォルト音色 |

### 3.2 パラメータ値の優先順位

```
1. Step のパラメータ値 → 最優先（ステップデータそのもの）
2. Pattern.preset_ref != 0xFFFF → パターン固有の音色
3. TrackEntry.preset_ref != 0xFFFF → トラック共通の音色
4. ProjectHeader.default_preset_ref != 0xFFFF → プロジェクトデフォルト
5. すべて 0xFFFF → ParamDef.default_val
```

### 3.3 参照の共通表現

**対応する構造体**: `ResourceRef`

```c
struct ResourceRef {
    uint8_t  ref_type;          // LOCAL_ID, UUID, INLINE, EXTERNAL
    uint8_t  resource_type;     // PATTERN, PRESET, SAMPLE, MAPPING, SONG
    uint16_t flags;             // bit0: required
    uint16_t local_id;          // ref_type == LOCAL_ID の場合
    uint16_t fallback_id;       // フォールバック先 (0xFFFF=なし)
};

enum RefType : uint8_t {
    LOCAL_ID = 0x01,
    UUID     = 0x02,
    INLINE   = 0x03,
    EXTERNAL = 0x04,
};

enum ResourceType : uint8_t {
    PATTERN = 0x01,
    PRESET  = 0x02,
    SAMPLE  = 0x03,
    MAPPING = 0x04,
    SONG    = 0x05,
};
```

---

## 4. 構造体一覧

| 概念 | 構造体 | Magic | サイズ |
|------|--------|-------|--------|
| Step | (配列要素) | - | param_count * 2 |
| StepFile | StepFile | UXST | 20 + 可変 |
| Pattern | Pattern | - | 10 + 可変 |
| PatternFile | PatternFile | UXPN | 16 + Pattern |
| Track | TrackEntry | - | 16 |
| PatternRef | PatternRef | - | 8 |
| Song | SongHeader | UXSG | 32 + 可変 |
| Project | ProjectHeader | UXPJ | 40 + 可変 |
| Pool | PoolHeader | UXPL | 16 |
| PoolEntry | PoolEntry | - | 12 |
| Preset | PresetHeader | UXPS | 28 + 可変 |
| ParamDef | ParamDef | - | 12 |
| ResourceRef | ResourceRef | - | 8 |

---

## 5. 設計判断

### 5.1 パラメータ値のビット幅

**検討した選択肢**

| 方式 | メリット | デメリット |
|------|----------|------------|
| 8bit 固定 | メモリ効率、組み込み向け | 14bit値を2パラメータに分割する必要あり |
| 8bit/16bit 混在 | 柔軟性 | パース複雑、step_size が可変長 |
| **16bit 固定** | シンプル、14bit/16bit値をそのまま格納 | メモリ2倍 |

**決定: 16bit 固定**

**理由**
1. **交換フォーマットとしてのシンプルさ** - パース処理が単純、step_size = param_count * 2 で固定
2. **高精度パラメータのサポート** - PITCH_BEND (14bit)、高精度CC (14bit) を単一パラメータで表現
3. **MIDI転送速度は許容範囲** - 典型的なパターン（64ステップ×4パラメータ = 512バイト）の転送時間:
   - MIDI 1.0 (31.25kbps): 約0.16秒
   - SysEx オーバーヘッド込み: 約0.2秒
4. **組み込みデバイスでの変換** - 内部実装では 8bit に変換して保持可能、交換時のみ 16bit

**トレードオフ**
- メモリ使用量は 8bit 比で2倍だが、現代のデバイスでは許容範囲
- 組み込みデバイスでは受信後に内部フォーマットへ変換することで対応可能

---

## 7. 実装提案（データ構造とは独立）

### 7.1 Copy on Write 編集

- 参照プリセットを編集開始した時点で複製
- 参照を複製側へ差し替え
- 共有元は不変に保つ

**目的**: 意図しない共有プリセット上書きを防ぐ

### 7.2 明示リンクでバリアント化

- ユーザー操作で link/pin を行った場合のみバリアント作成
- link しない限りは疎結合参照を維持

### 7.3 Undo と Snapshot

- 誤操作は起こる前提で undo を強化
- 任意時点スナップショットを提供
- 参照差し替えとプリセット差分が主な対象

---

## 8. まとめ

### 核心となる定義

| 概念 | 定義 | 構造体 | 役割 |
|------|------|--------|------|
| **Step** | uint16_t[param_count] | (配列) | 純粋データ、意味を持たない |
| **Pattern** | Step[] + ParamDef[] | Pattern | **交換最小単位**、自己完結 |
| **Track** | PatternRef[] + 属性 | TrackEntry | **再生順序**を決める |
| **Song** | Track[] | SongHeader | 楽曲構造 |
| **Project** | Song[] + Pools | ProjectHeader | 資源管理 |
| **Preset** | { id -> value } | PresetHeader | 音色設定 |

### 設計原則

1. **Pattern は交換最小単位** - 自己完結し、単体で意味を持つ
2. **Track は参照コンテナ** - データを持たず、Pattern の再生順序を決める
3. **Step は純粋データ** - 意味は Pattern が与える
4. **参照は疎結合** - 埋め込みより参照を優先
5. **Pools で資源共有** - 重複を避け、一元管理
6. **全パラメータ16bit固定** - step_size = param_count * 2 でシンプルに
