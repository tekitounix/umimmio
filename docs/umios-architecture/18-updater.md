# 18 — Updater (DFU over SysEx)

## 概要

SysEx 経由でのファームウェア/アプリバイナリ更新（DFU: Device Firmware Update）。
SystemTask 上で動作し、ホスト PC との通信で Flash の書き込み・検証・ロールバックを行う。

| 項目 | 状態 |
|------|------|
| DFU SysEx プロトコル | 実装済み |
| CRC32 完全性検証 | 実装済み |
| Ed25519 署名検証 | 実装済み |
| ロールバック | 実装済み |
| SystemMode 切替 | 実装済み |

---

## アーキテクチャ

```
Host PC (umi-tools / Web UI)
  │
  │  USB MIDI SysEx
  ▼
OTG_FS ISR → SysEx バッファ → notify(SystemTask)
  │
  ▼
SystemTask → Updater
  ├─ FW_QUERY / FW_INFO: デバイス情報問い合わせ
  ├─ FW_BEGIN:  バッファ確保、転送開始
  ├─ FW_DATA:   データチャンク受信 → Flash 書き込み
  ├─ FW_VERIFY: CRC32 + Ed25519 署名検証
  ├─ FW_COMMIT: メタデータ更新、確定
  ├─ FW_ROLLBACK: 前バージョンへのロールバック
  └─ FW_REBOOT: システム再起動
```

---

## DFU プロトコル

### コマンド一覧

| コマンド | 方向 | 説明 |
|---------|------|------|
| FW_QUERY | Host → Device | デバイス情報問い合わせ |
| FW_INFO | Device → Host | ファームウェア情報応答 |
| FW_BEGIN | Host → Device | 更新開始（対象、サイズ、CRC） |
| FW_DATA | Host → Device | データチャンク転送 |
| FW_ACK | Device → Host | 応答（成功/エラー） |
| FW_VERIFY | Host → Device | 転送完了後の検証要求 |
| FW_COMMIT | Host → Device | 検証成功後の確定 |
| FW_ROLLBACK | Host → Device | 前バージョンへのロールバック |
| FW_REBOOT | Host → Device | システム再起動 |

### 更新フロー

```
Host                          Device (SystemTask)
  │                              │
  │  FW_QUERY ──────────────→   │
  │  ←────────────── FW_INFO    │  (デバイス ID、現在の FW バージョン)
  │                              │
  │  FW_BEGIN ──────────────→   │  (対象: kernel|app, サイズ, CRC)
  │  ←──────────────── FW_ACK   │  (バッファ確保、Flash erase 開始)
  │                              │
  │  FW_DATA × N ───────────→  │  (256B チャンク × N)
  │  ←──────────────── FW_ACK   │  (Flash 書き込み、進捗通知)
  │                              │
  │  FW_VERIFY ─────────────→  │
  │  ←──────────────── FW_ACK   │  (CRC32 + Ed25519 署名検証)
  │                              │
  │  FW_COMMIT ─────────────→  │  (メタデータ更新、確定)
  │  ←──────────────── FW_ACK   │
  │                              │
  │  FW_REBOOT ─────────────→  │  (NVIC_SystemReset)
```

### 更新対象

| 対象 | Flash 領域 | 検証 |
|------|-----------|------|
| Kernel | Flash Bank 0 | CRC32 + 署名（Release） |
| App (.umia) | Flash Bank 1（アプリ領域） | CRC32 + 署名（Release） |

---

## データチャンク転送

### SysEx エンコーディング

MIDI SysEx は 7-bit データのみ送信可能。8-bit バイナリデータは 7-bit エンコーディングで転送する:

```
7 バイト入力 → 8 バイト出力
各 7 バイトの MSB を 1 バイトのヘッダに集約
```

### チャンクサイズ

- デフォルト: 256 バイト（エンコード後約 293 バイト）
- USB Full-Speed の SysEx パケットサイズ制限を考慮
- ACK を待ってから次のチャンクを送信（フロー制御）

---

## 検証

### CRC32

転送データ全体の CRC32 を計算し、FW_BEGIN で指定された期待値と比較する。
破損検出のみ（改ざん検出には不十分）。

### Ed25519 署名

Release ビルドの場合、カーネル内蔵の公開鍵で署名を検証する。
署名検証の詳細は [14-security.md](14-security.md) を参照。

---

## ロールバック

Flash 上に前バージョンのメタデータを保持し、FW_ROLLBACK コマンドで復元する。

```
Flash メタデータ領域:
  ├─ current_version
  ├─ current_crc32
  ├─ previous_version
  ├─ previous_offset
  └─ rollback_available: bool
```

---

## SystemMode

Shell の `mode` コマンドまたは内部遷移で DFU モードに切り替える:

```cpp
enum class SystemMode : uint8_t {
    Normal     = 0,   // 通常動作
    Dfu        = 1,   // DFU モード（Updater アクティブ）
    Bootloader = 2,   // ブートローダーモード
    Safe       = 3,   // セーフモード（アプリ未ロード）
};
```

DFU モード中:
- アプリの実行を停止
- AudioTask を無効化
- Updater のみがアクティブ
- Shell は引き続き利用可能（進捗確認用）

---

## エラーハンドリング

| エラー | 対応 |
|--------|------|
| チャンク CRC 不一致 | 再送要求（ACK にエラーコード） |
| 転送中断 | タイムアウトで自動キャンセル |
| 全体 CRC 不一致 | FW_VERIFY 失敗、書き込み済みデータを無効化 |
| 署名検証失敗 | FW_VERIFY 失敗、書き込み済みデータを無効化 |
| Flash 書き込みエラー | ACK にエラーコード、転送中断 |
| 電断 | 前バージョンが有効なまま（COMMITされるまで切り替わらない） |

---

## 関連ドキュメント

- [05-midi.md](05-midi.md) — SysEx トランスポート、7-bit エンコーディング
- [14-security.md](14-security.md) — Ed25519 署名検証、CRC32
- [17-shell.md](17-shell.md) — `mode dfu` コマンド
