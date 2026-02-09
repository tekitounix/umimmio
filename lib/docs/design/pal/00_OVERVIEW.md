# PAL (Peripheral Access Layer) 設計ドキュメント

**命名変更**: RAL (Register Access Layer) → **PAL (Peripheral Access Layer)** に統一。
RAL は UVM (Universal Verification Methodology) の用語と衝突し、
CMSIS の "Peripheral Access Layer" とも整合性が低いため、PAL を正式名称とする。

---

## 目的

PAL は umimmio (MMIO フレームワーク) の上に MCU 固有のハードウェア定義を構成する層である。
本ディレクトリは PAL に関する全ての設計ドキュメントを集約する。

```
umimmio (MMIO フレームワーク)          PAL (MCU 固有ハードウェア定義)
─────────────────────────          ──────────────────────────
Device, Block, Register, Field      「GPIOA は 0x40020000」
RegOps (read/write/modify)          「MODER は offset 0x00, 32bit」
ByteAdapter (I2C/SPI ブリッジ)       「MODER13 は bit 26-27」
Transport concepts                  「0b01 = 出力モード」
Access policies (RW/RO/WO)          割り込みベクター、メモリマップ
Error policies                      GPIO AF、クロックツリー、DMA

汎用・MCU 非依存                     MCU 固有・型番依存
```

---

## 対象プラットフォーム

| 優先度 | プラットフォーム | アーキテクチャ | 特記 |
|--------|----------------|---------------|------|
| 最優先 | **STM32** (F4/H7/L4/G4 等) | Cortex-M | 最も広範なペリフェラルバリエーション |
| 次点 | **RP2040 / RP2350** | Cortex-M0+/M33 / RISC-V | PIO, 3 層 GPIO, デュアルコア/デュアルISA |
| 次点 | **ESP32-S3 / ESP32-P4** | Xtensa LX7 / RISC-V | GPIO Matrix, HP/LP サブシステム |
| 将来 | **i.MX RT** (1060 等) | Cortex-M7 | IOMUXC, FlexRAM, 高性能リアルタイム |

---

## ドキュメント構成

### 設計文書

| 文書 | 概要 |
|------|------|
| `00_OVERVIEW.md` | 本文書 — PAL ドキュメントの全体マップ |
| `01_LAYER_MODEL.md` | 4 層モデル — ハードウェア定義のスコープ分類 |
| `02_CATEGORY_INDEX.md` | カテゴリ一覧 — C1–C13 の全体マトリクスと完全性チェックリスト |
| `03_ARCHITECTURE.md` | PAL アーキテクチャ提案 — コード構造と API 設計 (旧 06_RAL_ARCHITECTURE.md) |
| `04_ANALYSIS.md` | 既存 PAL アプローチの横断分析 (旧 06a_RAL_ANALYSIS.md) |
| `05_DATA_SOURCES.md` | データソース完全リファレンス — カテゴリ × プラットフォーム別の取得元・ツール・手動作業の整理 |

### カテゴリ別詳細 (categories/)

各カテゴリの詳細分析 + **生成ヘッダのコード例**。

| 文書 | カテゴリ | 概要 |
|------|---------|------|
| `categories/C01_CORE_PERIPHERALS.md` | C1 | NVIC, SCB, SysTick 等のコアペリフェラルレジスタ |
| `categories/C02_CORE_SYSTEM.md` | C2 | コアタイプ, FPU/MPU/TrustZone 有無 |
| `categories/C03_VECTORS.md` | C3 | 割り込み / 例外ベクター |
| `categories/C04_MEMORY_MAP.md` | C4 | Flash, SRAM, CCM 等のメモリマップ |
| `categories/C05_PERIPHERAL_REGISTERS.md` | C5 | GPIO, UART, SPI 等のペリフェラルレジスタ |
| `categories/C06_GPIO_MUX.md` | C6 | GPIO ピンマルチプレクシング (AF/FUNCSEL/Matrix/IOMUXC) |
| `categories/C07_CLOCK_TREE.md` | C7 | クロックツリー定義 |
| `categories/C08_DMA_MAPPING.md` | C8 | DMA マッピング |
| `categories/C09_POWER.md` | C9 | 電力管理 |
| `categories/C10_SECURITY.md` | C10 | セキュリティ / 保護 |
| `categories/C11_DEVICE_META.md` | C11 | デバイスメタデータ |
| `categories/C12_LINKER_STARTUP.md` | C12 | リンカ / スタートアップ |
| `categories/C13_DEBUG_TRACE.md` | C13 | デバッグ / トレース |

### 関連文書 (本ディレクトリ外)

| 文書 | 概要 |
|------|------|
| `../07_HW_DATA_PIPELINE.md` | HW 定義データ統合パイプライン — データソースと生成戦略 |
