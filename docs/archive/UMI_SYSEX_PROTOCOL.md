# UMI SysEx プロトコル仕様書

バージョン: 1.0.0
ステータス: ドラフト

**移行メモ:** UMI-SysEx へ統合完了。本書は参考資料。
最新は「docs/new/umi-sysex/UMI_SYSEX_TRANSPORT.md」。

## 概要

UMI SysEx プロトコルは、UMIデバイスとホストアプリケーション間の通信に使用するMIDI System Exclusive (SysEx) ベースのプロトコルです。

主な機能:
- **標準I/O**: シェルのstdin/stdout/stderrをMIDI経由で転送
- **ファームウェア更新**: A/Bパーティション対応のセキュアな更新
- **システム制御**: Ping/Pong、リセット、バージョン照会

## メッセージフォーマット

全てのUMI SysExメッセージは以下の構造に従います:

```
F0 <ID> <CMD> <SEQ> [PAYLOAD...] <CHECKSUM> F7
```

| フィールド | バイト数 | 説明                                |
|-----------|---------|-------------------------------------|
| F0        | 1       | SysEx開始バイト                      |
| ID        | 3       | UMI SysEx ID: `7E 7F 00`            |
| CMD       | 1       | コマンドバイト（後述）                |
| SEQ       | 1       | シーケンス番号 (0-127)               |
| PAYLOAD   | 0-N     | 7-bitエンコードされたペイロード       |
| CHECKSUM  | 1       | CMD+SEQ+PAYLOADのXORチェックサム      |
| F7        | 1       | SysEx終了バイト                      |

### 最小メッセージサイズ

最小の有効メッセージ: 8バイト (F0 + ID(3) + CMD + SEQ + CHECKSUM + F7)

### UMI SysEx ID

```c++
constexpr uint8_t UMI_SYSEX_ID[] = {0x7E, 0x7F, 0x00};
```

注: 現在はプレースホルダーIDを使用。製品版では正式なMIDI Manufacturer IDを登録する必要があります。

## 7-bitエンコーディング

MIDI SysExでは全てのデータバイトが7-bit (0x00-0x7F) である必要があります。UMIでは以下のエンコーディングで8-bitデータを転送します:

### エンコードアルゴリズム

入力7バイトごとに8バイトを出力:
1. 最初のバイト: MSB収集バイト（ビットN = バイトNの最上位ビット）
2. 続く7バイト: 各入力バイトの下位7ビット

```c++
// 8-bitデータを7-bitにエンコード
for (i = 0; i < in_len; i += 7) {
    uint8_t msb_byte = 0;
    for (j = 0; j < 7 && (i+j) < in_len; j++) {
        if (in[i + j] & 0x80) {
            msb_byte |= (1 << j);
        }
    }
    out[pos++] = msb_byte;
    for (j = 0; j < 7 && (i+j) < in_len; j++) {
        out[pos++] = in[i + j] & 0x7F;
    }
}
```

### サイズ計算

- エンコード後サイズ: `(in_len / 7) * 8 + (in_len % 7 ? (in_len % 7) + 1 : 0)`
- デコード後サイズ: `(in_len / 8) * 7 + (in_len % 8 ? (in_len % 8) - 1 : 0)`

## チェックサム

コマンドデータ (CMD + SEQ + PAYLOAD) のXORチェックサム:

```c++
uint8_t checksum = 0;
for (size_t i = 0; i < len; i++) {
    checksum ^= data[i];
}
return checksum & 0x7F;
```

## コマンドリファレンス

### 標準I/O (0x01-0x0F)

| コマンド     | 値   | 方向            | 説明                    |
|-------------|------|-----------------|------------------------|
| STDOUT_DATA | 0x01 | デバイス → ホスト | 標準出力データ           |
| STDERR_DATA | 0x02 | デバイス → ホスト | 標準エラー出力データ      |
| STDIN_DATA  | 0x03 | ホスト → デバイス | 標準入力データ           |
| STDIN_EOF   | 0x04 | ホスト → デバイス | 入力ストリーム終了        |
| FLOW_CTRL   | 0x05 | 双方向           | フロー制御 (XON/XOFF)    |

#### STDOUT_DATA / STDERR_DATA / STDIN_DATA

ペイロード: 7-bitエンコードされたテキストデータ (UTF-8)

stdinに "hello" を送信する例:
```
F0 7E 7F 00 03 00 <エンコードされた "hello\r"> <checksum> F7
```

#### FLOW_CTRL

ペイロード: 1バイト
- `0x11` (XON): 送信再開
- `0x13` (XOFF): 送信一時停止

### ファームウェア更新 (0x10-0x1F)

| コマンド     | 値   | 方向            | 説明                     |
|-------------|------|-----------------|--------------------------|
| FW_QUERY    | 0x10 | ホスト → デバイス | ファームウェア情報照会     |
| FW_INFO     | 0x11 | デバイス → ホスト | ファームウェア情報応答     |
| FW_BEGIN    | 0x12 | ホスト → デバイス | ファームウェア更新開始     |
| FW_DATA     | 0x13 | ホスト → デバイス | ファームウェアデータチャンク |
| FW_VERIFY   | 0x14 | ホスト → デバイス | ファームウェア検証         |
| FW_COMMIT   | 0x15 | ホスト → デバイス | ファームウェア更新確定     |
| FW_ROLLBACK | 0x16 | ホスト → デバイス | 前バージョンにロールバック  |
| FW_REBOOT   | 0x17 | ホスト → デバイス | デバイス再起動要求         |
| FW_ACK      | 0x18 | デバイス → ホスト | 肯定応答                  |
| FW_NACK     | 0x19 | デバイス → ホスト | 否定応答                  |

#### FW_INFO 応答

ペイロード (7-bitエンコード):
- バイト 0-3: 現在のバージョン (uint32_t, ビッグエンディアン)
- バイト 4-7: 最大ファームウェアサイズ (uint32_t)
- バイト 8: アクティブスロット (0=A, 1=B)
- バイト 9: ロールバック可能フラグ (0/1)

### システムコマンド (0x20-0x2F)

| コマンド         | 値   | 方向            | 説明                |
|-----------------|------|-----------------|---------------------|
| PING            | 0x20 | ホスト → デバイス | 接続確認             |
| PONG            | 0x21 | デバイス → ホスト | Ping応答            |
| RESET           | 0x22 | ホスト → デバイス | デバイス/プロトコルリセット |
| VERSION         | 0x23 | デバイス → ホスト | プロトコルバージョン   |
| STATUS_REQUEST  | 0x24 | ホスト → デバイス | システム状態要求      |
| STATUS_RESPONSE | 0x25 | デバイス → ホスト | システム状態応答      |

注: STATUS_REQUEST/STATUS_RESPONSEはWeb側(protocol.js)で定義されていますが、C++側(commands.hh)には未定義です。

#### VERSION 応答

ペイロード:
- バイト 0: メジャーバージョン
- バイト 1: マイナーバージョン

#### STATUS_RESPONSE

ペイロード (7-bitエンコード):
- バイト 0-1: DSP負荷 (uint16_t, 値×100, 例: 5600 = 56.00%)
- バイト 2: サンプルレート (値×1000, 例: 48 = 48000 Hz)
- バイト 3: バッファサイズ (値×16, 例: 16 = 256サンプル)
- バイト 4-7: 稼働時間（秒） (uint32_t)

## エラーコード

| コード | 名前                | 説明                          |
|-------|---------------------|------------------------------|
| 0x00  | OK                  | 成功                          |
| 0x01  | INVALID_COMMAND     | 不明または無効なコマンド         |
| 0x02  | INVALID_SEQUENCE    | シーケンス番号不一致            |
| 0x03  | INVALID_CHECKSUM    | チェックサム検証失敗            |
| 0x04  | BUFFER_OVERFLOW     | データがバッファサイズを超過     |
| 0x05  | UPDATE_NOT_STARTED  | 更新が開始されていない          |
| 0x06  | UPDATE_IN_PROGRESS  | 更新が既に進行中               |
| 0x07  | VERIFY_FAILED       | ファームウェア検証失敗          |
| 0x08  | SIGNATURE_INVALID   | 署名検証失敗                   |
| 0x09  | FLASH_ERROR         | フラッシュ書き込みエラー         |
| 0x0A  | ROLLBACK_UNAVAIL    | ロールバック用ファームウェアなし  |
| 0x0B  | TIMEOUT             | 操作タイムアウト               |

## 実装ファイル

### C++ (ファームウェア側)

- `lib/umidi/include/protocol/commands.hh` - コマンド定義
- `lib/umidi/include/protocol/encoding.hh` - 7-bitエンコーディング
- `lib/umidi/include/protocol/message.hh` - メッセージビルダー/パーサー
- `lib/umidi/include/protocol/standard_io.hh` - 標準I/O
- `lib/umidi/include/protocol/umi_sysex.hh` - 統合ハンドラー

### JavaScript (Web側)

- `examples/headless_webhost/web/lib/umi_web/core/protocol.js` - プロトコルユーティリティ
- `examples/headless_webhost/web/lib/umi_web/core/backends/hardware.js` - ハードウェアバックエンド

## 使用例

### シェルコマンド送信 (JavaScript)

```javascript
import { Command, buildMessage } from './protocol.js';

function sendShellCommand(output, text) {
    const encoder = new TextEncoder();
    const data = encoder.encode(text + '\r');
    const msg = buildMessage(Command.STDIN_DATA, txSequence++, data);
    output.send(msg);
}
```

### シェル出力処理 (JavaScript)

```javascript
function onMidiMessage(event) {
    if (event.data[0] !== 0xF0) return; // SysExではない

    const msg = parseMessage(event.data);
    if (!msg) return;

    if (msg.command === Command.STDOUT_DATA) {
        const text = new TextDecoder().decode(new Uint8Array(msg.payload));
        console.log('stdout:', text);
    }
}
```

### メッセージ構築 (C++)

```c++
#include "protocol/message.hh"

void send_stdout(const char* text, size_t len, SendFn send) {
    umidi::protocol::MessageBuilder<256> builder;
    builder.begin(umidi::protocol::Command::STDOUT_DATA, tx_seq_++);
    builder.add_data(reinterpret_cast<const uint8_t*>(text), len);
    send(builder.data(), builder.finalize());
}
```

## 既知の制限事項

1. **SysEx ID**: 現在は未登録のID (7E 7F 00) を使用。製品版では正式なMIDI Manufacturer IDの登録が必要。

2. **最大ペイロード**: MIDIバッファサイズによる制限あり。推奨最大デコードペイロード: 1024バイト。

3. **フロー制御**: シンプルなXON/XOFF方式。標準I/Oにはスライディングウィンドウ方式のACKなし。

## 実装状況

| 機能 | C++ (commands.hh) | JavaScript (protocol.js) | 状態 |
|-----|-------------------|--------------------------|------|
| STDOUT_DATA | o | o | 実装済み |
| STDERR_DATA | o | o | 実装済み |
| STDIN_DATA | o | o | 実装済み |
| STDIN_EOF | o | o | 定義のみ |
| FLOW_CTRL | o | o | 定義のみ |
| FW_* | o | o | 実装済み |
| PING/PONG | o | o | 実装済み |
| RESET | o | o | 実装済み |
| VERSION | o | o | 実装済み |
| STATUS_REQUEST | - | o | **JS側のみ** |
| STATUS_RESPONSE | - | o | **JS側のみ** |

## 今後の拡張予定

1. **パラメータ制御**: SysExベースのパラメータ照会/設定（CC代替）
2. **ファイル転送**: SysEx経由の任意ファイルアップロード/ダウンロード
3. **デバイス探索**: マルチデバイスの列挙とアドレッシング
4. **STATUS_REQUEST/RESPONSE**: C++側への正式実装
