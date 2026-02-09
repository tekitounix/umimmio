# Zephyr RTOS

**分類:** リアルタイム OS (RTOS)
**概要:** Linux Foundation 傘下のオープンソース RTOS。DeviceTree + Kconfig + CMake の三重構成でハードウェアを記述する、最も包括的なボードサポートシステムを持つ。

---

## アーキテクチャ概要

### HWMv2 ボードモデル

Zephyr v3.7 で正式導入された Hardware Model v2 (HWMv2) は、従来のフラットなボードディレクトリ構造をベンダーベースの階層構造に再編した。

```
zephyr/
├── boards/
│   └── st/                           ベンダーディレクトリ
│       └── nucleo_f411re/            ボードディレクトリ
│           ├── board.yml             メタデータ（SoC・リビジョン・バリアント）
│           ├── nucleo_f411re.dts     DeviceTree ソース
│           ├── nucleo_f411re_defconfig  デフォルト Kconfig
│           ├── Kconfig.nucleo_f411re SoC 選択チェーン
│           └── board.cmake           デバッグランナー設定
├── soc/
│   └── st/stm32/                     SoC 定義（ベンダー別）
│       ├── soc.yml                   ファミリ/シリーズ/SoC 階層
│       └── stm32f4x/                 シリーズ固有
└── dts/arm/st/f4/                    DeviceTree ソース
    ├── stm32f4.dtsi                  ファミリ基底
    ├── stm32f411.dtsi                SoC 固有
    └── stm32f411Xe.dtsi              パッケージ/メモリ固有
```

### DeviceTree 6-8 層 include チェーン

Zephyr のハードウェア記述の核心。最大 6-8 段の include チェーンで段階的にハードウェアを詳細化する。

```
armv7-m.dtsi              (1) Cortex-M 共通プロパティ
  └── stm32f4.dtsi        (2) STM32F4 ファミリ共通ペリフェラル
      └── stm32f411.dtsi  (3) STM32F411 SoC 固有ペリフェラル
          └── stm32f411Xe.dtsi  (4) パッケージ（ピン数・メモリサイズ）
              └── nucleo_f411re.dts  (5) ボード固有（LED, ボタン, コネクタ）
                  └── app.overlay  (6) アプリケーション固有（ユーザー定義）
```

`.overlay` ファイルは `.dts` と同じ構文で、ベース DTS の上に差分を適用する。ビルドシステムが `boards/<board>.overlay` や `app.overlay` を自動検出して適用する。

---

## 主要メカニズム

### Kconfig による SoC/ボード選択チェーン

ボード選択から SoC・アーキテクチャの Kconfig シンボルを `select` で連鎖的に有効化する。

```kconfig
config BOARD_NUCLEO_F411RE
    bool "ST Nucleo F411RE"
    depends on SOC_STM32F411XE
    select SOC_SERIES_STM32F4X

config SOC_STM32F411XE
    bool
    select CPU_CORTEX_M4
    select CPU_HAS_FPU
```

### リンカスクリプト自動生成

DeviceTree のメモリノードから C マクロを生成し、リンカスクリプトテンプレートに展開する。

```
DeviceTree (.dts) → dtc → gen_defines.py → C ヘッダマクロ → C プリプロセッサ → linker.cmd
```

手書きのリンカスクリプトは不要。DeviceTree がメモリ定義の唯一の源泉となる。

### HWMv2 extend メカニズム

`board.yml` の `extend:` キーワードで既存ボードを拡張可能。

```yaml
board:
  name: my_custom_board
  extend: nucleo_f411re
  vendor: mycompany
```

基底ボードの DTS・Kconfig をベースに、差分のみを追加定義する。

### CMake 統合

- `board.cmake` でデバッグランナー (OpenOCD, J-Link, pyOCD 等) を定義
- `BOARD_ROOT` で Out-of-Tree ボード定義を追加
- West モジュールシステムで外部ボード定義を配布可能
- シールド機構で拡張基板を DTS オーバーレイとして重畳

---

## 長所と短所

| 観点 | 評価 | 詳細 |
|------|------|------|
| データ継承 | ◎ | DTS include チェーン + Kconfig select チェーン |
| リンカ生成 | ◎ | テンプレートから完全自動生成 |
| ボード拡張 | ◎ | `extend:` + overlay + shield の多段構成 |
| Out-of-Tree | ◎ | BOARD_ROOT, Module, Shield の 3 方式 |
| 単一源泉 | ○ | DeviceTree が唯一のハードウェア記述 |
| 学習コスト | △ | DeviceTree + Kconfig + CMake の三重設定 |
| ファイル数 | △ | 1 ボード最低 5 ファイル |
| ツール依存 | △ | dtc, gen_defines.py, CMake の 3 ツール必須 |

---

## UMI への示唆

1. **DTS include チェーンの段階的詳細化** は UMI の Lua dofile() チェーンと同じ思想。ただし Zephyr は 6-8 段と深く、UMI は 2 段（family → mcu）でより簡潔
2. **リンカスクリプトの自動生成** は UMI でも採用すべき。DeviceTree の代わりに Lua データベースからの生成が適切
3. **vendor はディレクトリのグループ化のみ** に使用し、データ継承には関与させない設計は UMI でも踏襲すべき
4. **シールド機構** のような拡張基板の重畳パターンは、オーディオ機器のモジュラー設計に応用可能

---

## 参照

- [Board Porting Guide](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html)
- [HWMv2 Migration](https://docs.zephyrproject.org/latest/hardware/porting/hwmv2.html)
- [Devicetree HOWTOs](https://docs.zephyrproject.org/latest/build/dts/howtos.html)
- [Shield Guide](https://docs.zephyrproject.org/latest/hardware/porting/shields.html)
- [Zephyr GitHub](https://github.com/zephyrproject-rtos/zephyr)
