# ESP32 対応調査

**調査日**: 2026-02-04

---

## ESP32 アーキテクチャ

- **CPU**: Xtensa® 32-bit LX6 デュアルコア（ESP32）
  - Cortex-Mとは**異なるアーキテクチャ**
  - 一部のESP32バリアント（ESP32-C3, C6等）はRISC-V
- **特徴**: Wi-Fi (2.4 GHz) + Bluetooth統合、Ultra Low Power co-processor

---

## スタンドアロンツールチェーン

Espressifは`crosstool-NG`ベースのスタンドアロンツールチェーンを提供:

| ツールチェーン | ターゲット | 最新バージョン |
|---------------|-----------|---------------|
| `xtensa-esp-elf` | ESP32 (Xtensa) | esp-15.2.0_20251204 |
| `riscv32-esp-elf` | ESP32-C3/C6 (RISC-V) | esp-15.2.0_20251204 |

**配布プラットフォーム** (全37アセット):
- `aarch64-apple-darwin` (macOS arm64) ✅
- `aarch64-linux-gnu` (Linux arm64) ✅
- `arm-linux-gnueabi` (Linux arm 32-bit) ✅
- `arm-linux-gnueabihf` (Linux arm 32-bit HF) ✅
- `x86_64-apple-darwin` (macOS x64) ✅
- `x86_64-linux-gnu` (Linux x64) ✅
- `x86_64-w64-mingw32` (Windows x64) ✅

**重要**: これらはスタンドアロンのGCCツールチェーンであり、**ESP-IDF無しでベアメタル開発が可能**。

---

## サポートされるESP32バリアント

| チップ | CPU | ツールチェーン | HALサポート |
|--------|-----|---------------|------------|
| ESP32 | Xtensa LX6 | `xtensa-esp-elf` | ✅ |
| ESP32-S2 | Xtensa LX7 | `xtensa-esp-elf` | ✅ |
| **ESP32-S3** | Xtensa LX7 | `xtensa-esp-elf` | ✅ |
| ESP32-C2 | RISC-V | `riscv32-esp-elf` | ✅ |
| ESP32-C3 | RISC-V | `riscv32-esp-elf` | ✅ |
| ESP32-C5 | RISC-V | `riscv32-esp-elf` | ✅ (新規) |
| ESP32-C6 | RISC-V | `riscv32-esp-elf` | ✅ |
| ESP32-H2 | RISC-V | `riscv32-esp-elf` | ✅ |
| **ESP32-P4** | RISC-V (HP) + LP | `riscv32-esp-elf` | ✅ (最新) |

**結論**: ESP32-S3、ESP32-P4 ともに**サポートされている**。

---

## ペリフェラルライブラリの選択肢

### 1. ESP-IDF HAL (C言語)

ESP-IDFの `components/hal` は**FreeRTOSに依存しない**Hardware Abstraction Layer:

```
components/hal/
├── include/hal/          # ハードウェア非依存インターフェース
│   ├── gpio_hal.h
│   ├── uart_hal.h
│   ├── spi_hal.h
│   ├── i2c_hal.h
│   └── ...
├── esp32/                # ESP32固有実装
├── esp32s3/              # ESP32-S3固有実装
├── esp32p4/              # ESP32-P4固有実装
└── ...
```

**特徴**:
- 2層構造: HAL (上位) + LL (Low-Level, レジスタ操作)
- **OS非依存で設計** (ロック等はHAL層に含まない)
- `esp-hal-components` として単体で抽出可能

**esp-hal-components リポジトリ**:
- ESP-IDFから `soc`, `hal`, `esp_common`, `esp_rom`, `riscv`, `xtensa`, `esp_hw_support`, `esp_system`, `efuse`, `log` を抽出
- ベアメタル開発でのHAL利用を想定

### 2. esp-hal (Rust, no_std)

**esp-rs/esp-hal** は Rust の `no_std` HAL 実装:

```
サポートデバイス:
• ESP32 Series: ESP32
• ESP32-C Series: ESP32-C2, ESP32-C3, ESP32-C5, ESP32-C6
• ESP32-H Series: ESP32-H2
• ESP32-S Series: ESP32-S2, ESP32-S3
```

**特徴**:
- **完全なベアメタル** (no_std)
- FreeRTOS不要
- `embedded-hal` トレイト実装
- ESP32-P4は未サポート（2026年2月時点）
- 1.7k GitHub Stars、活発な開発

### 3. レジスタ直接操作

ESP-IDFの `soc` コンポーネントにレジスタ定義が含まれる:

```c
// components/soc/esp32s3/include/soc/gpio_reg.h
#define GPIO_OUT_REG          (DR_REG_GPIO_BASE + 0x0004)
#define GPIO_OUT_W1TS_REG     (DR_REG_GPIO_BASE + 0x0008)
#define GPIO_OUT_W1TC_REG     (DR_REG_GPIO_BASE + 0x000C)
```

**用途**: 最小限のコードサイズ、最大限の制御が必要な場合

---

## ESP-IDF

- **現在のバージョン**: v5.5.2 (2025-12-26)
- **ビルドシステム**: CMake + Ninja
- **コマンドライン**: `idf.py` (Python)
  - `idf.py set-target esp32`
  - `idf.py build`
  - `idf.py flash`
  - `idf.py monitor`

---

## UMIシステムとの統合可能性

### 方針A: ツールチェーン + HALパッケージ化（推奨）

```lua
-- packages/e/esp-toolchain/xmake.lua
package("esp-toolchain")
    set_homepage("https://github.com/espressif/crosstool-NG")
    set_description("Espressif toolchain for ESP32 (Xtensa/RISC-V)")
    
    add_versions("esp-15.2.0_20251204", "sha256...")
    
    on_install("macosx|arm64", function(package)
        -- xtensa-esp-elf と riscv32-esp-elf の両方をインストール
    end)

-- packages/e/esp-hal-components/xmake.lua (オプション)
package("esp-hal-components")
    set_homepage("https://github.com/espressif/esp-hal-components")
    set_description("HAL components extracted from ESP-IDF")
    
    -- sync-3-master ブランチから取得
    add_urls("https://github.com/espressif/esp-hal-components.git")
    add_versions("master", "sync-3-master")
```

**利点**:
- umiの既存ビルドシステム（xmake）と統合可能
- ベアメタル開発が可能（FreeRTOS不要）
- arm-embedded と同様のパターンで実装可能
- **esp-hal-components を使えばペリフェラル操作も可能**

**課題**:
- リンカスクリプト、スタートアップコードの準備が必要
- ESP-IDFのdriverレイヤーは使えない（HAL/LLレイヤーのみ）

### 方針B: ESP-IDF統合（複雑）

ESP-IDFをxmakeから呼び出す方式:

```lua
-- 概念実装
task("esp32-build")
    on_run(function()
        -- idf.py を呼び出し
        os.exec("idf.py build")
    end)
```

**課題**:
- ESP-IDFは独自のビルドシステム（CMake）を前提
- xmakeとの統合は複雑（ビルドシステムが二重になる）
- 環境変数（IDF_PATH等）の管理が必要

---

## 結論

1. **Phase 1**: `esp-toolchain` パッケージを実装
   - xtensa-esp-elf, riscv32-esp-elf ツールチェーンの自動ダウンロード
   - xmake toolchainとして登録

2. **Phase 2**: `esp-hal-components` パッケージを検討
   - ESP-IDFから抽出されたHAL/LLレイヤー
   - GPIO, UART, SPI, I2C 等のペリフェラル操作が可能

3. **Phase 3**: ESP32ベアメタル対応を検討
   - リンカスクリプト、スタートアップコードの準備
   - 対象チップ: ESP32-S3 (Xtensa), ESP32-P4 (RISC-V)

4. **ESP-IDF統合は見送り**
   - ビルドシステムの二重化は避ける
   - ESP-IDFが必要な場合は、ESP-IDFを直接使用することを推奨

**Rust利用の場合**: `esp-rs/esp-hal` が完全なno_std HALを提供しており、そちらを推奨

---

## 参考リンク

- [espressif/crosstool-NG](https://github.com/espressif/crosstool-NG) - ツールチェーンビルド
- [espressif/esp-hal-components](https://github.com/espressif/esp-hal-components) - HALコンポーネント抽出版
- [esp-rs/esp-hal](https://github.com/esp-rs/esp-hal) - Rust no_std HAL
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
