# UMI 設計ドキュメント概要

本ディレクトリは、UMI の HAL / ドライバ / ビルドシステム設計に関する議論・調査・意思決定を集約する。
これはアーキテクチャそのものではなく、**設計議論のナビゲーションマップ**である。

---

## 3 つの設計領域と関係

```
┌─────────────────────────────────────────────────────────┐
│                    Build System (xmake)                  │
│  umiport.board rule / MCU DB / memory.ld generation     │
│                                                         │
│   "どのドライバ実装を選択・コンパイルするか"              │
└────────────┬───────────────────────────┬────────────────┘
             │ selects                   │ configures
             ▼                           ▼
┌──────────────────────┐    ┌──────────────────────────┐
│   HAL (umihal)       │    │   Driver (umiport)       │
│                      │    │                           │
│ C++23 concepts で    │◄───│ MCU 固有のドライバ実装    │
│ HW 契約を定義        │    │ HAL concepts を satisfy   │
│ (実装なし・型のみ)   │    │                           │
└──────────────────────┘    └──────────────────────────┘
  defines contracts           provides implementations

                             ┌──────────────────────────┐
                             │   Device (umidevice)     │
                             │                           │
                             │ MCU 外部デバイス定義      │
                             │ (I2C/SPI センサ、DAC 等)  │
                             └──────────────────────────┘
                               external device drivers
```

**依存の流れ:** アプリケーション → Driver (umiport) → HAL (concepts)
**選択の流れ:** Build System → Driver 実装を選択 → HAL contracts を satisfy

---

## 読み方ガイド

| やりたいこと | 読むべき文書 |
|-------------|-------------|
| 現行設計の問題を把握したい | `01_PROBLEM_STATEMENT.md` |
| 特定フレームワーク/技術の詳細を知りたい | `research/` 内の個別ファイル |
| 複数アプローチを横断比較したい | `02_COMPARATIVE_ANALYSIS.md` |
| 最終的な設計方針を確認したい | `03_ARCHITECTURE.md` |
| 議論の生データ・レビュー記録を見たい | `archive/` |

---

## ドキュメント一覧

### 設計文書 (番号順に読む)

| 文書 | 概要 |
|------|------|
| `00_OVERVIEW.md` | 本文書 -- 設計ドキュメントの全体マップ |
| `01_PROBLEM_STATEMENT.md` | 現行/旧アーキテクチャの課題整理と設計要件の定義 |
| `research/` | 個別フレームワーク調査 (1 ファイル = 1 トピック) |
| `02_COMPARATIVE_ANALYSIS.md` | 調査結果の横断分析 -- パターン分類・トレードオフ比較 |
| `03_ARCHITECTURE.md` | 全調査を統合した UMI の理想アーキテクチャ提案 |
| `04_BOARD_CONFIG.md` | ボード設定アーキテクチャ — 設定の統一と局所化 |
| `05_PROJECT_STRUCTURE.md` | ユーザープロジェクト構成 — 既存 BSP 利用 / カスタム BSP の推奨パターン |
| `06a_RAL_ANALYSIS.md` | → `pal/04_ANALYSIS.md` に移動 |
| `06_RAL_ARCHITECTURE.md` | → `pal/03_ARCHITECTURE.md` に移動 |
| `06b_PAL_HW_DEFINITIONS.md` | → `pal/` に分割・拡充 |
| `pal/` | **PAL (Peripheral Access Layer) 設計ドキュメント群** — 4 層モデル、14 カテゴリの詳細分析、生成ヘッダのコード例 |
| `07_HW_DATA_PIPELINE.md` | HW 定義データ統合パイプライン — SVD/CMSIS/CubeMX 等の複数ソース統合と生成戦略 |

### アーカイブ

| 文書 | 概要 |
|------|------|
| `archive/` | 設計プロセスで生成された生データ・レビュー記録・中間成果物 |

`archive/` には調査段階の詳細分析 (`HAL_INTERFACE_ANALYSIS.md`, `BSP_ANALYSIS.md`,
`BSP_INHERITANCE_SURVEY.md` 等) と複数 AI によるレビュー記録 (`archive/review/`) が含まれる。
これらは `01`--`03` の根拠資料であり、通常は参照不要。

---

## 設計領域ごとの主要な問い

### HAL (umihal)

- concept 粒度: モノリシック vs コンポーザブル
- sync / async の分離方法
- GPIO の入出力型安全性
- エラーモデル (`Result<T>` の設計)

### Driver (umiport)

- パッケージ粒度: MCU / board / device の分割
- ボード定義フォーマット: Lua + C++ デュアル構造
- データ継承モデル (`extends` によるボード派生)
- startup / linker script の配置

### Device (umidevice)

- MCU 外部デバイスの定義 (I2C/SPI センサ、DAC 等)
- MCU と同列のレイヤ配置 (ドライバではなくデバイス定義)

### Build System (xmake)

- rule ライフサイクルの実行順序
- ハードウェアデータの single source of truth
- ボード継承の Lua `extends` 実装
