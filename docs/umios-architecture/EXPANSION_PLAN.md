# umios-architecture ドキュメント拡張計画

## 現状分析

### 既存ドキュメント（umios-architecture/）

| # | ファイル | カバー範囲 | 完成度 |
|---|---------|-----------|--------|
| 00 | overview | システム全体像・タスクモデル | ○ |
| 01 | audio-context | AudioContext 統一仕様 | ○ |
| 02 | processor-controller | Processor/Controller モデル | ○ |
| 03 | event-system | イベントシステム・EventRouter | ○ |
| 04 | param-system | パラメータシステム | ○ |
| 05 | midi | MIDI 統合 | ○ |
| 06 | syscall | Syscall 番号体系 | ○ |
| 07 | memory | メモリレイアウト（組み込み） | ○ |
| 08 | backend-adapters | バックエンドアダプタ | ○ |
| 09 | app-binary | .umia バイナリ仕様 | ○ |
| 10 | shared-memory | SharedMemory 構造体 | ○ |
| 11 | scheduler | スケジューラ・FPU ポリシー | **△ 大部分がTODO** |

### 不足領域（structure.md との差分）

structure.md が定義する「umios lib」の4柱:

1. **RT-Kernel** — scheduler, notify, wait_block, context switch
2. **System Service** — loader, updater, file system, shell, diagnostics
3. **Memory Management** — MPU, heap/stack monitor, fault handle
4. **Security** — crypto (sha256, sha512, ed25519)

このうち umios-architecture でカバーされているのは:
- RT-Kernel: 11-scheduler に FPU ポリシーのみ。他は TODO
- System Service: 06-syscall に番号体系。個別サービスの設計仕様なし
- Memory Management: 07-memory にレイアウトのみ。MPU/Fault/監視なし
- Security: **完全に未カバー**

### umi-kernel/ の関連仕様（統合元候補）

| umi-kernel ファイル | 関連する追加領域 | 扱い |
|---|---|---|
| spec/kernel.md (§3-5) | RT-Kernel（タスクモデル、スケジューリング、割り込み、通知） | → 11-scheduler に統合 |
| spec/memory-protection.md | Memory Management（MPU、Fault、ヒープ/スタック監視） | → 新規 12 に昇格 |
| spec/system-services.md | System Service（Syscall詳細、監視） | → 06-syscall 拡張 + 新規 13 |
| service/FILESYSTEM.md | System Service（FS設計） | → 新規 13 に含む |
| service/SHELL.md | System Service（シェル） | 空ファイル。新規 13 に項目のみ |
| BOOT_SEQUENCE.md | System Service（loader/起動） | → 新規 13 に含む |

---

## 追加計画

### 方針

- 既存の番号体系（00-11）を継続し、12番以降を追加
- 11-scheduler は TODO を埋める形で **拡充**（新規作成ではない）
- umi-kernel/spec/ の規範仕様を umios-architecture の「目標設計仕様」レベルに抽象化して統合
- ターゲット固有の詳細（STM32F4 レジスタアドレス等）は umios-architecture には入れず、umi-kernel に残す

### 変更一覧

#### 1. 11-scheduler.md — 拡充（RT-Kernel）

**現状:** FPU ポリシーのみ記載、他 TODO
**対応:** umi-kernel/spec/kernel.md §3-5 を元に以下を追記

- タスク優先度と実行モデル（4タスク表、役割分離規範）
- コンテキストスイッチ（レジスタ退避/復元、PendSV/SVC フロー）
- スケジューリングアルゴリズム（O(1) ビットマップ）
- 割り込みとタスク通知（notify、wait_block、WaitEvent）
- タイマーとスリープ（SysTick、delay）
- ポート層 API 一覧（structure.md の RT-Kernel Port セクション参照）

**ソース:** umi-kernel/spec/kernel.md, umi-kernel/ARCHITECTURE.md, structure.md

#### 2. 12-memory-protection.md — 新規（Memory Management）

**理由:** 07-memory はレイアウトのみ。保護・監視・Fault は別の関心事

- MPU 境界設計（AppText/AppData/AppStack/SharedMemory）
- ヒープ/スタック衝突検出アルゴリズム
- Fault 処理と隔離方針（OS 生存保証）
- ヒープ/スタック使用量モニタリング
- ターゲット非搭載時の縮退動作（MPU なし、特権分離なし）

**ソース:** umi-kernel/spec/memory-protection.md, umi-kernel/MEMORY.md

#### 3. 13-system-services.md — 新規（System Service）

**理由:** 06-syscall は ABI 番号体系。サービスの設計・ライフサイクルは別

- サービスアーキテクチャ概要（SystemTask 上で動作するサービス群）
- App Loader（.umia 検証、ロードフロー、sign validator）
- Updater（DFU over SysEx、relocator、CRC validator）
- File System（littlefs 統合、BlockDevice 抽象、非同期 syscall）
- Shell（SysEx stdio、コマンド体系）
- Diagnostics（プロファイラ、ヒープ統計、タスク統計）
- Boot Sequence（Reset→main のフロー概要）

**ソース:** umi-kernel/spec/system-services.md, umi-kernel/service/FILESYSTEM.md, umi-kernel/BOOT_SEQUENCE.md

#### 4. 14-security.md — 新規（Security / Crypto）

**理由:** 完全に未カバー。structure.md に crypto 項目あり

- セキュリティモデル概要（何を守るか: アプリ署名検証、OTA 改竄検知）
- 暗号プリミティブ（SHA-256, SHA-512, Ed25519）
- アプリ署名検証フロー（Loader → sign validator → 実行許可）
- OTA/DFU の完全性検証（CRC + 署名）
- 鍵管理（公開鍵の格納場所、更新方針）
- 組み込み制約（リアルタイム安全性、ROM/RAM フットプリント）

**ソース:** structure.md, 09-app-binary.md（AppHeader の sign フィールド）

#### 5. 06-syscall.md — 軽微な拡張

- 13-system-services への相互参照を追加
- ファイルシステム API の詳細は 13 に委譲する旨を明記

#### 6. README.md — 更新

- ドキュメント一覧に 12-14 を追加
- 推奨読み順フローチャートに新ドキュメントを追加

---

## 新しいドキュメント構成（完成後）

```
00-overview
01-audio-context
02-processor-controller
03-event-system          ← EventRouter はここ
04-param-system
05-midi
06-syscall               ← ABI 定義（軽微更新）
07-memory                ← レイアウト（変更なし）
08-backend-adapters
09-app-binary
10-shared-memory
11-scheduler             ← RT-Kernel 全体に拡充 ★
12-memory-protection     ← 新規: MPU/Fault/監視 ★
13-system-services       ← 新規: Loader/FS/Shell/Diagnostics ★
14-security              ← 新規: Crypto/署名検証 ★
```

## 推奨読み順（追加分）

```
11-scheduler ─→ 12-memory-protection
      │
      ▼
07-memory
      │
      ▼
13-system-services ←── 06-syscall
      │
      ▼
09-app-binary ─→ 14-security
```

## 実施順序

1. **11-scheduler 拡充** — 既存 TODO を埋める。他の新規ドキュメントの基盤
2. **12-memory-protection** — 11 と密接に関連（Fault→タスク停止等）
3. **13-system-services** — Loader/FS 等。12 の MPU 設定に依存
4. **14-security** — Loader の署名検証に依存するため最後
5. **06-syscall, README.md 更新** — 相互参照の整備

## umi-kernel/ との関係

- umi-kernel/spec/ は**ポーティング向けの規範仕様**（MUST/SHALL レベル）として維持
- umios-architecture/ は**設計仕様**（目標アーキテクチャ、ターゲット非依存の考え方）
- 重複する内容は umios-architecture を正とし、umi-kernel/spec/ から参照する形にする
- umi-kernel/platform/ のターゲット固有情報は umios-architecture には含めない
