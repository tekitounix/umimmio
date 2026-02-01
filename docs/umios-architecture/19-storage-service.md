# 19 — StorageService

## 概要

ファイルシステムの非同期アクセスサービス。
SystemTask 上で動作し、アプリからの FS syscall を逐次処理する。

| 項目 | 状態 |
|------|------|
| Syscall インターフェース（番号定義） | 新設計 |
| StorageService（要求キュー） | 新設計 |
| littlefs 統合（内蔵/SPI Flash） | 新設計 |
| FATfs 統合（SD カード） | 新設計 |
| BlockDevice 抽象化 | 新設計 |

---

## アーキテクチャ

```
App (ControlTask)
  │
  │  fs::open() / read() / write() / close()
  │  → SVC → 要求キュー → 即 return
  ▼
StorageService (SystemTask)
  │
  │  要求受信 → FS 操作 → 完了通知
  ▼
ファイルシステム
  ├─ littlefs (内蔵/SPI Flash)
  └─ FATfs (SD カード)
  │
  ▼
BlockDevice
  ├─ InternalFlash
  ├─ SPI Flash (W25Qxx 等)
  ├─ FRAM / EEPROM
  └─ SD (SPI / SDIO)
```

---

## 非同期モデル

FS syscall は**非同期一本化**。要求発行と完了取得を分離する:

```
ControlTask:  fs::write(fd, buf, len)
                │
                ▼
              SVC → syscall handler → 要求を StorageService キューに投入
                │
                └─→ 即 return（ControlTask は他の処理を継続可能）

SystemTask:   wait_block() で要求受信
                │
                ▼
              littlefs::write() → BlockDevice::write()
              (erase 中は yield → ControlTask に CPU を渡す)
                │
                ▼
              完了 → notify(ControlTask, FS)

ControlTask:  wait_event(event::FS) → 結果取得
```

### 同期ラッパー

同期的に使いたい場合はアプリ側ユーティリティでラップする:

```cpp
// アプリ側ユーティリティ（lib/umios/app/）
int open_sync(const char* path, int flags) {
    int fd = fs::open(path, flags);   // 非同期発行
    wait_event(event::FS);            // 完了待ち
    return fd;
}
```

Syscall レベルで同期/非同期の 2 形態を持つ必要はない。

---

## FS 選択

| メディア | FS | 理由 |
|---------|-----|------|
| 内蔵 Flash / SPI Flash | littlefs | 電断耐性（COW）、ウェアレベリング |
| SD カード | FATfs | PC 互換（FAT32） |

パスで自動振り分け:
- `/flash/...` → littlefs
- `/sd/...` → FATfs

---

## Syscall インターフェース

[06-syscall.md](06-syscall.md) §グループ 6 (60–69):

| Nr | 名前 | 引数 | 戻り値 | 状態 |
|----|------|------|--------|------|
| 60 | FileOpen | `path, flags` | fd / エラー | 将来 |
| 61 | FileRead | `fd, buf, len` | 読み取りバイト数 | 将来 |
| 62 | FileWrite | `fd, buf, len` | 書き込みバイト数 | 将来 |
| 63 | FileClose | `fd` | 0 / エラー | 将来 |
| 64 | FileSeek | `fd, offset, whence` | 新位置 | 将来 |
| 65 | FileStat | `path, stat_buf` | 0 / エラー | 将来 |
| 66 | DirOpen | `path` | dirfd / エラー | 将来 |
| 67 | DirRead | `dirfd, entry_buf` | 0 / エラー | 将来 |
| 68 | DirClose | `dirfd` | 0 / エラー | 将来 |

> **実装との差異**: 現在の `syscall_numbers.hh` は旧番号体系（32-40）を使用している。
> 本仕様の番号体系（60-69）への移行は、FS 実装時に合わせて行う。

---

## BlockDevice インターフェース

```cpp
struct BlockDevice {
    int read(uint32_t block, uint32_t offset, void* buf, uint32_t size);
    int write(uint32_t block, uint32_t offset, const void* buf, uint32_t size);
    int erase(uint32_t block);
    uint32_t block_size();   // バイト単位
    uint32_t block_count();
};
```

### erase の非同期処理

Flash erase は数十ミリ秒かかる。erase 中に CPU を他タスクに譲る:

```
StorageService:
  block_device.erase(block)
    ├─ erase 開始（DMA/ハードウェア）
    ├─ wait_block(EraseComplete)  → CPU を ControlTask に譲る
    └─ 完了通知で復帰 → 次の操作に進む
```

---

## 電断安全性

### littlefs

littlefs は Copy-on-Write（COW）メカニズムで電断耐性を実現する:
- 既存データを上書きせず、新しいブロックに書き込む
- メタデータの更新はアトミック（1ブロック書き込み完了で切り替え）
- 追加のトランザクションログは不要

### FATfs

FAT32 は電断に脆弱。SD カードの場合:
- 書き込み中の電断でファイル破損の可能性
- 重要データは littlefs 側に保存することを推奨

---

## ファイルディスクリプタ

- アプリごとに最大 4 個のファイルディスクリプタを同時オープン可能
- fd はカーネル内部のテーブルインデックス（0-3）
- アプリ Fault / Exit 時にオープン中の fd は自動クローズ

---

## 関連ドキュメント

- [06-syscall.md](06-syscall.md) — Syscall 番号体系
- [13-system-services.md](13-system-services.md) — SystemTask でのディスパッチ
