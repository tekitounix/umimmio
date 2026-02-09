# PlatformIO

**分類:** IDE / ビルドツール
**概要:** マルチプラットフォーム組み込み開発ツール。board.json によるフラットなボード定義と、platform.py による動的パッケージ選択が特徴。ボード間の継承メカニズムは存在しない。

---

## アーキテクチャ概要

### ディレクトリ構造

```
platforms/ststm32/
├── platform.json                  プラットフォームマニフェスト
├── platform.py                    動的パッケージ選択ロジック（Python）
├── boards/                        ボード定義群（ststm32 プラットフォームで 291 ファイル）
│   └── nucleo_f446re.json         ボードマニフェスト
└── builder/frameworks/            フレームワーク別ビルダー
    ├── arduino.py
    └── stm32cube.py
```

> **注:** 291 ボードという数値は ststm32 プラットフォーム単体の数。PlatformIO 全体ではさらに多い。

### board.json のフラット定義

各ボードを 1 つの JSON ファイルで完結的に記述する。

```json
{
  "build": {
    "cpu": "cortex-m4",
    "mcu": "stm32f446ret6",
    "extra_flags": "-DSTM32F446xx",
    "f_cpu": "180000000L",
    "framework": ["arduino", "stm32cube", "mbed"],
    "variant": "NUCLEO_F446RE"
  },
  "debug": {
    "default_tools": ["stlink"],
    "openocd_target": "stm32f4x",
    "svd_path": "STM32F446x.svd"
  },
  "upload": {
    "maximum_ram_size": 131072,
    "maximum_size": 524288,
    "protocol": "stlink"
  }
}
```

---

## 主要メカニズム

### board.json 間に継承なし

PlatformIO の最大の構造的弱点。同一 MCU ファミリのボード間で `build.cpu`, `build.extra_flags`, `debug.openocd_target` 等が完全に重複する。

```
boards/
├── nucleo_f401re.json        STM32F401RE の全データ
├── nucleo_f411re.json        STM32F411RE の全データ（ほぼコピー）
├── nucleo_f446re.json        STM32F446RE の全データ（ほぼコピー）
└── blackpill_f411ce.json     STM32F411CE の全データ（ほぼコピー）
```

MCU 情報の変更時に関連する全 board.json を手動で更新する必要がある。

### platform.py による動的パッケージ選択

各プラットフォームの Python クラスがボード情報に基づいてパッケージを動的に有効化する。

```python
class Ststm32Platform(PlatformBase):
    def configure_default_packages(self, variables, targets):
        board_config = self.board_config(board)
        mcu = board_config.get("build.mcu", "")
        frameworks = variables.get("pioframework", [])

        if "arduino" in frameworks:
            self.packages["framework-arduinoststm32"]["optional"] = False
        if "stm32cube" in frameworks:
            self.packages["framework-stm32cubef4"]["optional"] = False
```

### リンカスクリプト 3 段フォールバック

```
1. ユーザー指定     board_build.ldscript = custom.ld
     ↓ なければ
2. フレームワーク   framework-arduinoststm32/variants/<variant>/ldscript.ld
     ↓ なければ
3. テンプレート生成  board.json の upload.maximum_size / maximum_ram_size から生成
```

**テンプレート生成は FLASH + RAM の 2 リージョン限定。** CCM-RAM、DTCM、複数 SRAM 等は非対応。組み込みオーディオで必要な特殊メモリ領域には対応できない。

### プロジェクトローカルボード

プロジェクト内の `boards/` ディレクトリに JSON を置くだけでカスタムボードが使える。本体の変更は不要。

---

## 長所と短所

| 観点 | 評価 | 詳細 |
|------|------|------|
| ボード追加の容易さ | ◎ | JSON 1 ファイルのみ |
| デバッグ統合 | ◎ | 300+ ボードで即座にデバッグ可能 |
| 動的パッケージ選択 | ○ | platform.py で条件に応じたパッケージ選択 |
| データ継承 | -- | **board.json 間に継承メカニズムなし。大量のデータ重複** |
| リンカ生成 | △ | テンプレート生成は 2 リージョン限定 |
| 構造化データ | △ | ボード間の共通データが抽出されていない |
| メンテナンス性 | △ | MCU 変更時に全関連ボードの手動更新が必要 |

---

## UMI への示唆

1. **JSON 1 ファイルでボード追加** の DX は最高だが、**継承なし = Fork-and-Modify アンチパターン** の典型例。UMI はこの轍を踏まないよう、Lua dofile() チェーンによるデータ継承を維持すべき
2. **2 リージョン限定** のリンカスクリプト生成は、STM32 の CCM-RAM や複数 SRAM 領域に対応できない。UMI のリンカ生成はこれを上回る必要がある
3. **platform.py の動的選択** は xmake ルールの `on_config` と同等の役割。ボード情報に基づくパッケージ・フラグの条件分岐パターンは参考になる
4. **300+ ボードの即時デバッグ対応** はツール統合の成功例。UMI も board 定義にデバッグプローブ設定を含めるべき

---

## 参照

- [Custom Embedded Boards](https://docs.platformio.org/en/latest/platforms/creating_board.html)
- [platform-ststm32 (GitHub)](https://github.com/platformio/platform-ststm32)
- [PlatformIO Core](https://docs.platformio.org/en/latest/core/index.html)
