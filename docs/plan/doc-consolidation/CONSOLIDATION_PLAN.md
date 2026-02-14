# UMI ドキュメント統廃合計画

**作成日:** 2026-02-14
**関連:** [DOCUMENT_INVENTORY.md](DOCUMENT_INVENTORY.md)

---

## 1. 現状の問題

### 1.1 数字で見る問題

| 指標 | 現状 |
|------|------|
| 総ドキュメント数 | ~210ファイル |
| 不要/重複ファイル | ~75ファイル（35%） |
| 壊れたリンク | docs/README.md 25+箇所、copilot-instructions.md 5+箇所 |
| カーネル文書の重複箇所 | 3ディレクトリ（完全同一コピー4ファイル含む） |
| 分散したドキュメント領域 | docs/ ⇔ lib/docs/ ⇔ lib/umi/*/docs/ の3層 |

### 1.2 構造的問題

1. **正本が不明確** — 同じトピックが複数箇所に存在し、どれが最新・正本か判断不能
2. **archive の不完全な運用** — archiveに移動したファイルが元の場所にもコピーで残存
3. **docs/ のディレクトリ再編が繰り返され、その痕跡が残存** — refs/, dev/, umi-kernel/, umios-architecture/, archive/ が混在
4. **lib内ドキュメントと docs/ の役割分担が不明確** — USB, MIDI, Port 等のドメインドキュメントが両方に存在
5. **目次ファイル(README.md)が機能していない** — 壊れたリンクが修正されていない

---

## 2. 統廃合の方針

### 2.1 基本原則

| 原則 | 説明 |
|------|------|
| **Single Source of Truth** | 各トピックの正本は1箇所のみ |
| **Proximity to Code** | ライブラリ固有のドキュメントはlib/内に配置 |
| **Clear Hierarchy** | docs/は全体設計・仕様・ガイド、lib/docs/はライブラリ標準、lib/umi/*/docs/はライブラリ固有 |
| **Archive = 完全な移動** | archiveしたファイルは元の場所から必ず削除 |
| **Living Index** | 目次ファイルは自動生成またはCI検証で壊れたリンクを防ぐ |

### 2.2 再編後のカテゴリ構成

統廃合後のドキュメントを以下の7カテゴリに分類する。各カテゴリの詳細は個別の要約ドキュメント（CAT_*.md）を参照。

| カテゴリ | ID | 内容 | 配置先 | 詳細 |
|---------|-----|------|--------|------|
| **A. コア仕様** | CAT_A | アーキテクチャ、UMIP/UMIC/UMIM仕様、Concepts、命名体系、セキュリティ | docs/specs/ | [CAT_A_CORE_SPECS.md](CAT_A_CORE_SPECS.md) |
| **B. OS設計** | CAT_B | カーネル仕様、umios-architecture、ブートシーケンス、メモリ保護、サービス | docs/os/ | [CAT_B_OS_DESIGN.md](CAT_B_OS_DESIGN.md) |
| **C. プロトコル** | CAT_C | SysExプロトコル、USB Audio設計、MIDIトランスポート | docs/protocols/ | [CAT_C_PROTOCOLS.md](CAT_C_PROTOCOLS.md) |
| **D. 開発ガイド** | CAT_D | コーディング規約、ビルド、テスト、デバッグ、リリース、ツール設定 | lib/docs/ (既存) + docs/guides/ | [CAT_D_DEV_GUIDES.md](CAT_D_DEV_GUIDES.md) |
| **E. HAL/ドライバ設計** | CAT_E | HAL Concept、PAL、ボード設定、ビルドシステム、コード生成 | lib/docs/design/ (既存) | [CAT_E_HAL_DESIGN.md](CAT_E_HAL_DESIGN.md) |
| **F. DSP/技術資料** | CAT_F | TB-303 VCF/VCO解析、VAFilter、HW I/O処理設計 | docs/dsp/ + docs/hw_io/ (既存) | [CAT_F_DSP_TECHNICAL.md](CAT_F_DSP_TECHNICAL.md) |
| **G. ライブラリ固有** | CAT_G | 各ライブラリ(umibench, umimmio, umitest等)の内部ドキュメント | lib/*/docs/ (既存) | [CAT_G_LIBRARY_DOCS.md](CAT_G_LIBRARY_DOCS.md) |

---

## 3. 具体的な統廃合アクション

### Phase 1: 即座に実行可能 — 重複除去とクリーンアップ

#### 1.1 完全同一コピーの削除

以下のファイルは `docs/archive/umi-kernel/` と `docs/umi-kernel/` で完全同一であるため、archive側を削除して正本を明確化する。

| 削除対象 | 正本 |
|---------|------|
| docs/archive/umi-kernel/OVERVIEW.md | docs/umi-kernel/OVERVIEW.md |
| docs/archive/umi-kernel/DESIGN_DECISIONS.md | docs/umi-kernel/DESIGN_DECISIONS.md |
| docs/archive/umi-kernel/MEMORY.md | docs/umi-kernel/MEMORY.md |
| docs/archive/umi-kernel/service/FILESYSTEM.md | docs/umi-kernel/service/FILESYSTEM.md |
| docs/archive/umi-kernel/service/SHELL.md | docs/umi-kernel/service/SHELL.md |

#### 1.2 移行済みファイルの削除

| 削除対象 | 理由 |
|---------|------|
| docs/archive/UXMP_SPECIFICATION.md | UMI-SysExに統合済み（提案書に明記） |
| docs/archive/UXMP_DATA_SPECIFICATION.md | 同上 |
| docs/archive/UXMP提案書.md | 同上（冒頭に「移行完了」記載） |
| docs/archive/UMI_STATUS_PROTOCOL.md | 現行実装と不一致（現状.mdで確認済み） |
| docs/archive/KERNEL_SCHEDULER_REDESIGN.md | 実装済みで役割終了 |
| docs/archive/WEB_UI_REDESIGN.md | 反映済みで役割終了 |
| docs/archive/umix/UMIX_OVERVIEW.md | SysExプロトコルに統合済み |
| docs/archive/umix/UMIX_TRANSPORT_SYSEX.md | 同上 |
| lib/umi/bench_old/README.md | umibenchに置換済み |
| lib/umi/bench_old/KNOWN_ISSUES.md | 同上 |

#### 1.3 メモの統合

| 統合元 | 統合先 | アクション |
|--------|--------|-----------|
| docs/MEMOMEMO.md | docs/MEMO.md | 4項目をMEMO.mdに追記後、MEMOMEMO.mdを削除 |
| docs/structure.md | docs/PROJECT_STRUCTURE.md | PROJECT_STRUCTUREに包含済み。structure.mdを削除 |
| docs/CLANG_ARM_MULTILIB_WORKAROUND.md | docs/CLANG_TIDY_SETUP.md | 内容が重複。1ファイルに統合 |

#### 1.4 壊れたリンクの修正

| ファイル | アクション |
|---------|-----------|
| docs/README.md | 全面書き直し（新カテゴリ構成に合わせる） |
| .github/copilot-instructions.md | CLAUDE.mdと整合するようパスを修正 |
| docs/archive/README.md | 壊れたリンクを修正 |
| docs/LISENCE_SERVER.md | ファイル名typo修正（LISENCE → LICENSE） |

### Phase 2: カーネル文書の一本化

#### 2.1 docs/umi-kernel/ の整理

`docs/umi-kernel/spec/` が正本として確立されているため：

| アクション | 対象 | 理由 |
|-----------|------|------|
| archive移動 | docs/umi-kernel/OVERVIEW.md | spec/kernel.mdに内容包含 |
| archive移動 | docs/umi-kernel/ARCHITECTURE.md | umios-architecture/02-kernel/が詳細版 |
| archive移動 | docs/umi-kernel/BOOT_SEQUENCE.md | umios-architecture/02-kernel/15-boot-sequence.mdが正本 |
| archive移動 | docs/umi-kernel/DESIGN_DECISIONS.md | adr.mdに役割移行 |
| archive移動 | docs/umi-kernel/IMPLEMENTATION_PLAN.md | docs/dev/IMPLEMENTATION_PLAN.mdが最新 |
| archive移動 | docs/umi-kernel/MEMORY.md | spec/memory-protection.mdが正本 |
| 保持 | docs/umi-kernel/spec/*.md | 正本（4ファイル） |
| 保持 | docs/umi-kernel/adr.md | ADR（設計判断記録） |
| 保持 | docs/umi-kernel/plan.md | メタ計画文書 |
| 保持 | docs/umi-kernel/platform/stm32f4.md | プラットフォーム固有 |

#### 2.2 docs/archive/umi-kernel/ の整理

Phase 1.1 で同一コピーを削除した後：

| アクション | 対象 | 理由 |
|-----------|------|------|
| 保持(archive) | docs/archive/umi-kernel/ARCHITECTURE.md | umi-kernel/版と異なる旧バージョン |
| 保持(archive) | docs/archive/umi-kernel/BOOT_SEQUENCE.md | 同上 |
| 保持(archive) | docs/archive/umi-kernel/IMPLEMENTATION_PLAN.md | 同上 |
| 保持(archive) | docs/archive/umi-kernel/LIBRARY_CONTENTS.md | PROJECT_STRUCTUREに包含されるが参考として保持 |

### Phase 3: SysEx/プロトコル文書の整理

| アクション | 対象 | 理由 |
|-----------|------|------|
| 統合 | UMI_SYSEX_DATA.md → UMI_SYSEX_DATA_SPEC.md | DATA_SPEC.mdが詳細版、DATA.mdの内容を吸収 |
| 統合 | UMI_SYSEX_STATUS_IMPLEMENTATION_NOTES.md → UMI_SYSEX_STATUS.md | 実装メモをSTATUS.mdに統合 |
| archive移動 | docs/archive/UMI_SYSEX_PROTOCOL.md | umi-sysex/が正本 |

### Phase 4: dev/ と lib/docs/ の整理

| アクション | 対象 → 先 | 理由 |
|-----------|-----------|------|
| 名前変更 | docs/dev/GUIDELINE.md → docs/dev/DESIGN_PATTERNS.md | 内容はC++設計パターン集であり「ガイドライン」ではない |
| 移動 | docs/dev/SIMULATION.md → docs/guides/SIMULATION.md | 開発ガイドの一部 |
| 統合 | docs/clang_tooling_evaluation.md → lib/docs/guides/CODE_QUALITY_GUIDE.md | 関連内容。評価結果をガイドに反映 |
| 統合 | docs/CLANG_TIDY_SETUP.md + docs/CLANG_ARM_MULTILIB_WORKAROUND.md → lib/docs/guides/CODE_QUALITY_GUIDE.md | ツール設定をガイドに集約 |

### Phase 5: USB/MIDIドキュメントの住み分け明確化

| アクション | 理由 |
|-----------|------|
| docs/umi-usb/ → docs/archive/ へ移動 | lib/umi/usb/docs/の方が最新かつ詳細 |
| docs/archive/umidi/ は保持 | lib/umi/midi/docs/と補完関係（設計思想の記録として価値） |

### Phase 6: 目次・ナビゲーションの再構築

| アクション | 対象 |
|-----------|------|
| 全面書き直し | docs/README.md — 新カテゴリ構成に合わせた目次 |
| 更新 | README.md — ディレクトリ構成を現状に合わせる |
| 更新 | .github/copilot-instructions.md — パスを修正 |
| 更新 | CLAUDE.md — ドキュメント参照テーブルを新パスに合わせる |

---

## 4. 統廃合後の目標構成

```
docs/
├── README.md                    # 目次（全カテゴリへのナビゲーション）
├── NOMENCLATURE.md              # 命名体系・用語定義（正本）
├── PROJECT_STRUCTURE.md         # プロジェクト構成（正本）
├── MEMO.md                      # 開発メモ（統合済み）
│
├── refs/                        # [CAT_A] コア仕様
│   ├── ARCHITECTURE.md          # アーキテクチャ
│   ├── CONCEPTS.md              # C++20 Concepts
│   ├── SECURITY.md              # セキュリティ
│   ├── UMIP.md                  # Processor仕様
│   ├── UMIC.md                  # Controller仕様
│   ├── UMIM.md                  # Module仕様
│   ├── UMIM_NATIVE_SPEC.md      # ネイティブ仕様
│   ├── API_APPLICATION.md       # アプリAPI
│   ├── API_KERNEL.md            # カーネルAPI
│   ├── API_DSP.md               # DSP API
│   ├── API_BSP.md               # BSP API
│   ├── API_UI.md                # UI API
│   └── UMIDSP_GUIDE.md          # DSPガイド
│
├── umi-kernel/                  # [CAT_B] OS設計
│   ├── spec/                    # 仕様正本（4ファイル）
│   ├── adr.md                   # ADR
│   ├── plan.md                  # 計画
│   └── platform/stm32f4.md     # プラットフォーム固有
│
├── umios-architecture/          # [CAT_B] OS設計仕様（41ファイル、既存構成維持）
│
├── umi-sysex/                   # [CAT_C] SysExプロトコル（統合後5ファイル）
│   ├── UMI_SYSEX_OVERVIEW.md
│   ├── UMI_SYSEX_CONCEPT_MODEL.md
│   ├── UMI_SYSEX_DATA_SPEC.md  # DATA.md を吸収
│   ├── UMI_SYSEX_STATUS.md     # IMPL_NOTESを吸収
│   └── UMI_SYSEX_TRANSPORT.md
│
├── guides/                      # [CAT_D] 開発ガイド（新設）
│   ├── SIMULATION.md            # dev/から移動
│   └── ... (追加予定)
│
├── dev/                         # [CAT_D] 開発者向け（整理後）
│   ├── DESIGN_PATTERNS.md       # 旧GUIDELINE.md（名前変更）
│   ├── IMPLEMENTATION_PLAN.md   # 実装計画
│   ├── RUST.md                  # Rust比較
│   └── DEBUG_VSCODE_CORTEX_DEBUG.md
│
├── dsp/                         # [CAT_F] DSP技術資料（既存構成維持）
│   ├── tb303/vcf/
│   ├── tb303/vco/
│   └── vafilter/
│
├── hw_io/                       # [CAT_F] HW I/O処理設計（既存構成維持）
│
├── web/                         # Web UI設計
│
├── LICENSE_SERVER.md             # ライセンスサーバー（typo修正）
├── STM32H7.md                   # STM32H7 DCache技術資料
├── esp32-support-investigation.md # ESP32調査
│
├── archive/                     # アーカイブ（整理後）
│   ├── README.md                # アーカイブ索引（リンク修正済み）
│   ├── umi-kernel/              # 旧カーネル文書（差分があるもののみ）
│   ├── umi-api/                 # 旧API設計
│   ├── umidi/                   # 旧MIDI設計
│   ├── UMI_SYSTEM_ARCHITECTURE.md
│   ├── UMIOS_DESIGN_DECISIONS.md
│   ├── DESIGN_CONTEXT_API.md
│   ├── STM32F4_KERNEL_FLOW.md
│   ├── OPTIMIZATION_PLAN.md
│   └── 現状.md
│
└── plan/                        # 本計画文書
    ├── DOCUMENT_INVENTORY.md
    ├── CONSOLIDATION_PLAN.md
    └── CAT_*.md
```

---

## 5. 削除ファイル一覧（合計25+ファイル）

| ファイル | 理由 |
|---------|------|
| docs/structure.md | PROJECT_STRUCTUREに包含 |
| docs/MEMOMEMO.md | MEMO.mdに統合 |
| docs/CLANG_ARM_MULTILIB_WORKAROUND.md | CLANG_TIDY_SETUPに統合 |
| docs/clang_tooling_evaluation.md | CODE_QUALITY_GUIDEに統合 |
| docs/umi-usb/USB_AUDIO.md | lib/umi/usb/docs/に移動 |
| docs/umi-usb/USB_AUDIO_REDESIGN_PLAN.md | lib/umi/usb/docs/に移動 |
| docs/archive/UXMP_SPECIFICATION.md | SysExに統合済み |
| docs/archive/UXMP_DATA_SPECIFICATION.md | SysExに統合済み |
| docs/archive/UXMP提案書.md | SysExに統合済み |
| docs/archive/UMI_STATUS_PROTOCOL.md | 現行不一致 |
| docs/archive/KERNEL_SCHEDULER_REDESIGN.md | 実装済み |
| docs/archive/WEB_UI_REDESIGN.md | 反映済み |
| docs/archive/umix/ (2ファイル) | SysExに統合済み |
| docs/archive/umi-kernel/ (5同一ファイル) | 重複 |
| docs/archive/PLAN_AUDIOCONTEXT_REFACTOR.md | 完了済み |
| docs/archive/UMIOS_CONTENTS.md | PROJECT_STRUCTUREに包含 |
| docs/archive/LIBRARY_CONTENTS.md | PROJECT_STRUCTUREに包含 |
| docs/umi-sysex/UMI_SYSEX_DATA.md | DATA_SPECに統合 |
| docs/umi-sysex/UMI_SYSEX_STATUS_IMPLEMENTATION_NOTES.md | STATUSに統合 |
| lib/umi/bench_old/ (2ファイル) | umibenchに置換済み |

---

## 6. 実行順序とリスク

### 実行順序

1. **Phase 1（即座）** — 重複除去、typo修正、メモ統合 → リスクなし
2. **Phase 2（短期）** — カーネル文書一本化 → plan.mdの計画と整合確認が必要
3. **Phase 3（短期）** — SysEx統合 → 内容確認してから統合
4. **Phase 4（中期）** — dev/ と lib/docs/ の整理 → CLAUDE.mdの参照パス更新が必要
5. **Phase 5（中期）** — USB/MIDIの住み分け → lib側のドキュメント品質確認が先
6. **Phase 6（最終）** — 目次再構築 → 全Phase完了後

### リスクと緩和策

| リスク | 緩和策 |
|--------|--------|
| CLAUDE.mdの参照パスが壊れる | Phase 6でCLAUDE.md, copilot-instructions.mdを同時更新 |
| archive削除で情報が失われる | 削除ではなく`trash`コマンドを使用。git履歴にも残る |
| 進行中の作業との競合 | mainブランチではなく専用ブランチ `docs/consolidation` で実施 |

---

## 7. 成果物チェックリスト

- [ ] 不要ファイル25+件の削除/archive移動
- [ ] カーネル文書の正本一本化
- [ ] SysExプロトコル文書の統合
- [ ] dev/ の名前変更・移動
- [ ] 壊れたリンクの全修正
- [ ] docs/README.md の全面書き直し
- [ ] CLAUDE.md の参照テーブル更新
- [ ] copilot-instructions.md のパス修正
- [ ] カテゴリ別要約ドキュメント 7本の作成
