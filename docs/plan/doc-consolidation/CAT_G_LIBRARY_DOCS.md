# CAT_G: ライブラリドキュメント — 統合内容要約

**カテゴリ:** G. ライブラリドキュメント
**配置先:** `lib/*/` + `lib/umi/*/docs/`
**関連:** [CONSOLIDATION_PLAN.md](CONSOLIDATION_PLAN.md) | [DOCUMENT_INVENTORY.md](DOCUMENT_INVENTORY.md)

---

## 1. カテゴリ概要

各ライブラリ固有のドキュメント群。公開ライブラリ（umibench, umimmio, umirtm, umitest, umiport）と内部ライブラリ（umi/fs, umi/usb, umi/midi, umi/port, umi/mmio, umi/dsp, umi/bench_old, umi/ref）のREADME、設計文書、日本語版を含む。

**対象読者:** ライブラリ利用者、コントリビューター
**特徴:** 公開5ライブラリは LIBRARY_SPEC.md 準拠で統一フォーマット。内部ライブラリは独自構成で品質にばらつきあり。

---

## 2. 所属ドキュメント一覧

### 2.1 公開ライブラリ — umibench（3ファイル）★★★★★

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 1 | lib/umibench/README.md | ~69 | ★ | **保持** — Quick Start、API、Examples 完備 |
| 2 | lib/umibench/docs/DESIGN.md | ~300+ | ★ | **保持** — Vision/要件/アーキテクチャ/API/テスト |
| 3 | lib/umibench/docs/ja/README.md | ~55 | ★ | **保持** — 日本語版（自然な翻訳） |

### 2.2 公開ライブラリ — umimmio（8ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 4 | lib/umimmio/README.md | ~62 | ★ | **保持** — Quick Start、API |
| 5 | lib/umimmio/docs/DESIGN.md | ~300+ | ★ | **保持（吸収先）** — USAGE/EXAMPLES/TESTING を統合 |
| 6 | lib/umimmio/docs/EXAMPLES.md | ~200 | ◆ | **削除** → DESIGN.md に統合 |
| 7 | lib/umimmio/docs/GETTING_STARTED.md | ~100 | ◆ | **削除** → DESIGN.md に統合 |
| 8 | lib/umimmio/docs/INDEX.md | ~50 | ◆ | **削除** → DESIGN.md の目次に吸収 |
| 9 | lib/umimmio/docs/TESTING.md | ~150 | ◆ | **削除** → DESIGN.md に統合 |
| 10 | lib/umimmio/docs/USAGE.md | ~200 | ◆ | **削除** → DESIGN.md に統合 |
| 11 | lib/umimmio/docs/ja/README.md | ~60 | ★ | **保持** — 日本語版 |

### 2.3 公開ライブラリ — umirtm（3ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 12 | lib/umirtm/README.md | ~58 | ★ | **保持** — Quick Start、API |
| 13 | lib/umirtm/docs/DESIGN.md | ~200+ | ★ | **保持** — Vision/要件/アーキテクチャ |
| 14 | lib/umirtm/docs/ja/README.md | ~55 | ★ | **保持** — 日本語版 |

### 2.4 公開ライブラリ — umitest（3ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 15 | lib/umitest/README.md | ~62 | ★ | **保持** — Quick Start、API |
| 16 | lib/umitest/docs/DESIGN.md | ~200+ | ★ | **保持** — Vision/要件/アーキテクチャ |
| 17 | lib/umitest/docs/ja/README.md | ~55 | ★ | **保持** — 日本語版 |

### 2.5 公開ライブラリ — umiport（1ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 18 | lib/umiport/README.md | ~39 | ★ | **保持** — WIP。将来 DESIGN.md 追加必要 |

### 2.6 内部ライブラリ — umi/fs（6ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 19 | lib/umi/fs/docs/README.md | ~138 | ★ | **保持** — 概要・API・ビルド手順 |
| 20 | lib/umi/fs/docs/DESIGN.md | ~150+ | ★ | **保持** — クリーンルーム設計方針（F1-F6, L1-L5 克服） |
| 21 | lib/umi/fs/docs/AUDIT.md | ~300+ | ★ | **保持** — リファレンス実装の監査レポート |
| 22 | lib/umi/fs/docs/CLEANROOM_PLAN.md | ~200 | ★ | **保持** — littlefs 再実装ロードマップ |
| 23 | lib/umi/fs/docs/SLIM_STORAGE_PLAN.md | ~150 | ★ | **保持** — StorageService 適合の機能拡張計画 |
| 24 | lib/umi/fs/test/TEST_REPORT.md | ~200 | ★ | **保持** — テスト結果・ベンチマーク |

### 2.7 内部ライブラリ — umi/usb（14ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 25 | lib/umi/usb/docs/UMIUSB_REFERENCE.md | ~100+ | ★ | **保持** — ファイルツリー・依存関係リファレンス |
| 26 | lib/umi/usb/docs/ASRC_DESIGN.md | ~200 | ★ | **保持** — PI 制御ベース ASRC 設計 |
| 27 | lib/umi/usb/docs/IMPLEMENTATION_ANALYSIS.md | ~300+ | ★ | **保持** — アーキテクチャ・データフロー分析 |
| 28 | lib/umi/usb/docs/SPEED_SUPPORT_DESIGN.md | ~500+ | ◆ | **保持（マスター）** — 分割元。design/ が詳細版 |
| 29 | lib/umi/usb/docs/UAC_SPEC_REFERENCE.md | ~300+ | ★ | **保持** — UAC 1.0/2.0 仕様リファレンス |
| 30 | lib/umi/usb/docs/UAC2_DUPLEX_INVESTIGATION.md | ~200 | ★ | **保持** — DWC2 パリティバグ調査報告 |
| 31 | lib/umi/usb/docs/design/00-implementation-plan.md | ~100 | ★ | **保持** — 統合実装計画インデックス |
| 32 | lib/umi/usb/docs/design/01-speed-support.md | ~200 | ★ | **保持** — HW 分離・HS 対応 |
| 33 | lib/umi/usb/docs/design/02-hal-winusb-webusb.md | ~200 | ★ | **保持** — Hal concept 拡張 |
| 34 | lib/umi/usb/docs/design/03-api-architecture.md | ~200 | ★ | **保持** — 層構成・ファイルツリー |
| 35 | lib/umi/usb/docs/design/04-isr-decoupling.md | ~200 | ★ | **保持** — ISR 軽量化設計 |
| 36 | lib/umi/usb/docs/design/05-midi-integration.md | ~200 | ★ | **保持** — MIDI 統合再設計 |
| 37 | lib/umi/usb/docs/design/06-midi-separation.md | ~200 | ★ | **保持** — MIDI 完全分離 |
| 38 | lib/umi/usb/docs/design/07-uac-feature-coverage.md | ~200 | ★ | **保持** — UAC 機能網羅計画 |

### 2.8 内部ライブラリ — umi/midi（3ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 39 | lib/umi/midi/README.md | ~40 | ★ | **保持** — 概要 |
| 40 | lib/umi/midi/docs/PROTOCOL.md | ~200+ | ★ | **保持** — SysEx プロトコル実装仕様 |
| 41 | lib/umi/midi/docs/design.md | ~200+ | ★ | **保持** — UMP-Opt フォーマット・レイヤー設計 |

### 2.9 内部ライブラリ — umi/port（9ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 42 | lib/umi/port/docs/BACKEND_SWITCHING.md | ~200 | ★ | **保持** — バックエンド切り替え概要・設計要約 |
| 43 | lib/umi/port/docs/DAISY_POD_PLAN.md | ~200 | ★ | **保持** — Daisy Seed/Pod 対応計画 |
| 44 | lib/umi/port/docs/design/00-implementation-plan.md | ~150 | ★ | **保持** — 実装ルール・ToDo |
| 45 | lib/umi/port/docs/design/01-principles.md | ~100 | ★ | **保持** — マクロ排除・同名ヘッダ・xmake制御 |
| 46 | lib/umi/port/docs/design/02-port-architecture.md | ~200 | ★ | **保持** — 5レイヤー構成 |
| 47 | lib/umi/port/docs/design/03-concept-contracts.md | ~200 | ★ | **保持** — Concept 契約定義 |
| 48 | lib/umi/port/docs/design/04-hw-separation.md | ~150 | ★ | **保持** — HW 漏出排除原則 |
| 49 | lib/umi/port/docs/design/05-migration.md | ~200 | ★ | **保持** — 移行マッピング |
| 50 | lib/umi/port/docs/design/06-mmio-integration.md | ~150 | ★ | **保持** — umimmio 統合 |

### 2.10 内部ライブラリ — umi/mmio（3ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 51 | lib/umi/mmio/docs/USAGE.md | ~436 | ★ | **保持** — 階層型レジスタアクセス使用ガイド |
| 52 | lib/umi/mmio/docs/NAMING.md | ~119 | ★ | **保持** — ALL_CAPS 命名規則ガイド |
| 53 | lib/umi/mmio/docs/IMPROVEMENTS.md | ~1037 | ★ | **保持** — 改善提案・バグ報告・テスト不足指摘 |

### 2.11 内部ライブラリ — umi/dsp（1ファイル）

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 54 | lib/umi/dsp/README.md | ~40 | ★ | **保持** — DSP ライブラリ概要 |

### 2.12 その他 — umi/bench_old, umi/ref, umi/test/umitest

| # | ファイル | 行数 | 有用性 | 統廃合アクション |
|---|---------|------|--------|-----------------|
| 55 | lib/umi/bench_old/README.md | ~40 | ✗ | **削除** — umibench に置換済み |
| 56 | lib/umi/bench_old/KNOWN_ISSUES.md | ~200+ | ◆ | **削除** — 旧版の問題一覧。教訓は umibench に反映済み |
| 57 | lib/umi/ref/README.md | ~29 | ★ | **保持** — umimock リファレンス実装 |
| 58 | lib/umi/test/umitest/README.md | ~30 | ◆ | **確認要** — umitest の内部コピーか要調査 |

---

## 3. ドキュメント別内容要約

### 3.1 公開ライブラリ群（umibench / umimmio / umirtm / umitest）

LIBRARY_SPEC.md v2.0.0 準拠の統一フォーマット。各ライブラリは `README.md` + `docs/DESIGN.md` + `docs/ja/README.md` の3ファイル構成が標準。

**umibench（ゴールドリファレンス ★★★★★）:**
- クロスターゲット・マイクロベンチマーク（Host / WASM / ARM）
- ベースライン補正、中央値ベースキャリブレーション
- `TimerLike` / `OutputLike` concepts による軽量抽象化
- DESIGN.md: Vision → Non-Negotiable Requirements → Architecture → Programming Model → API Spec → Test Strategy → Design Principles の7セクション構成

**umimmio（型安全 MMIO）:**
- コンパイル時レジスタマップ: Device → Block → Register → Field 階層
- Access Policies: RO/WO/RW のコンパイル時制御
- 複数トランスポート: DirectTransport, I2cTransport, SpiTransport
- **問題:** DESIGN.md 以外に USAGE/EXAMPLES/GETTING_STARTED/INDEX/TESTING が分散（LIBRARY_SPEC.md では DESIGN.md に統合すべき）

**umirtm（RTT 互換デバッグモニタ）:**
- SEGGER RTT 互換リングバッファ、軽量 printf、`{}` プレースホルダ print
- ホスト側ブリッジ機能
- ヘッダのみ、ヒープ不要、例外不使用

**umitest（マクロフリー・テストフレームワーク）:**
- `std::source_location` による自動ソース位置キャプチャ
- `TestContext` 構造化テスト + 単純アサーション
- ヘッダのみ、依存なし（基盤ライブラリ）

**umiport（プラットフォーム基盤 — WIP）:**
- STM32F4 スタートアップコード、Renode シミュレーション
- UART 出力バックエンド
- DESIGN.md 未作成（今後追加必要）

---

### 3.2 umi/fs — ファイルシステム（6ファイル、高品質）

FATfs (FAT12/16/32) と littlefs (v2.1) のクリーンルーム C++23 実装。

**README.md:**
- 2つのFS: FATfs + littlefs
- 特徴: ヒープ不使用、グローバル可変状態なし、複数インスタンス安全、ASSERT 不使用
- テスト: littlefs 113/113, FATfs 96/96, 比較テスト 33+27, Renode 統合 28/28

**DESIGN.md:**
- **FATfs 設計:** BPB定数、セクタウィンドウ、FATテーブル、リファレンス欠陥 F1-F6 の克服方法
  - F1: f_rename クロスリンク → 中間sync
  - F2: グローバル状態 → クラスメンバに封入
  - F3: 2nd FAT 戻り値無視 → 全 disk_write チェック
  - F4: 循環チェーン無限ループ → カウンタ上限
  - F5: truncate サイズ不整合 → エラー時 objsize 更新スキップ
  - F6: ヒープ割り当て → 完全排除（LFN バッファはスタック）
- **littlefs 設計:** CRC-32, BD操作, ブロックアロケータ, リファレンス欠陥 L1-L5 の克服
- **実装結果:** Flash 71,028B (リファレンスC比 +1.8%)、littlefs -22% コード行数

**AUDIT.md:**
- リファレンス実装（FATfs R0.16, littlefs v2.11）の包括的監査
- Critical/High/Medium 問題の特定がクリーンルーム実装の動機

**CLEANROOM_PLAN.md:**
- littlefs のゼロからの再実装計画
- 目標: リファレンスとの同一行を 1089行 → 50行未満に削減
- Part 1-4 の段階的実装（CRC → BD → メタデータ → CTZスキップリスト）

**SLIM_STORAGE_PLAN.md:**
- StorageService (umios-architecture/19-storage-service.md) との統合計画
- 5つの不足機能: ジオメトリAPI、pending_move復旧、ウェアレベリング統計、fs_gc公開、マウントパラメータ

---

### 3.3 umi/usb — USB Audio/MIDI（14ファイル、最大規模）

USB Audio Class (UAC1/UAC2) + USB MIDI の header-only C++20 実装。

**UMIUSB_REFERENCE.md:**
- ファイルツリー: core/(types, hal, device, descriptor) + audio/(interface, types, device) + midi/ + hal/stm32_otg
- 依存: umidsp(PiRateController), umidi(MIDI型)

**IMPLEMENTATION_ANALYSIS.md:**
- 4層アーキテクチャ: Application → USB Class → USB Device Core → HAL
- データフロー: Audio OUT(Host→EP→RingBuffer→DMA→DAC), Audio IN(ADC→RingBuffer→SOF→EP→Host)
- フィードバック: SOF割り込み → FeedbackCalculator(PI制御) → Feedback EP

**ASRC_DESIGN.md:**
- 問題: ホスト/デバイス時間差 ±500ppm、HW PLL 調整不可
- 解法: PI制御（Kp=2, Ki=0.02, 最大±1000ppm調整）

**SPEED_SUPPORT_DESIGN.md（マスタードキュメント）:**
- design/01-07 の分割元。全内容を含む
- 評価: HW分離=不完全、USB速度=FS のみ、HS対応=設計済み未実装

**UAC_SPEC_REFERENCE.md:**
- UAC 1.0/2.0/3.0 の仕様整理
- 結論: UAC 1.0 + 2.0 を対象（3.0 は OS サポート不十分）

**UAC2_DUPLEX_INVESTIGATION.md:**
- DWC2 iso IN フレーム偶奇パリティ反転バグの調査報告
- Audio IN 50%フレーム送信スキップ → macOS が 21kHz と誤判定 → OUT 供給半減

**design/ ディレクトリ（8ファイル）:**
- 00: 統合実装計画（インデックス）
- 01: HW分離・HS対応、Hal concept の漏れ分析
- 02: Hal/Class concept 拡張、WinUSB/WebUSB
- 03: API アーキテクチャ、層構成、SpeedTraits
- 04: ISR 軽量化・外部処理疎結合
- 05: MIDI 統合再設計、MIDI 2.0 対応
- 06: MIDI 完全分離（umidi パッケージ化）
- 07: UAC 機能網羅計画

---

### 3.4 umi/midi — MIDI ライブラリ（3ファイル）

組み込み向け MIDI ライブラリ。UMP-Opt (Universal MIDI Packet 最適化) フォーマット。

**README.md:**
- ARM Cortex-M 最適化、header-only、型安全、ゼロアロケーション

**PROTOCOL.md:**
- UMI SysEx フレーム: `F0 <SYSEX_ID> <CMD> <SEQ> <DATA...> <CHECKSUM> F7`
- 7-bit エンコーディング（8bit→SysEx安全形式）
- コマンド: Standard IO(0x01-0x0F), Firmware Update(0x10-0x1F), System+State(0x20-0x2F), Auth(0x30-0x3F), Object Transfer(0x40-0x5F)
- StandardIO クラス: stdin/stdout/stderr、フロー制御

**design.md:**
- UMP-Opt フォーマット: `UMP32 { uint32_t word; }` — 単一32bit比較でメッセージ型チェック
- 3層構造: core/(ump, parser, result, sysex_buffer) → messages/(channel_voice, system, sysex, utility) + protocol/(encoding, commands, message, standard_io)
- is_note_on() が ARM 2-3命令にコンパイル

---

### 3.5 umi/port — ポート抽象化（9ファイル）

マクロ排除・xmake 制御のバックエンド切り替えアーキテクチャ。

**BACKEND_SWITCHING.md（概要）:**
- 3原則: マクロ排除、同名ヘッダ差し替え、xmake インクルードパス制御
- 5レイヤー: common → arch → mcu → board → platform
- 依存方向: target → board → mcu → arch → common（逆禁止）
- HAL=レジスタ操作(mcu/)、Driver=HALを束ねてConcept実装(board/)、PAL=CPU固有(arch/)

**DAISY_POD_PLAN.md:**
- STM32H750 ベースの Daisy Seed/Pod 対応計画
- libDaisy 完全非依存、レジスタ直接操作
- 将来拡張: Daisy Patch, Field, Petal 等

**design/ ディレクトリ（7ファイル）:**
- 00: 実装計画（Phase 0-5 ロードマップ）
- 01: 基本原則（マクロ排除、同名ヘッダ、xmake制御の3ルール）
- 02: port/ アーキテクチャ（5レイヤー詳細、派生ボード）
- 03: Concept 契約（arch/mcu/board の形式定義、static_assert 検証）
- 04: HW 分離原則（カーネル・ミドルウェアからの HW 漏出排除）
- 05: 移行マッピング（現行ファイル→新構成の対応表）
- 06: umimmio 統合（レジスタ定義の umimmio 移行）

---

### 3.6 umi/mmio — MMIO 内部ドキュメント（3ファイル）

umimmio の内部実装ドキュメント。公開版（lib/umimmio/docs/）とは別管理。

**USAGE.md（436行）:**
- 階層型アクセス: Device → Block → Register → Field
- Transport層: Direct, I2C, SPI
- Register操作: write(), read(), modify(), is(), flip()
- Access Policies, ベストプラクティス

**NAMING.md（119行）:**
- MMIO定義: ALL_CAPS（Datasheet準拠 — 認知負荷低減）
- ライブラリ実装: CamelCase/lower_case（C++標準）
- clang-tidy 設定例

**IMPROVEMENTS.md（1037行）:**
- 優先度別改善提案（高: RO/WO コンパイル時制御、中: Transport concept、低: エラーポリシー）
- バグ報告（ByteAdapter アドレストランケーション）
- テスト不足指摘（RO/WO コンパイルエラー、64bitレジスタ、異レジスタ間 modify 禁止）

---

### 3.7 削除対象 — umi/bench_old（2ファイル）

umibench に完全置換された旧ベンチマークライブラリ。

**README.md:**
- 旧 API: `Runner<Timer>`、`Stats`、`TimerLike`/`OutputLike`

**KNOWN_ISSUES.md:**
- Critical: UART 無限ブロックリスク（タイムアウトなし）
- Critical: `halt()` の HardFault（デバッガ未接続時）
- umibench で全問題が解決済み

---

## 4. カテゴリ内の関連性マップ

```
公開ライブラリ（LIBRARY_SPEC.md 準拠）
├── umibench ← ゴールドリファレンス ★★★★★
│   ├── README.md + DESIGN.md + ja/README.md （完全）
│   └── 依存: umimmio, umitest
├── umimmio ← 型安全 MMIO
│   ├── README.md + DESIGN.md + ja/README.md （核）
│   ├── USAGE/EXAMPLES/GETTING_STARTED/INDEX/TESTING ← 要統合
│   └── 依存: umitest
├── umirtm ← RTT デバッグ
│   ├── README.md + DESIGN.md + ja/README.md （完全）
│   └── 依存: umitest
├── umitest ← テストフレームワーク（基盤、依存なし）
│   └── README.md + DESIGN.md + ja/README.md （完全）
└── umiport ← プラットフォーム基盤（WIP）
    └── README.md のみ（DESIGN.md 未作成）

内部ライブラリ（lib/umi/*/docs/）
├── umi/fs ← ファイルシステム（6ファイル、高品質）
│   ├── README + DESIGN + AUDIT + CLEANROOM + SLIM_STORAGE + TEST_REPORT
│   └── 関連: umios-architecture/19-storage-service.md (CAT_B)
├── umi/usb ← USB Audio/MIDI（14ファイル、最大規模）
│   ├── リファレンス + 分析 + ASRC + Speed + UAC仕様 + バグ調査
│   ├── design/00-07（体系的な設計文書群）
│   ├── 関連: docs/umi-usb/ (CAT_C, archive移動対象)
│   └── 依存: umi/dsp, umi/midi
├── umi/midi ← MIDI（3ファイル）
│   ├── README + PROTOCOL + design
│   ├── 関連: docs/umi-sysex/ (CAT_C)
│   └── UMP-Opt フォーマットでARM最適化
├── umi/port ← ポート抽象化（9ファイル）
│   ├── BACKEND_SWITCHING + DAISY_POD_PLAN + design/00-06
│   ├── 関連: lib/docs/design/ (CAT_E)
│   └── 5レイヤー: common → arch → mcu → board → platform
├── umi/mmio ← MMIO 内部（3ファイル）
│   ├── USAGE + NAMING + IMPROVEMENTS
│   └── 関連: umimmio (公開版)、umi/port/design/06
├── umi/dsp ← DSP（1ファイル、README のみ）
│   └── 関連: docs/dsp/ (CAT_F)
├── umi/bench_old ← 旧ベンチマーク（2ファイル、削除対象）
│   └── umibench に置換済み
└── umi/ref ← umimock（1ファイル）
    └── 規約検証用リファレンス

クロスカテゴリ依存:
├── umi/fs ↔ CAT_B (StorageService)
├── umi/usb ↔ CAT_C (SysEx/USB プロトコル)
├── umi/midi ↔ CAT_C (SysEx プロトコル)
├── umi/port ↔ CAT_E (HAL/PAL 設計)
└── umi/mmio ↔ umimmio (公開/内部の二重管理)
```

---

## 5. 統廃合アクション

### Phase 4 実行項目（ライブラリドキュメント正規化）

| ステップ | アクション | 対象 |
|---------|-----------|------|
| 4.1 | umimmio の分散ファイル5件を DESIGN.md に統合 | USAGE, EXAMPLES, GETTING_STARTED, INDEX, TESTING → DESIGN.md |
| 4.2 | umi/bench_old ディレクトリの削除（2ファイル） | bench_old/README.md, KNOWN_ISSUES.md |
| 4.3 | umi/mmio/docs/ と umimmio/docs/ の関係整理 | 内部版は実装詳細、公開版は利用者向けに役割分担を明記 |
| 4.4 | umiport に DESIGN.md を新規作成 | umi/port/docs/ の設計をベースに |

### 統合後の構成

```
公開ライブラリ（各3ファイル標準）
lib/umibench/     → README.md + docs/DESIGN.md + docs/ja/README.md （変更なし）
lib/umimmio/      → README.md + docs/DESIGN.md + docs/ja/README.md （5ファイル統合）
lib/umirtm/       → README.md + docs/DESIGN.md + docs/ja/README.md （変更なし）
lib/umitest/      → README.md + docs/DESIGN.md + docs/ja/README.md （変更なし）
lib/umiport/      → README.md + docs/DESIGN.md [新規] （DESIGN.md 追加）

内部ライブラリ（変更なし — 全保持）
lib/umi/fs/docs/        → 6ファイル
lib/umi/usb/docs/       → 14ファイル
lib/umi/midi/docs/      → 2ファイル + README
lib/umi/port/docs/      → 9ファイル
lib/umi/mmio/docs/      → 3ファイル
lib/umi/dsp/            → README のみ
lib/umi/ref/            → README のみ

削除
lib/umi/bench_old/      → 全削除（2ファイル）
```

---

## 6. 品質評価

### 6.1 公開ライブラリ

| ライブラリ | SPEC準拠 | README | DESIGN | ja/ | テスト | 総合 |
|-----------|---------|--------|--------|-----|-------|------|
| umibench | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ |
| umimmio | ★★★☆☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ |
| umirtm | ★★★★★ | ★★★★☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ |
| umitest | ★★★★★ | ★★★★☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ |
| umiport | ★☆☆☆☆ | ★★☆☆☆ | なし | なし | なし | ★★☆☆☆ |

### 6.2 内部ライブラリ

| ライブラリ | 網羅性 | 一貫性 | 読みやすさ | コード整合 | 総合 |
|-----------|--------|--------|-----------|-----------|------|
| umi/fs | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ |
| umi/usb | ★★★★★ | ★★★★☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ |
| umi/midi | ★★★★☆ | ★★★★☆ | ★★★★★ | ★★★★☆ | ★★★★☆ |
| umi/port | ★★★★★ | ★★★★★ | ★★★★☆ | ★★★☆☆ | ★★★★☆ |
| umi/mmio | ★★★★☆ | ★★★☆☆ | ★★★★☆ | ★★★★☆ | ★★★☆☆ |
| umi/dsp | ★★☆☆☆ | — | ★★★☆☆ | ★★★☆☆ | ★★☆☆☆ |

### 6.3 カテゴリ全体

| 観点 | 評価 | コメント |
|------|------|---------|
| 網羅性 | ★★★★☆ | 公開5ライブラリ＋内部7ライブラリをカバー。umiport/umi/dsp が薄い |
| 一貫性 | ★★★☆☆ | 公開ライブラリは統一。内部ライブラリはフォーマットがバラバラ |
| 更新頻度 | ★★★★☆ | 2026-01〜02 に集中整備。umi/usb は活発に改訂中 |
| 読みやすさ | ★★★★☆ | umi/fs と umi/usb は特に優れている |
| コードとの整合 | ★★★★☆ | 実装と直結。umi/port は設計先行（実装はこれから） |
| LIBRARY_SPEC 準拠 | ★★★☆☆ | umibench のみ完全準拠。umimmio は分散問題。umiport は DESIGN.md なし |

---

## 7. 推奨事項

1. **umimmio の即時統合** — 5ファイル（USAGE/EXAMPLES/GETTING_STARTED/INDEX/TESTING）を DESIGN.md に統合。LIBRARY_SPEC.md の「旧ファイルを DESIGN.md に統合する」方針に直接該当
2. **umi/bench_old の削除** — umibench に完全置換済み。KNOWN_ISSUES.md の教訓は既に反映されており保持理由がない
3. **umiport DESIGN.md の作成** — umi/port/docs/ に充実した設計文書があるため、それをベースに公開版 DESIGN.md を作成
4. **umi/mmio と umimmio の役割明確化** — 内部版（umi/mmio/docs/）は実装者向け詳細、公開版（umimmio/docs/）は利用者向けガイドとして役割分担を README に明記
5. **umi/dsp の文書化強化** — README のみでは不十分。docs/dsp/ (CAT_F) の内容を活かした DESIGN.md を将来的に作成
6. **umi/usb の SPEED_SUPPORT_DESIGN.md 整理** — design/ に分割済みだがマスターも残存。分割版を正本とし、マスターは目次化またはarchive移動
7. **内部ライブラリの LIBRARY_SPEC.md 適用検討** — umi/fs は既に高品質だが、標準フォーマット（README + DESIGN.md）への整形で発見性を向上
