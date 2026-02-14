# ESP-IDF

**分類:** SoC SDK (Espressif IoT Development Framework)
**概要:** Espressif 社が提供する ESP32 シリーズ向け公式開発フレームワーク。4 層の明確なハードウェア抽象化と、soc_caps.h による「単一源泉」設計が特徴。

---

## アーキテクチャ概要

### 4 層抽象化

ESP-IDF は明確な 4 層でハードウェアを抽象化する。各層の責務と OS 依存性が厳密に分離されている。

```
┌─────────────────────────────────────────┐
│  Driver API (esp_driver_gpio 等)        │  ← ユーザー向け API（OS 依存あり）
├─────────────────────────────────────────┤
│  HAL (hal/*.h)                          │  ← OS 非依存、static inline
├─────────────────────────────────────────┤
│  LL  (hal/*_ll.h)                       │  ← レジスタ操作ラッパー
├─────────────────────────────────────────┤
│  SoC (soc/<chip>/include/soc/)          │  ← レジスタ定義・SoC 能力宣言
└─────────────────────────────────────────┘
```

| 層 | 場所 | 役割 | OS 依存 |
|---|------|------|---------|
| SoC | `components/soc/<chip>/` | レジスタ定義、`soc_caps.h` | なし |
| LL (Low-Level) | `components/hal/<chip>/include/hal/` | static inline レジスタ操作 | なし |
| HAL | `components/hal/include/hal/` | チップ共通の HAL インタフェース | なし |
| Driver | `components/esp_driver_*/` | FreeRTOS 統合、ユーザー API | あり |

---

## 主要メカニズム

### soc_caps.h — 単一源泉パターン

ESP-IDF の最も特徴的な設計。各チップの能力を C マクロで一元的に宣言する。

```c
/* components/soc/esp32s3/include/soc/soc_caps.h */
#define SOC_UART_NUM            3
#define SOC_UART_FIFO_LEN       128
#define SOC_I2C_NUM             2
#define SOC_GPIO_PIN_COUNT      49
#define SOC_ADC_PERIPH_NUM      2
#define SOC_ADC_MAX_CHANNEL_NUM 10
```

**Kconfig 自動生成パイプライン:**

```
soc_caps.h (#define マクロ)
  → gen_soc_caps_kconfig.py (Python スクリプト)
    → Kconfig.soc_caps.in (自動生成 — DO NOT EDIT)
      → menuconfig で可視化
```

C ヘッダと Kconfig の間でデータの不整合が発生しない。pre-commit フックで同期を強制する。

### ldgen フラグメントリンカ生成

ESP-IDF 独自の `ldgen` ツールで、各コンポーネントがメモリ配置を分散的に宣言し、自動統合する。

```
[mapping:wifi]
archive: libwifi.a
entries:
    * (noflash)              # WiFi ライブラリ全体を IRAM に配置

[mapping:freertos]
archive: libfreertos.a
entries:
    port (noflash)           # FreeRTOS ポート層を IRAM に配置
    tasks:vTaskSwitchContext (noflash)  # 特定関数のみ IRAM に
```

**テンプレートと生成:**

```
components/esp_system/ld/<chip>/
├── memory.ld.in              メモリ領域定義（チップ固有）
└── sections.ld.in            セクション配置（マーカー付きテンプレート）
```

`sections.ld.in` 内のマーカー `mapping[iram0_text]` を ldgen が実際のオブジェクト配置に置換する。

### sdkconfig.defaults レイヤリング

```
sdkconfig.defaults                  全ターゲット共通
sdkconfig.defaults.<IDF_TARGET>     チップ固有 (例: sdkconfig.defaults.esp32s3)
sdkconfig                          ユーザーカスタマイズ（menuconfig で生成）
```

後の設定が前の設定を上書きする積層方式。

### BSP コンポーネント標準化 (v5.x)

ESP-IDF v5.x 系では BSP がコンポーネントとして標準化された。各コンポーネントは独立した CMakeLists.txt を持ち、依存関係を明示的に宣言する。

```cmake
idf_component_register(
    SRCS "src/uart.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio hal soc
    PRIV_REQUIRES esp_pm
)
```

IDF コンポーネントマネージャによるパッケージ配布にも対応。

---

## 長所と短所

| 観点 | 評価 | 詳細 |
|------|------|------|
| 単一源泉 | ◎ | soc_caps.h が唯一の源泉、Kconfig は自動生成 |
| リンカ生成 | ◎ | ldgen + フラグメントで分散定義・自動統合 |
| 4 層分離 | ◎ | SoC/LL/HAL/Driver の責務が明確 |
| コンポーネント分離 | ◎ | 明確な依存宣言と選択的ビルド |
| データ継承 | △ | ファイルパス (`<chip>/`) による暗黙的切り替え。明示的継承なし |
| 学習コスト | △ | Kconfig + CMake + soc_caps.h + ldgen の四重構成 |
| 独自ツール依存 | △ | ldgen が ESP-IDF 固有の Python ツール |
| 対象チップ限定 | △ | ESP32 シリーズのみ対応 |

---

## UMI への示唆

1. **soc_caps.h の単一源泉パターン** は UMI の Lua データベースと同じ思想。C マクロの代わりに Lua テーブルを使う点が異なるが、「唯一のソースから他を生成する」原則は共通
2. **4 層抽象化** (SoC/LL/HAL/Driver) は UMI の umiport/umihal の設計と対応する。OS 非依存層と OS 依存層の境界が明確
3. **ldgen フラグメント** の「各コンポーネントがメモリ配置を自己宣言する」方式は、オーディオフレームワークでの DMA バッファ配置に応用可能
4. **pre-commit フックによる同期強制** はデータの一貫性保証として参考になる

---

## 参照

- [Hardware Abstraction](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/hardware-abstraction.html)
- [Linker Script Generation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/linker-script-generation.html)
- [Build System](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html)
- [ESP-IDF GitHub](https://github.com/espressif/esp-idf)
