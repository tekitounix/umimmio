# UMI 統合設計調査レポート

**調査日:** 2026-02-14
**調査方法:** 6チーム並列調査（ビルドシステム / HAL・ポート層 / OS層 / オーディオ・DSP / ユーティリティ / 横断文書）
**目的:** 全レイヤーの現状を事実として把握し、統合設計の基盤とする

---

## 1. システム全体像

UMI は5つのレイヤーで構成される。

```
┌─────────────────────────────────────────────────┐
│  Application (Processor + Controller)            │
│  ─ ProcessorLike concept, AudioContext            │
├─────────────────────────────────────────────────┤
│  OS / Runtime (umios)                            │
│  ─ Kernel<N,M,HW,C>, EventRouter, Shell          │
│  ─ Syscall ABI, SharedMemory, TripleBuffer       │
├─────────────────────────────────────────────────┤
│  HAL / Port (umihal + umiport)                   │
│  ─ 30+ Concepts (GPIO, UART, I2S, Audio, Timer)  │
│  ─ Platform implementations (STM32F4, WASM, Host) │
├─────────────────────────────────────────────────┤
│  PAL / MMIO (umimmio + PAL定義)                   │
│  ─ Device/Register/Field 型システム               │
│  ─ Transport抽象 (Direct, I2C, SPI)              │
├─────────────────────────────────────────────────┤
│  Build System (xmake + synthernet)               │
│  ─ MCU DB, embedded rule, flash plugin            │
│  ─ クロスコンパイル (ARM, WASM, Host)             │
└─────────────────────────────────────────────────┘
```

### 1.1 依存関係マップ

```
umimmio ← umiport ← [examples]
              ↑
umihal ──────┘
              ↑
umirtm ──────┘

umitest (独立、テストフレームワーク)
umibench (独立、ベンチマークフレームワーク)

lib/umi/kernel ← lib/umi/core ← lib/umi/runtime
lib/umi/usb ← lib/umi/midi
lib/umi/dsp (独立)
```

---

## 2. レイヤー別調査結果

### 2.1 ビルドシステム層

**構成:**
- ルート xmake.lua → 7ライブラリ + ツール
- lib/umi/xmake.lua → 24内部モジュール + 便利バンドル
- xmake-repo/synthernet/ → 6パッケージ (arm-embedded, clang-arm, gcc-arm, pyocd, python3, renode)

**MCU データベース:**
- mcu-database.json: 11 MCU (STM32F1/F4/H5/H7)
- cortex-m.json: コアプロファイル別フラグ
- build-options.json: 最適化・デバッグ・LTO設定
- toolchain-configs.json: ツールチェーンパス

**クロスコンパイル戦略:**

| ターゲット | プラットフォーム | ツールチェーン | ルール |
|-----------|---------------|-------------|-------|
| ARM Cortex-M | cross/arm | clang-arm (default) | embedded |
| WASM | wasm/wasm32 | emcc | なし (plat/arch) |
| Host | native | clang/gcc | debug/release |

**embedded ルール:** 1026行の Lua コード。MCU選択→フラグ生成→リンカスクリプト→VSCode設定生成を自動化。

**dev-sync ワークフロー:** `~/.xmake/` へのインストールが必要で、ソース編集は自動反映されない。`xmake dev-sync` → キャッシュクリア → リビルドの3ステップが必要。

### 2.2 HAL / ポート層

**umihal (ヘッダオンリー):**
- 30+ C++23 Concepts: GPIO, UART, I2S, Audio, Timer, I2C, SPI, Transport, Platform, Codec, Interrupt, Fault
- `Result<T>` (std::expected) ベースのエラーハンドリング — 例外不使用
- テスト: 全Conceptの static_assert + compile-fail テスト

**umiport (ヘッダオンリー + スタートアップ):**
- 4プラットフォーム実装: Host, STM32F4-Disco, STM32F4-Renode, WASM
- `#ifdef` ゼロ — 全てコンパイル時Concept解決 + xmakeボード選択
- リンク時注入パターン: `umi::rt::detail::write_bytes()` の宣言と定義が分離

**PAL (Peripheral Access Layer):**
- 4層モデル: L1(アーキテクチャ共通) → L2(コアプロファイル) → L3(MCUファミリ) → L4(デバイスバリアント)
- 14カテゴリ (C1-C14): コアペリフェラル～デバッグ/トレース
- Phase 1 (手書き) → Phase 2 (SVD生成) → Phase 3 (マルチMCU) のロードマップ
- 既存 svd2ral ツール (umi_mmio リポジトリ) のリファクタリングで Phase 2 を実現予定

**抽象化品質:** 5/5 — `#ifdef` ゼロ、型安全、ゼロオーバーヘッド

### 2.3 OS / Runtime 層

**カーネル実装:**
- `Kernel<8, 4, HW, 1>` テンプレート — O(1) ビットマップスケジューラ
- 4タスク固定: Audio(P0), System(P1), Control(P2), Idle(P3)
- `Hw<Impl>` テンプレートでハードウェア抽象化

**実装状況:**

| 機能 | 設計(docs) | 実装(code) | ギャップ |
|------|-----------|-----------|---------|
| タスクスケジューラ | 100% | 100% | なし |
| メモリ保護 (MPU) | 100% | 85% | MemoryUsage構造体未実装 |
| EventRouter | 100% | 100% | なし |
| Shell | 100% | 95% | 軽微 |
| StorageService | 100% | 30% | BlockDevice実装なし |
| Fault Handler | 100% | 0% | fault_handler.hh は存在するが未統合 |
| MIDI | 100% | 60% | SysExAssembler, hw_timestamp変換 |

**「綺麗でない」の具体的理由:**
1. kernel.cc に 100+ volatile デバッグ変数
2. SystemTask がクラス化されていない（kernel.cc にインライン）
3. fault_handler.hh が orphaned（定義済みだが未include）
4. KernelEvent と syscall::event の namespace 二重化
5. FS syscall (60-83) がスタブのまま

### 2.4 オーディオ / DSP パイプライン

**Processor Concept:**
```cpp
template<typename P>
concept ProcessorLike = requires(P& p, AudioContext& ctx) {
    { p.process(ctx) } -> std::same_as<void>;
};
```

**AudioContext — ワンソース・マルチターゲットの要:**
- 全プラットフォーム同一インターフェース
- inputs/outputs (std::span), input_events, output_events
- sample_rate, buffer_size, dt, sample_position
- SharedParamState, SharedChannelState, SharedInputState

**DSP設計パターン:**
- `set_params(dt)` でコエフィシェント事前計算（低頻度）
- `operator()` でサンプル処理（高頻度、乗算加算のみ）
- Data/State 分離 — UI はコエフィシェント、DSP はステート
- `dt = 1/sr` 事前計算で 14サイクル除算を回避

**リアルタイム安全:**
- Audio DMA: Priority 0x00 (BASEPRI非マスク) — クリティカルセクション貫通
- signal() パターン: アトミックのみ、MaskedCritical 不使用
- FPU コンテキスト: コンパイル時ポリシー決定 (Forbidden/Exclusive/LazyStack)

### 2.5 ユーティリティライブラリ

**umibench (★★★★★ ゴールドリファレンス):**
- LIBRARY_SPEC v2.0.0 完全準拠
- 11セクション DESIGN.md, INDEX.md, TESTING.md 完備
- docs/ja/ ミラー完備
- テスト: 522行, 全パス

**umimmio:**
- Device → Block → Register → Field の型階層
- 3 Transport: Direct, I2C, SPI
- Access Policy: RW, RO, WO, Inherit
- テスト: 1278行, 全パス

**umirtm:**
- RTT (Real-Time Transfer) ベースのデバッグ出力
- リンク時注入パターンの実装側
- テスト: 1732行, 全パス

**umitest:**
- Suite クラス: run() + check_*() の2スタイル
- source_location 対応のエラー報告
- テスト: 667行

**品質差:**

| ライブラリ | LIBRARY_SPEC準拠 | docs/ja/ | DESIGN.md | テスト |
|-----------|---------------|---------|-----------|-------|
| umibench | ★★★★★ | ✓ | ✓ | 522行 |
| umimmio | ★★★★☆ | ✓ | ✓ | 1278行 |
| umirtm | ★★★★☆ | ✓ | ✓ | 1732行 |
| umitest | ★★★★☆ | ✓ | ✓ | 667行 |
| umihal | ★★★☆☆ | ✗ | ✗ | あり |
| umiport | ★★★☆☆ | ✗ | ✗ | 限定的 |

### 2.6 ドキュメント現状

**総ファイル数:** 157 Markdown ファイル (3.7 MB)
**配置レベル:** 4層 (docs/, lib/docs/, lib/umi/*/docs/, docs/plan/)

**CLAUDE.md のパス正確性:** 40% (7参照中4件がブロークン)
- `docs/refs/specs/ARCHITECTURE.md` → 実際は `docs/refs/ARCHITECTURE.md`
- `docs/new/DESIGN_CONTEXT_API.md` → 実際は `docs/archive/DESIGN_CONTEXT_API.md`
- `docs/new/UMI_SYSTEM_ARCHITECTURE.md` → 実際は `docs/archive/UMI_SYSTEM_ARCHITECTURE.md`
- `docs/refs/reference/API_APPLICATION.md` → 実際は `docs/refs/API_APPLICATION.md`

---

## 3. 統合ポイントの現状

### 3.1 ビルドシステム ↔ HAL/ポート

**現状:** 部分的に統合
- MCU DB → embedded ルール → コンパイルフラグ: 機能する
- MCU DB → PAL ヘッダ選択: 設計済み、未実装 (Phase 2)
- ボードルール → スタートアップ/リンカ自動注入: 機能する

**ギャップ:** BSP定義（bsp.hh）が手動 constexpr。MCU DB の情報と重複している。

### 3.2 HAL ↔ OS

**現状:** 疎結合で良好
- `Hw<Impl>` テンプレートが OS←→HAL のブリッジ
- カーネルは umihal/umiport に直接依存しない
- アプリケーション例 (stm32f4_kernel) で統合

**ギャップ:** なし。設計意図通り。

### 3.3 OS ↔ アプリケーション

**現状:** 機能するが改善余地あり
- Syscall ABI は定義済みで動作する
- SharedMemory + TripleBuffer で lock-free IPC
- EventRouter がMIDI/ボタン入力をオーディオキューにルーティング

**ギャップ:**
- FS syscall (60-83) が未実装
- アプリライフサイクル管理が不完全

### 3.4 ビルドシステム ↔ OS

**現状:** 間接的な統合のみ
- embedded ルールがリンカスクリプト生成 → カーネルのメモリレイアウトに影響
- MCU DB のメモリサイズ → リンカシンボル注入

**ギャップ:** カーネル設定（タスク数、スタックサイズ等）がビルドシステムから制御できない。

### 3.5 全体を貫く実例: stm32f4_kernel

```
Reset_Handler()
  → init_clocks() [mcu.cc — 手動レジスタ設定]
  → init_gpio()   [mcu.cc — 手動レジスタ設定]
  → init_loader()  [lib/umi/service/loader/]
  → load_app()     [AppHeader検証, CRC32, Ed25519]
  → MPU設定        [8リージョン、手動constexpr]
  → init_usb()     [lib/umi/usb/]
  → init_audio()   [I2S DMA設定]
  → start_rtos()   [4タスク生成、スケジューラ開始]
    → audio_task   [DMA → SpscQueue → process()]
    → system_task  [SysEx Shell, MIDI処理]
    → control_task [アプリ main()]
    → idle_task    [WFI]
```

**観察:** init_clocks/init_gpio は mcu.cc にハードコードされており、PAL/MCU DB との統合余地が大きい。

---

## 4. コード品質の定量データ

### 4.1 コードサイズ

| コンポーネント | ファイル数 | 行数 |
|-------------|---------|------|
| lib/umi/kernel/ | 14 | 3,078 |
| stm32f4_kernel/src/ | 4 | 2,216 |
| lib/umihal/ | ~50 | ~2,500 |
| lib/umiport/ | ~30 | ~1,500 |
| lib/umimmio/ | ~15 | ~2,000 |
| lib/umibench/ | ~10 | ~800 |
| lib/umirtm/ | ~10 | ~600 |
| lib/umitest/ | ~10 | ~700 |

### 4.2 テストカバレッジ

| テストスイート | テスト数 | 状態 |
|-------------|--------|------|
| umitest | 全パス | ✓ |
| umimmio | 全パス | ✓ |
| umirtm | 全パス | ✓ |
| umibench | 全パス | ✓ |
| test_kernel | 88/88 | ✓ |
| umihal (concept tests) | 30+ static_assert | ✓ |

### 4.3 ビルドサイズ (STM32F4)

- Kernel: Flash 4.6%, RAM 58.0% (168KB Flash, 128KB RAM)
- synth_app: .umia 生成成功

---

## 5. 主要な発見

### 5.1 設計上の強み

1. **#ifdef ゼロの抽象化** — Concept + テンプレートで全プラットフォーム切替
2. **リンク時注入パターン** — ライブラリ間の疎結合を実現
3. **コンパイル時ポリシー決定** — FPU、最適化、スタックサイズ
4. **型安全な MMIO** — Device/Register/Field でハードウェアレジスタを型で表現
5. **lock-free リアルタイム** — SpscQueue, TripleBuffer, signal() パターン

### 5.2 統合上の課題

1. **BSP と MCU DB の重複** — bsp.hh の手動定義と MCU DB の情報が独立
2. **mcu.cc のハードコード** — クロック/GPIO初期化が PAL を使っていない
3. **カーネルのデバッグ残留物** — 100+ volatile 変数が本番コードに残存
4. **fault_handler.hh の孤立** — 定義済みだが統合されていない
5. **ドキュメントのパス破損** — CLAUDE.md 参照の 60% が壊れている
6. **FS スタックの未完成** — StorageService のテンプレートのみ、実装なし

### 5.3 変革的ポテンシャル

1. **MCU DB → PAL 自動生成** — SVD + CMSIS-Pack → C++ヘッダ自動生成で MCU 追加コストを劇的削減
2. **ボード定義の統一** — MCU DB + BSP → 単一ソースからリンカ/BSP/PAL全生成
3. **embedded ルールの拡張** — 現在の1026行ルールにPAL生成を統合
4. **ワンソース・マルチターゲット** の完全実現 — 同一 Processor コードで ARM/WASM/Desktop/Renode
5. **テスト自動化パイプライン** — Renode シミュレーションでハードウェアレベルの自動テスト
