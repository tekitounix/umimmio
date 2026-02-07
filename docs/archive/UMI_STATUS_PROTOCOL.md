# UMI Status Protocol 設計書

バージョン: 0.2.0 (Draft)
ステータス: 設計段階

**移行メモ:** UMI-SysEx へ統合完了。本書は参考資料。
仕様本体は「docs/new/umi-sysex/UMI_SYSEX_STATUS.md」。
実装ノートは「docs/new/umi-sysex/UMI_SYSEX_STATUS_IMPLEMENTATION_NOTES.md」。

## 1. 概要

UMIデバイスのステータス情報取得・設定プロトコルの設計書。SysExをトランスポートとして使用し、シェルI/O、ファームウェア更新、パラメータ照会、メーター情報等を提供する。

## 2. 設計要件

### 2.1 リアルタイム非干渉

MIDIリアルタイムメッセージとSysExの優先度:

| 種類 | 優先度 | 処理タイミング | 例 |
|------|--------|---------------|-----|
| Note On/Off, CC, Clock | 最高 | 即座（AudioTask） | 演奏データ |
| SysEx (Status) | 低 | ServerTask内 | 本プロトコル |

**SysEx処理の制約:**
1. オーディオ処理（DMA割り込み、DSP処理）に一切影響を与えない
2. MIDI演奏データの処理に影響を与えない
3. UI更新（ControlTask）に影響を与えない

### 2.2 現状の実装と改善点

現在の実装（`kernel.cc`）:
- SysEx**組み立て**: USB ISR内（`usb_midi.hh::handle_rx`でsysex_buf_に蓄積）
- `on_sysex`コールバック: ISR内で`g_sysex_buf`にコピー + `resume_task(system_task)` — 軽量
- **プロトコル処理**: SystemTask（Server優先度）内で`g_stdio.process_message()`を実行 — ISR外

つまりプロトコル処理自体はすでにタスクコンテキストで実行されている。残る改善点:

1. **SysEx組み立てがISR内**: 長いSysExで割り込み処理時間が延びる（現状256バイト上限で実害は小さい）
2. **単一バッファ（g_sysex_buf）**: 処理中に次のSysExが来ると取りこぼす。キュー化が望ましい
3. **ServerTask内でsuspend_task/resume_taskによる起床**: wait_event/notifyベースのイベントドリブンに統一すべき

これらを改善するアーキテクチャを§3で定義する。

## 3. SysEx処理アーキテクチャ

### 3.1 設計原則

**ServerTask内イベントドリブン処理。タスク分離しない。**

根拠:
- SysExプロトコル処理（shell応答、ステータス応答）は数msで完了し、長時間CPUを占有しない
- DFUのFlash書き込みはStorageServiceに非同期委譲済み（UMIOS_STORAGE.md参照）
- タスク追加はスタックメモリ消費・コード複雑化のコストに見合わない
- ServerTask内でイベントループを回せば、SysEx処理中もUSB受信を処理可能

### 3.2 処理フロー

```
┌──────────────────────────────────────────────────────┐
│  割り込みハンドラ【最小限の処理のみ】                    │
│                                                       │
│  handle_rx():                                         │
│    生バイト → raw_rx_queue.try_push()                  │
│    kernel.notify_from_isr(server_task, EVENT_USB_RX)  │
│                                                       │
│  【禁止】CIN解析、SysEx組立、コールバック               │
└───────────────────────┬──────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────┐
│  ServerTask (Priority::Server)【パケット処理+プロトコル】│
│                                                       │
│  イベントループ:                                       │
│    while (true) {                                     │
│      auto ev = wait_event(USB_RX | FS_COMPLETE | ...) │
│                                                       │
│      if (ev & USB_RX) {                               │
│        while (auto pkt = raw_rx_queue.try_pop()) {    │
│          // CIN解析、SysEx組み立て                      │
│          // Note/CC → midi_queue → notify(AudioTask)  │
│          // SysEx完結 → プロトコル処理（下記）          │
│        }                                              │
│      }                                                │
│                                                       │
│      if (ev & FS_COMPLETE) {                          │
│        // DFU: 次のブロック書き込み要求                  │
│      }                                                │
│    }                                                  │
│                                                       │
│  SysExプロトコル処理:                                   │
│    shell応答     → 即完了（数ms）                      │
│    ステータス応答 → 即完了                              │
│    DFU書き込み   → StorageServiceへ非同期要求           │
│    stdout flush  → USB TX                             │
└──────────────────────────────────────────────────────┘
```

### 3.3 バイナリ転送（DFU）のフロー

DFU転送は大量データ（.umiaで最大128KB）だが、パケット単位でwait_eventを挟むことでServerTaskがCPUを長時間占有しない:

```
1. FW_BEGIN受信 → DFUセッション開始
2. FW_DATA受信 → StorageServiceへ非同期書き込み要求（即return）
3. wait_event(FS_COMPLETE | USB_RX)  ← CPUを手放す → ControlTask(UI)が動ける
4. FS_COMPLETE → ACK送信
5. USB_RX → 次のFW_DATAパケット → 2に戻る
6. FW_VERIFY受信 → CRC検証 → 完了応答
```

各ステップは短時間（数ms以下）で完了し、ステップ3のwait_eventでCPUを手放す。これは非同期FS設計（UMIOS_STORAGE.md）の自然な帰結。

### 3.4 実装ガイドライン

ServerTaskはControlTask（UI）より高い優先度のため、CPU占有に注意:

- **SysExプロトコル処理**: 個々のコマンド応答は即完了（数ms）→ 問題なし
- **DFU**: パケット単位でwait_event → 問題なし
- **重いshell応答**（将来: FS一覧、メモリダンプ等）: チャンク単位で送信し、間にyield()を挟むこと

**原則: ServerTask内の1回の連続処理は数ms以内に収める。** それを超える場合はyield()またはwait_eventで分割する。

### 3.4 タスク構成

```
Realtime (0): AudioTask     — DSP処理、MIDI Note/CC反映
Server   (1): ServerTask    — ドライバ、SysEx処理、StorageService
User     (2): ControlTask   — アプリmain、UI
Idle     (3): IdleTask      — スリープ
```

SysExプロトコル処理はServerTask内で行う。Idleタスクへの分離は不要。

## 4. コマンド体系

### 4.1 設計原則

1. **シンプルさ**: 最小限のコマンドセット
2. **効率性**: バイナリ形式、低オーバーヘッド
3. **非同期**: リクエスト/レスポンス分離
4. **拡張性**: 将来のコマンド追加に対応

### 4.2 コマンド一覧

```c++
enum class Command : uint8_t {
    // Standard IO (0x01-0x0F)
    STDOUT_DATA     = 0x01,
    STDERR_DATA     = 0x02,
    STDIN_DATA      = 0x03,
    STDIN_EOF       = 0x04,
    FLOW_CTRL       = 0x05,

    // Firmware Update (0x10-0x1F)
    FW_QUERY        = 0x10,
    // ...

    // System (0x20-0x2F)
    PING            = 0x20,
    PONG            = 0x21,
    RESET           = 0x22,
    VERSION         = 0x23,
    STATUS_REQUEST  = 0x24,
    STATUS_RESPONSE = 0x25,
    IDENTITY_REQ    = 0x26,
    IDENTITY_RES    = 0x27,

    // Audio Status (0x30-0x3F)
    AUDIO_STATUS_REQ = 0x30,
    AUDIO_STATUS_RES = 0x31,
    METER_REQ        = 0x32,
    METER_RES        = 0x33,

    // Parameter (0x40-0x4F)
    PARAM_LIST_REQ  = 0x40,
    PARAM_LIST_RES  = 0x41,
    PARAM_GET       = 0x42,
    PARAM_VALUE     = 0x43,
    PARAM_SET       = 0x44,
    PARAM_ACK       = 0x45,
    PARAM_SUBSCRIBE = 0x46,
    PARAM_NOTIFY    = 0x47,

    // Preset (0x50-0x5F) - 将来用
    PRESET_LIST_REQ = 0x50,
    PRESET_LIST_RES = 0x51,
    PRESET_LOAD     = 0x52,
    PRESET_SAVE     = 0x53,
};
```

### 4.3 メッセージ詳細

#### IDENTITY_RESPONSE (0x27)

```
Byte 0-2:   Manufacturer ID (3 bytes, MIDI規格)
Byte 3-4:   Device Family (uint16_t, BE)
Byte 5-6:   Device Model (uint16_t, BE)
Byte 7:     Firmware Version Major
Byte 8:     Firmware Version Minor
Byte 9-10:  Firmware Version Patch (uint16_t, BE)
Byte 11:    Protocol Version Major
Byte 12:    Protocol Version Minor
Byte 13+:   Device Name (null-terminated UTF-8)
```

#### AUDIO_STATUS_RESPONSE (0x31)

```
Byte 0-1:   DSP Load (uint16_t, ×100, 例: 5600 = 56.00%)
Byte 2-4:   Sample Rate (uint24_t)
Byte 5-6:   Buffer Size (uint16_t, samples)
Byte 7-8:   Latency Input (uint16_t, samples)
Byte 9-10:  Latency Output (uint16_t, samples)
Byte 11:    Polyphony Current
Byte 12:    Polyphony Max
Byte 13:    Flags
            bit 0: Audio Running
            bit 1: Clipping Detected
            bit 2: Buffer Underrun Detected
            bit 3: Buffer Overrun Detected
Byte 14-17: Underrun Count (uint32_t)
Byte 18-21: Overrun Count (uint32_t)
Byte 22-25: Uptime (uint32_t, seconds)
```

#### METER_RESPONSE (0x33)

```
Byte 0:     Channel Count (N)
For each channel (N × 4 bytes):
  Byte 0-1: Peak Level (int16_t, -32768=silence, 0=0dBFS)
  Byte 2-3: RMS Level (int16_t)
```

明示的なサブスクリプション後のみ送信。

#### PARAM_LIST_RESPONSE (0x41)

大きなリストは複数メッセージに分割。

```
Byte 0:     Total Param Count
Byte 1:     Offset
Byte 2:     Count in this message
For each param:
  Byte 0:     Param ID
  Byte 1:     CC Number (0xFF = no CC mapping)
  Byte 2:     Type (0=int, 1=float, 2=bool, 3=enum)
  Byte 3-4:   Min Value (int16_t)
  Byte 5-6:   Max Value (int16_t)
  Byte 7-8:   Default Value (int16_t)
  Byte 9:     Flags
              bit 0: Read-only
              bit 1: Hidden
              bit 2: Automatable
  Byte 10:    Group ID
  Byte 11:    Name Length
  Byte 12+:   Name (UTF-8)
```

#### PARAM_VALUE (0x43)

```
Byte 0:     Param ID
Byte 1-2:   Value (int16_t)
```

#### PARAM_SUBSCRIBE (0x46)

```
Byte 0:     Action (0=unsubscribe, 1=subscribe)
Byte 1:     Param ID (0xFF = all)
Byte 2:     Rate Limit (ms, 0=every change)
```

### 4.4 処理優先度ガイドライン

| コマンドグループ | 処理場所 | 備考 |
|----------------|---------|------|
| Note/CC | AudioTask (Realtime) | midi_queue経由 |
| STDIN/STDOUT/STDERR | ServerTask | プロトコル処理 |
| FW_* | ServerTask | StorageServiceへ非同期委譲 |
| STATUS/IDENTITY | ServerTask | 即完了 |
| AUDIO_STATUS | ServerTask | 即完了 |
| METER_* | ServerTask | 定期タイマー |
| PARAM_* | ServerTask | 即完了 |

全てのSysExプロトコル処理はServerTask内で完結する。

### 4.5 フロー制御

```
受信側: sysex組み立てバッファが逼迫 → FLOW_CTRL(XOFF)送信
        バッファ回復 → FLOW_CTRL(XON)送信

送信側: ホストからXOFF受信 → 送信一時停止
        XON受信 → 送信再開

PARAM_SUBSCRIBE: Rate Limitで通知頻度を制限
```

## 5. 既存プロトコル比較

| 項目 | MIDI-CI | OSC | Elektron SysEx | UMI |
|------|---------|-----|----------------|-----|
| トランスポート | SysEx | UDP/TCP | SysEx | SysEx |
| データ形式 | JSON/CBOR | バイナリ+文字列 | バイナリ | バイナリ |
| オーバーヘッド | 大 | 中 | 小 | 小 |
| パラメータ照会 | ○ | ○ | ○ | ○ |
| サブスクリプション | ○ | × | × | ○ (Rate Limit付き) |
| ファームウェア更新 | × | × | × | ○ |
| シェルI/O | × | × | × | ○ |

**設計判断:**
- MIDI-CIのJSON/CBORは組込みには重い → バイナリ形式を採用
- OSCの階層的思想は参考になるが、MIDIトランスポートに不向き
- Elektron方式に近いコンパクトなバイナリSysExを基本とする
- サブスクリプション（MIDI-CI参考）はRate Limit付きで限定的に採用

## 6. 実装計画

### Phase 1: 基本ステータス
- STATUS_REQUEST/RESPONSE実装
- IDENTITY_REQ/RES実装

### Phase 2: パラメータ照会
- PARAM_LIST, PARAM_GET/SET実装
- CC以外のパラメータ操作

### Phase 3: サブスクリプション
- PARAM_SUBSCRIBE/NOTIFY実装
- Rate Limit付き通知

### Phase 4: メーター情報
- METER_REQ/RES実装
- AUDIO_STATUS実装

### Phase 5: SysEx処理アーキテクチャ改善
- 割り込みハンドラの最小化（§3.2の実装）
- ServerTask内イベントドリブン化

## 7. 結論

1. **バイナリ形式を維持** — 組込み効率優先
2. **ServerTask内で処理を完結** — タスク分離不要、イベントドリブンで非ブロッキング
3. **DFUはStorageService非同期委譲** — Flash書き込み中もServerTask応答可能
4. **サブスクリプションはRate Limit付き** — バッファオーバーフロー防止
