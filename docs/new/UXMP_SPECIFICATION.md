# UMI Extended MIDI Protocol (UXMP) 仕様書

バージョン: 0.1.0 (Draft)
ステータス: 設計段階
目標: MIDI 1.0の事実上の標準拡張

## 1. 概要

### 1.1 背景と目的

MIDI 2.0は多くの機能を提供するが、組み込みデバイスには過剰な部分が多い。MIDI 1.0の枠組み内でSysExによる拡張を標準化することで、以下を実現する：

1. **メーカー間で共通のプロトコル** - 現状バラバラな実装を統一
2. **軽量な実装** - 組み込みデバイスでも実装可能
3. **後方互換性** - 既存のMIDI 1.0インフラで動作
4. **リアルタイム性の保護** - 演奏データへの影響を最小化

### 1.2 設計原則

1. **シンプルさ優先** - JSON/CBORは使わない、バイナリ形式
2. **リアルタイム非干渉** - SysEx処理は低優先度、要求がなければ応答しない
3. **既存仕様の活用** - MIDI SDS、Universal SysEx等の良い部分を継承
4. **段階的採用** - 必要な機能だけを実装可能

### 1.3 プロトコルスイート

| プロトコル | 略称 | 用途 |
|-----------|------|------|
| UXMP-DFU | DFU over MIDI | ファームウェア更新 |
| UXMP-STDIO | Standard I/O over MIDI | stdin/stdout/stderr |
| UXMP-SHELL | Shell over MIDI | 対話的コマンド実行 |
| UXMP-TEST | Test over MIDI | 自動テスト・検証 |
| UXMP-STATUS | Status & Log | 状態監視・ログ取得 |
| UXMP-DATA | User Data Exchange | プリセット・パターン・サンプル転送 |

## 2. 共通仕様

### 2.1 SysExメッセージ形式

```
F0 <Manufacturer ID> <Protocol ID> <Command> <Sequence> [Payload...] [Checksum] F7
```

| フィールド | サイズ | 説明 |
|-----------|--------|------|
| F0 | 1 | SysEx開始 |
| Manufacturer ID | 1 or 3 | メーカーID (後述) |
| Protocol ID | 1 | UXMPプロトコル識別子 |
| Command | 1 | コマンド番号 |
| Sequence | 1 | シーケンス番号 (0-127) |
| Payload | 0-N | コマンド固有データ |
| Checksum | 0-1 | チェックサム (オプション) |
| F7 | 1 | SysEx終了 |

### 2.2 Manufacturer ID の扱い

#### オプション1: Universal Non-Real Time (推奨)

```
F0 7E <Device ID> <Sub-ID1> <Sub-ID2> ...
```

- Sub-ID1 = 0x7D (教育/開発用) または新規割り当て申請
- 標準化を目指すならMMA/AMEIへの申請が必要

#### オプション2: 開発用ID

```
F0 7D ... (Educational/Development Use)
```

- 商用製品には使用不可
- プロトタイプ・開発用

#### オプション3: UMI用3バイトID

```
F0 00 7F 00 ... (仮: 7E 7F 00)
```

- UMIプロジェクト固有
- 他メーカーが採用する場合は各自のIDを使用

### 2.3 Protocol ID

| ID | プロトコル | 説明 |
|----|-----------|------|
| 0x01 | UXMP-STDIO | Standard I/O |
| 0x02 | UXMP-DFU | Firmware Update |
| 0x03 | UXMP-SHELL | Interactive Shell |
| 0x04 | UXMP-TEST | Automated Testing |
| 0x05 | UXMP-STATUS | Status & Logging |
| 0x06 | UXMP-DATA | User Data Exchange |
| 0x10-0x1F | Reserved | 将来の標準拡張用 |
| 0x20-0x7F | Vendor | ベンダー固有拡張 |

### 2.4 7ビットエンコーディング

MIDI SysExではMSB=0が必須。8ビットデータは以下の形式でエンコード：

```
入力: 7バイト (56ビット)
出力: 8バイト (1バイト目にMSB集約)

Byte 0: 0 | b7_6 | b7_5 | b7_4 | b7_3 | b7_2 | b7_1 | b7_0
Byte 1: 0 | b6..b0 of input byte 0
Byte 2: 0 | b6..b0 of input byte 1
...
Byte 7: 0 | b6..b0 of input byte 6
```

オーバーヘッド: 約14.3%

### 2.5 チェックサム

#### 方式A: Roland互換 (推奨)

```c
uint8_t checksum_roland(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (0x80 - (sum & 0x7F)) & 0x7F;
}
```

検証: データ + チェックサムの合計の下位7ビットが0

#### 方式B: XOR (簡易)

```c
uint8_t checksum_xor(const uint8_t* data, size_t len) {
    uint8_t xor_val = 0;
    for (size_t i = 0; i < len; i++) {
        xor_val ^= data[i];
    }
    return xor_val & 0x7F;
}
```

### 2.6 フロー制御

#### 2.6.1 Push/Poll モデル

- **Poll (デフォルト)**: ホストが要求したときのみ応答
- **Push (サブスクリプション)**: 明示的な購読後に自動送信

```
┌────────────────────────────────────────────────────────────┐
│                    フロー制御の原則                          │
├────────────────────────────────────────────────────────────┤
│ 1. 要求がなければ応答しない (リアルタイム保護)               │
│ 2. Push通知はサブスクリプション後のみ                       │
│ 3. レート制限を必ず設定 (ms単位)                            │
│ 4. XON/XOFFによるバックプレッシャー                         │
└────────────────────────────────────────────────────────────┘
```

#### 2.6.2 XON/XOFF

| コマンド | 値 | 説明 |
|---------|-----|------|
| XOFF | 0x00 | 送信一時停止要求 |
| XON | 0x01 | 送信再開 |

### 2.7 タイミング要件

| 条件 | 推奨値 | 説明 |
|------|--------|------|
| メッセージ間遅延 | ≥20ms | 連続SysEx間の最小間隔 |
| 応答タイムアウト | 2000ms | ACK/NAK待ちの最大時間 |
| バースト制限 | 4KB/100ms | 短時間の最大転送量 |

## 3. UXMP-STDIO (Standard I/O over MIDI)

### 3.1 概要

UART/CDC相当の双方向ストリームをMIDI SysEx上で実現。

### 3.2 コマンド

| Command | 名前 | 方向 | 説明 |
|---------|------|------|------|
| 0x01 | STDOUT_DATA | Device→Host | 標準出力データ |
| 0x02 | STDERR_DATA | Device→Host | 標準エラー出力 |
| 0x03 | STDIN_DATA | Host→Device | 標準入力データ |
| 0x04 | STDIN_EOF | Host→Device | 入力終了 |
| 0x05 | FLOW_CTRL | Both | フロー制御 (XON/XOFF) |

### 3.3 メッセージ形式

#### STDOUT_DATA / STDERR_DATA / STDIN_DATA

```
F0 <MfgID> 01 <Cmd> <Seq> <EncodedData...> <Checksum> F7
```

- EncodedData: 7ビットエンコードされたテキスト/バイナリ
- 最大ペイロード: 1024バイト (エンコード前)

#### FLOW_CTRL

```
F0 <MfgID> 01 05 <Seq> <XON/XOFF> F7
```

## 4. UXMP-DFU (Firmware Update over MIDI)

### 4.1 概要

MIDI SysExによるファームウェア更新。USB DFUの問題(ドライバ、モード切替)を回避。

### 4.2 既存実装の分析

| メーカー | 方式 | 特徴 |
|---------|------|------|
| Elektron | 独自SysEx | 大きなファイル、USB必須 |
| KORG | logue-cli | 非公開プロトコル |
| Arturia | 独自SysEx | リバースエンジニアリングのみ |
| tubbutec | SysEx直接 | F7後の遅延が重要 |

### 4.3 コマンド

| Command | 名前 | 方向 | 説明 |
|---------|------|------|------|
| 0x10 | FW_QUERY | H→D | ファームウェア情報要求 |
| 0x11 | FW_INFO | D→H | ファームウェア情報応答 |
| 0x12 | FW_BEGIN | H→D | 更新開始 |
| 0x13 | FW_BEGIN_ACK | D→H | 更新開始確認 |
| 0x14 | FW_DATA | H→D | ファームウェアデータ |
| 0x15 | FW_DATA_ACK | D→H | データ受信確認 |
| 0x16 | FW_END | H→D | 更新完了 |
| 0x17 | FW_END_ACK | D→H | 完了確認 |
| 0x18 | FW_ABORT | Both | 更新中断 |
| 0x19 | FW_VERIFY | H→D | 検証要求 |
| 0x1A | FW_VERIFY_RES | D→H | 検証結果 |

### 4.4 更新フロー

```
┌──────────┐                              ┌──────────┐
│   Host   │                              │  Device  │
└────┬─────┘                              └────┬─────┘
     │                                         │
     │──── FW_QUERY ──────────────────────────>│
     │<─── FW_INFO (version, size, etc.) ─────│
     │                                         │
     │──── FW_BEGIN (total_size, crc32) ──────>│
     │<─── FW_BEGIN_ACK ──────────────────────│
     │                                         │
     │──── FW_DATA (seq=0, chunk) ────────────>│
     │<─── FW_DATA_ACK (seq=0) ───────────────│
     │──── FW_DATA (seq=1, chunk) ────────────>│
     │<─── FW_DATA_ACK (seq=1) ───────────────│
     │          ...                            │
     │──── FW_DATA (seq=N, last chunk) ───────>│
     │<─── FW_DATA_ACK (seq=N) ───────────────│
     │                                         │
     │──── FW_END ────────────────────────────>│
     │<─── FW_END_ACK (status) ───────────────│
     │                                         │
     │──── FW_VERIFY ─────────────────────────>│
     │<─── FW_VERIFY_RES (crc_ok) ────────────│
     │                                         │
```

### 4.5 FW_INFO ペイロード

```
Byte 0-3:   Current FW Version (Major.Minor.Patch.Build)
Byte 4-7:   Bootloader Version
Byte 8-11:  Flash Size (uint32, LE)
Byte 12-15: Page Size (uint32, LE)
Byte 16-19: Max Chunk Size (uint32, LE)
Byte 20:    Flags
            bit 0: Bootloader mode active
            bit 1: Dual bank supported
            bit 2: Encryption required
            bit 3: Signature required
Byte 21+:   Device Name (null-terminated UTF-8)
```

### 4.6 FW_DATA ペイロード

```
Byte 0-1:   Packet Sequence (uint16, LE)
Byte 2-5:   Offset in firmware (uint32, LE)
Byte 6+:    Encoded firmware data (max 512 bytes before encoding)
```

### 4.7 エラー処理

| Error Code | 名前 | 説明 |
|------------|------|------|
| 0x00 | OK | 成功 |
| 0x01 | ERR_CRC | CRCエラー |
| 0x02 | ERR_SIZE | サイズ不正 |
| 0x03 | ERR_SEQ | シーケンスエラー |
| 0x04 | ERR_FLASH | Flash書き込み失敗 |
| 0x05 | ERR_VERIFY | 検証失敗 |
| 0x06 | ERR_BUSY | デバイスビジー |
| 0x07 | ERR_AUTH | 認証失敗 |

## 5. UXMP-SHELL (Shell over MIDI)

### 5.1 概要

対話的なコマンドラインインターフェース。人間が直接操作する用途。

### 5.2 コマンド

| Command | 名前 | 方向 | 説明 |
|---------|------|------|------|
| 0x20 | SHELL_OPEN | H→D | シェルセッション開始 |
| 0x21 | SHELL_OPEN_ACK | D→H | セッション開始確認 |
| 0x22 | SHELL_CLOSE | H→D | セッション終了 |
| 0x23 | SHELL_INPUT | H→D | コマンド入力 |
| 0x24 | SHELL_OUTPUT | D→H | 出力データ |
| 0x25 | SHELL_PROMPT | D→H | プロンプト表示 |
| 0x26 | SHELL_COMPLETE | H→D | タブ補完要求 |
| 0x27 | SHELL_COMPLETE_RES | D→H | 補完候補 |

### 5.3 STDIOとの関係

```
┌─────────────────────────────────────────────────────────────┐
│                    SHELL と STDIO の関係                     │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  UXMP-SHELL                      UXMP-STDIO                 │
│  ┌─────────────┐                ┌─────────────┐            │
│  │ SHELL_INPUT │ ───────────────> STDIN_DATA  │            │
│  │             │                │             │            │
│  │SHELL_OUTPUT │ <─────────────── STDOUT_DATA │            │
│  │             │                │             │            │
│  │SHELL_PROMPT │ (シェル固有)   │ STDERR_DATA │            │
│  └─────────────┘                └─────────────┘            │
│                                                             │
│  【違い】                                                    │
│  - SHELL: 対話的、プロンプト/補完あり、人間向け             │
│  - STDIO: ストリーム、プログラムからの自動操作向け          │
│                                                             │
│  【実装共通化】                                               │
│  - 内部のコマンドパーサーは同一                             │
│  - 出力フォーマットは同一                                   │
│  - SHELLはSTDIOのラッパーとして実装可能                     │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 5.4 標準コマンド (推奨)

| コマンド | 説明 |
|---------|------|
| `help` | コマンド一覧 |
| `version` | ファームウェアバージョン |
| `status` | システム状態 |
| `get <param>` | パラメータ取得 |
| `set <param> <value>` | パラメータ設定 |
| `list params` | パラメータ一覧 |
| `list presets` | プリセット一覧 |
| `save` | 設定保存 |
| `reset` | 設定リセット |
| `reboot` | 再起動 |

## 6. UXMP-TEST (Test over MIDI)

### 6.1 概要

自動テスト・品質管理用プロトコル。WebツールやCI/CDからの操作。

### 6.2 コマンド

| Command | 名前 | 方向 | 説明 |
|---------|------|------|------|
| 0x30 | TEST_LIST | H→D | テスト一覧要求 |
| 0x31 | TEST_LIST_RES | D→H | テスト一覧 |
| 0x32 | TEST_RUN | H→D | テスト実行 |
| 0x33 | TEST_RESULT | D→H | テスト結果 |
| 0x34 | TEST_ABORT | H→D | テスト中断 |
| 0x35 | DIAG_REQ | H→D | 診断情報要求 |
| 0x36 | DIAG_RES | D→H | 診断情報応答 |

### 6.3 TEST_RESULT ペイロード

```
Byte 0:     Test ID
Byte 1:     Result (0=PASS, 1=FAIL, 2=SKIP, 3=ERROR)
Byte 2-5:   Duration (ms, uint32 LE)
Byte 6+:    Message (null-terminated UTF-8, optional)
```

## 7. UXMP-STATUS (Status & Logging)

### 7.1 概要

デバイス状態の監視とログ取得。UXMP-SHELLと機能は重複するが、プログラムからの効率的アクセス用。

### 7.2 コマンド

| Command | 名前 | 方向 | 説明 |
|---------|------|------|------|
| 0x40 | IDENTITY_REQ | H→D | デバイス識別要求 |
| 0x41 | IDENTITY_RES | D→H | デバイス識別応答 |
| 0x42 | STATUS_REQ | H→D | 状態要求 |
| 0x43 | STATUS_RES | D→H | 状態応答 |
| 0x44 | AUDIO_STATUS_REQ | H→D | オーディオ状態要求 |
| 0x45 | AUDIO_STATUS_RES | D→H | オーディオ状態応答 |
| 0x46 | LOG_SUBSCRIBE | H→D | ログ購読 |
| 0x47 | LOG_DATA | D→H | ログデータ |
| 0x48 | METER_SUBSCRIBE | H→D | メーター購読 |
| 0x49 | METER_DATA | D→H | メーターデータ |

### 7.3 IDENTITY_RES ペイロード

```
Byte 0-2:   Manufacturer ID (MIDI format)
Byte 3-4:   Device Family (uint16 BE)
Byte 5-6:   Device Model (uint16 BE)
Byte 7:     FW Version Major
Byte 8:     FW Version Minor
Byte 9-10:  FW Version Patch (uint16 BE)
Byte 11:    Protocol Version Major
Byte 12:    Protocol Version Minor
Byte 13+:   Device Name (null-terminated UTF-8)
```

### 7.4 AUDIO_STATUS_RES ペイロード

```
Byte 0-1:   DSP Load (uint16, ×100, e.g., 5600 = 56.00%)
Byte 2-4:   Sample Rate (uint24)
Byte 5-6:   Buffer Size (uint16, samples)
Byte 7-8:   Input Latency (uint16, samples)
Byte 9-10:  Output Latency (uint16, samples)
Byte 11:    Current Polyphony
Byte 12:    Max Polyphony
Byte 13:    Flags
            bit 0: Audio Running
            bit 1: Clipping Detected
            bit 2: Underrun Detected
            bit 3: Overrun Detected
Byte 14-17: Underrun Count (uint32)
Byte 18-21: Overrun Count (uint32)
Byte 22-25: Uptime (uint32, seconds)
```

### 7.5 サブスクリプション

#### LOG_SUBSCRIBE

```
Byte 0:     Action (0=unsubscribe, 1=subscribe)
Byte 1:     Log Level (0=ALL, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR)
Byte 2-3:   Rate Limit (ms, 0=every message)
```

#### METER_SUBSCRIBE

```
Byte 0:     Action (0=unsubscribe, 1=subscribe)
Byte 1:     Channel Mask (bit field)
Byte 2-3:   Update Interval (ms, minimum 50ms)
```

## 8. UXMP-DATA (User Data Exchange)

### 8.1 概要

プリセット、パターン、サンプル等のユーザーデータ交換。

### 8.2 既存仕様の分析

#### MIDI Sample Dump Standard (SDS)

```
- 採用: 1986年、MMA/AMEI標準
- 形式: ヘッダー + データパケット (120バイト/パケット)
- ハンドシェイク: ACK/NAK/WAIT/CANCEL
- 課題: 遅い (120バイト制限)、サンプル専用
```

#### KORG logue SDK

```
- 形式: ZIPアーカイブ (.prlgunit等)
- 内容: バイナリ + manifest.json
- パラメータ: 6個まで
- 転送: 非公開プロトコル
```

#### Elektron

```
- 対応: プロジェクト、パターン、サウンド
- 非対応: サンプル (Transfer app必須)
- フロー制御: 20ms間隔
```

### 8.3 データカテゴリ

| Category | ID | 説明 | 典型サイズ |
|----------|-----|------|-----------|
| Preset | 0x01 | シンセパッチ | 256B - 4KB |
| Pattern | 0x02 | シーケンスパターン | 1KB - 64KB |
| Song | 0x03 | ソングデータ | 4KB - 256KB |
| Sample | 0x04 | オーディオサンプル | 1KB - 16MB |
| Wavetable | 0x05 | ウェーブテーブル | 2KB - 1MB |
| Project | 0x06 | プロジェクト全体 | 64KB - 64MB |
| Config | 0x07 | 設定データ | 64B - 4KB |
| Custom | 0x10-0x7F | ベンダー固有 | - |

### 8.4 コマンド

| Command | 名前 | 方向 | 説明 |
|---------|------|------|------|
| 0x50 | DATA_LIST_REQ | H→D | データ一覧要求 |
| 0x51 | DATA_LIST_RES | D→H | データ一覧応答 |
| 0x52 | DATA_INFO_REQ | H→D | データ情報要求 |
| 0x53 | DATA_INFO_RES | D→H | データ情報応答 |
| 0x54 | DATA_READ_REQ | H→D | データ読み出し開始 |
| 0x55 | DATA_READ_RES | D→H | データ読み出し応答 |
| 0x56 | DATA_WRITE_REQ | H→D | データ書き込み開始 |
| 0x57 | DATA_WRITE_ACK | D→H | 書き込み開始確認 |
| 0x58 | DATA_CHUNK | Both | データチャンク |
| 0x59 | DATA_CHUNK_ACK | Both | チャンク確認 |
| 0x5A | DATA_END | Both | 転送完了 |
| 0x5B | DATA_END_ACK | Both | 完了確認 |
| 0x5C | DATA_DELETE | H→D | データ削除 |
| 0x5D | DATA_DELETE_ACK | D→H | 削除確認 |

### 8.5 DATA_LIST_RES ペイロード

```
Byte 0:     Category
Byte 1:     Total Count
Byte 2:     Offset (this message)
Byte 3:     Count in this message
For each item:
  Byte 0-1:   Item ID (uint16)
  Byte 2-3:   Size (KB, uint16)
  Byte 4:     Flags
              bit 0: Factory
              bit 1: Modified
              bit 2: Locked
  Byte 5:     Name Length
  Byte 6+:    Name (UTF-8)
```

### 8.6 DATA_INFO_RES ペイロード

```
Byte 0:     Category
Byte 1-2:   Item ID (uint16)
Byte 3-6:   Size (bytes, uint32 LE)
Byte 7-10:  CRC32
Byte 11-14: Created Timestamp (Unix time, uint32)
Byte 15-18: Modified Timestamp (Unix time, uint32)
Byte 19:    Format Version
Byte 20:    Flags
Byte 21:    Name Length
Byte 22+:   Name (UTF-8)
```

### 8.7 チャンク転送プロトコル

```
┌──────────┐                              ┌──────────┐
│   Host   │                              │  Device  │
└────┬─────┘                              └────┬─────┘
     │                                         │
     │──── DATA_READ_REQ (cat, id) ───────────>│
     │<─── DATA_READ_RES (size, crc) ─────────│
     │                                         │
     │<─── DATA_CHUNK (seq=0, data) ──────────│
     │──── DATA_CHUNK_ACK (seq=0) ────────────>│
     │<─── DATA_CHUNK (seq=1, data) ──────────│
     │──── DATA_CHUNK_ACK (seq=1) ────────────>│
     │          ...                            │
     │<─── DATA_END ──────────────────────────│
     │──── DATA_END_ACK ──────────────────────>│
     │                                         │
```

### 8.8 サンプルデータ形式 (UXMP-SAMPLE)

#### ヘッダー (SDS互換拡張)

```
Byte 0-1:   Sample Number (uint16)
Byte 2:     Bit Depth (8, 16, 24, 32)
Byte 3:     Channels (1=mono, 2=stereo)
Byte 4-7:   Sample Rate (uint32 LE)
Byte 8-11:  Sample Count (uint32 LE)
Byte 12-15: Loop Start (uint32 LE)
Byte 16-19: Loop End (uint32 LE)
Byte 20:    Loop Type (0=none, 1=forward, 2=pingpong, 3=reverse)
Byte 21:    Root Note (MIDI note number)
Byte 22-23: Fine Tune (cents, int16)
Byte 24-27: Gain (float, normalized)
Byte 28:    Name Length
Byte 29+:   Name (UTF-8)
```

### 8.9 プリセット/パラメータ形式 (UXMP-PRESET)

#### 設計原則

1. **自己記述的**: パラメータのメタデータを含む
2. **バージョン互換**: 前方/後方互換性を考慮
3. **圧縮対応**: 大きなプリセットはオプションで圧縮

#### ヘッダー

```
Byte 0-3:   Magic ("UXPR" = 0x55 0x58 0x50 0x52)
Byte 4-5:   Format Version (uint16)
Byte 6-7:   Device Family (uint16)
Byte 8-9:   Device Model (uint16)
Byte 10-13: Preset Size (uint32)
Byte 14-17: CRC32
Byte 18:    Flags
            bit 0: Compressed (zlib)
            bit 1: Encrypted
            bit 2: Signed
Byte 19:    Parameter Count
Byte 20:    Name Length
Byte 21+:   Name (UTF-8)
```

#### パラメータエントリ

```
Byte 0:     Parameter ID
Byte 1:     Type (0=int8, 1=int16, 2=int32, 3=float, 4=string)
Byte 2-N:   Value (type dependent)
```

### 8.10 パターン/シーケンス形式 (UXMP-PATTERN)

#### ヘッダー

```
Byte 0-3:   Magic ("UXPT" = 0x55 0x58 0x50 0x54)
Byte 4-5:   Format Version (uint16)
Byte 6-7:   Pattern Length (bars)
Byte 8-9:   Time Signature Numerator (uint16)
Byte 10-11: Time Signature Denominator (uint16)
Byte 12-15: Tempo (BPM × 100, uint32)
Byte 16:    Track Count
Byte 17:    Flags
            bit 0: Compressed
            bit 1: Includes automation
Byte 18:    Name Length
Byte 19+:   Name (UTF-8)
```

#### トラックデータ

```
For each track:
  Byte 0:     Track ID
  Byte 1:     Track Type (0=MIDI, 1=Audio, 2=Automation)
  Byte 2-3:   Event Count (uint16)
  For each event:
    Byte 0-3:   Tick Position (uint32)
    Byte 4:     Event Type
    Byte 5-N:   Event Data
```

#### イベントタイプ

| Type | 名前 | データ |
|------|------|--------|
| 0x00 | Note On | note, velocity |
| 0x01 | Note Off | note, velocity |
| 0x02 | CC | cc_num, value |
| 0x03 | Pitch Bend | value (int16) |
| 0x04 | Aftertouch | value |
| 0x10 | Automation | param_id, value |

## 9. 実装ガイドライン

### 9.1 最小実装 (Minimal)

必須機能のみ:

- UXMP-STATUS: IDENTITY_REQ/RES のみ
- Universal Device Inquiry への応答

### 9.2 基本実装 (Basic)

シェルとファームウェア更新:

- UXMP-STDIO: 全機能
- UXMP-DFU: 全機能
- UXMP-STATUS: IDENTITY, STATUS

### 9.3 フル実装 (Full)

全プロトコル対応:

- 全UXMP-*プロトコル
- UXMP-DATA: 全カテゴリ

### 9.4 処理優先度

```
┌─────────────────────────────────────────────────────────────┐
│                    処理優先度ガイドライン                     │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Realtime (最高)                                            │
│    - MIDI Note/CC/PitchBend                                │
│    - Timing Clock                                           │
│    - オーディオDMA                                          │
│                                                             │
│  Server (中)                                                │
│    - SysExパケット受信・組み立て                            │
│    - MIDIメッセージの振り分け                               │
│                                                             │
│  Idle (最低)                                                │
│    - 全UXMP-*プロトコル処理                                 │
│    - シェルコマンド実行                                     │
│    - データ転送処理                                         │
│    - ログ出力                                               │
│                                                             │
│  【原則】                                                    │
│  要求がなければ応答しない                                   │
│  SysEx処理がリアルタイム処理に影響を与えてはならない        │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## 10. 既存プロトコルとの互換性

### 10.1 Universal Device Inquiry

標準のデバイス識別に応答すること:

```
Request:  F0 7E 7F 06 01 F7
Response: F0 7E <DevID> 06 02 <MfgID> <Family> <Model> <Version> F7
```

### 10.2 MIDI Sample Dump Standard

UXMP-DATAのサンプル転送はSDSと相互運用可能であるべき:

- SDSヘッダーとの相互変換をサポート
- SDSハンドシェイク (ACK/NAK/WAIT/CANCEL) に対応可能

### 10.3 MIDI-CI

MIDI-CIのProperty Exchangeとの概念的な互換性:

- リソース名 → カテゴリID + アイテムID
- JSON/CBOR → バイナリ形式 (変換レイヤーで対応可能)

## 11. セキュリティ考慮事項

### 11.1 ファームウェア更新

- オプション: 署名検証 (Ed25519推奨)
- オプション: 暗号化 (ChaCha20-Poly1305推奨)
- 必須: CRC32検証

### 11.2 データ保護

- 工場プリセット: 書き込み保護
- ユーザーデータ: オプションでロック機能

### 11.3 アクセス制御

- オプション: PIN/パスワード認証
- オプション: 接続元の制限

## 12. 今後の課題

### 12.1 標準化

1. MMA/AMEIへのUniversal SysEx Sub-ID申請
2. 仕様書の英語版作成
3. リファレンス実装の公開
4. 相互運用性テストスイートの作成

### 12.2 拡張予定

1. MIDI 2.0トランスポート対応 (SysEx8)
2. Bluetooth MIDI対応
3. 圧縮転送の標準化
4. マルチデバイス同期

## 付録A: メッセージ一覧

| Protocol | Cmd | 名前 | 方向 |
|----------|-----|------|------|
| STDIO | 0x01 | STDOUT_DATA | D→H |
| STDIO | 0x02 | STDERR_DATA | D→H |
| STDIO | 0x03 | STDIN_DATA | H→D |
| STDIO | 0x04 | STDIN_EOF | H→D |
| STDIO | 0x05 | FLOW_CTRL | Both |
| DFU | 0x10 | FW_QUERY | H→D |
| DFU | 0x11 | FW_INFO | D→H |
| DFU | 0x12 | FW_BEGIN | H→D |
| DFU | 0x13 | FW_BEGIN_ACK | D→H |
| DFU | 0x14 | FW_DATA | H→D |
| DFU | 0x15 | FW_DATA_ACK | D→H |
| DFU | 0x16 | FW_END | H→D |
| DFU | 0x17 | FW_END_ACK | D→H |
| DFU | 0x18 | FW_ABORT | Both |
| DFU | 0x19 | FW_VERIFY | H→D |
| DFU | 0x1A | FW_VERIFY_RES | D→H |
| SHELL | 0x20 | SHELL_OPEN | H→D |
| SHELL | 0x21 | SHELL_OPEN_ACK | D→H |
| SHELL | 0x22 | SHELL_CLOSE | H→D |
| SHELL | 0x23 | SHELL_INPUT | H→D |
| SHELL | 0x24 | SHELL_OUTPUT | D→H |
| SHELL | 0x25 | SHELL_PROMPT | D→H |
| SHELL | 0x26 | SHELL_COMPLETE | H→D |
| SHELL | 0x27 | SHELL_COMPLETE_RES | D→H |
| TEST | 0x30 | TEST_LIST | H→D |
| TEST | 0x31 | TEST_LIST_RES | D→H |
| TEST | 0x32 | TEST_RUN | H→D |
| TEST | 0x33 | TEST_RESULT | D→H |
| TEST | 0x34 | TEST_ABORT | H→D |
| TEST | 0x35 | DIAG_REQ | H→D |
| TEST | 0x36 | DIAG_RES | D→H |
| STATUS | 0x40 | IDENTITY_REQ | H→D |
| STATUS | 0x41 | IDENTITY_RES | D→H |
| STATUS | 0x42 | STATUS_REQ | H→D |
| STATUS | 0x43 | STATUS_RES | D→H |
| STATUS | 0x44 | AUDIO_STATUS_REQ | H→D |
| STATUS | 0x45 | AUDIO_STATUS_RES | D→H |
| STATUS | 0x46 | LOG_SUBSCRIBE | H→D |
| STATUS | 0x47 | LOG_DATA | D→H |
| STATUS | 0x48 | METER_SUBSCRIBE | H→D |
| STATUS | 0x49 | METER_DATA | D→H |
| DATA | 0x50 | DATA_LIST_REQ | H→D |
| DATA | 0x51 | DATA_LIST_RES | D→H |
| DATA | 0x52 | DATA_INFO_REQ | H→D |
| DATA | 0x53 | DATA_INFO_RES | D→H |
| DATA | 0x54 | DATA_READ_REQ | H→D |
| DATA | 0x55 | DATA_READ_RES | D→H |
| DATA | 0x56 | DATA_WRITE_REQ | H→D |
| DATA | 0x57 | DATA_WRITE_ACK | D→H |
| DATA | 0x58 | DATA_CHUNK | Both |
| DATA | 0x59 | DATA_CHUNK_ACK | Both |
| DATA | 0x5A | DATA_END | Both |
| DATA | 0x5B | DATA_END_ACK | Both |
| DATA | 0x5C | DATA_DELETE | H→D |
| DATA | 0x5D | DATA_DELETE_ACK | D→H |

## 付録B: 参考文献

1. MIDI 1.0 Detailed Specification (MMA)
2. MIDI Sample Dump Standard (MMA, 1986)
3. Universal System Exclusive Messages (MMA)
4. MIDI 2.0 Specification (MMA/AMEI)
5. MIDI-CI Specification 1.2
6. USB MIDI 1.0 Specification
7. KORG logue SDK Documentation
8. Elektron Transfer Protocol (reverse engineered)
9. Roland SysEx Implementation Guide

## 変更履歴

| 日付 | バージョン | 変更内容 |
|------|-----------|----------|
| 2026-01-26 | 0.1.0 | 初版ドラフト |
