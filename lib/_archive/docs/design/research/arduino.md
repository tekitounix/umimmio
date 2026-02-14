# Arduino

**分類:** フレームワーク / エコシステム
**概要:** 世界最大のマイコン開発エコシステム。boards.txt のフラットなプロパティファイルと platform.txt のビルドレシピテンプレートで構成される。ボード間の継承メカニズムは存在しない。

---

## アーキテクチャ概要

### ディレクトリ構造

```
hardware/arduino/avr/
├── boards.txt              ボード定義（フラットなキーバリュー）
├── platform.txt            ビルドレシピテンプレート
├── programmers.txt          書き込み器定義
└── variants/
    ├── standard/           Arduino Uno
    │   └── pins_arduino.h  ピンマッピング定義
    ├── mega/               Arduino Mega
    │   └── pins_arduino.h
    └── leonardo/           Arduino Leonardo
        └── pins_arduino.h
```

### boards.txt フラットプロパティ形式

```properties
uno.name=Arduino Uno
uno.build.board=AVR_UNO
uno.build.core=arduino
uno.build.f_cpu=16000000L
uno.build.mcu=atmega328p
uno.build.variant=standard
uno.upload.maximum_size=32256
uno.upload.maximum_data_size=2048
uno.upload.protocol=arduino
uno.upload.speed=115200
uno.upload.tool=avrdude
```

各ボードの全プロパティがドット区切りのフラットなキーバリューで記述される。配列やネストオブジェクトは表現不可。

---

## 主要メカニズム

### platform.txt ビルドレシピテンプレート

ビルドコマンドのテンプレート。`{変数}` で boards.txt の値を参照する。

```properties
compiler.c.cmd=avr-gcc
compiler.c.flags=-c -g -Os -w -std=gnu11 -ffunction-sections -fdata-sections -MMD

# レシピは boards.txt の値を変数として参照
recipe.c.o.pattern="{compiler.path}{compiler.c.cmd}" {compiler.c.flags} \
    -mmcu={build.mcu} -DF_CPU={build.f_cpu} \
    -DARDUINO={runtime.ide.version} -DARDUINO_{build.board} \
    {includes} "{source_file}" -o "{object_file}"
```

### variant メカニズム (pins_arduino.h)

`pins_arduino.h` がデジタル/アナログピン番号から物理ピンへのマッピングを定義する。

```cpp
// variants/standard/pins_arduino.h (Arduino Uno)
#define NUM_DIGITAL_PINS    20
#define NUM_ANALOG_INPUTS   6
#define LED_BUILTIN         13

static const uint8_t A0 = 14;
static const uint8_t A1 = 15;
```

ボードごとに異なるピンマッピングを variant ディレクトリで管理する。`build.variant` プロパティで参照先を指定する。

### menu 構文によるバリアント選択

同一ボード内のオプション分岐（MCU バリアント、クロック周波数等）を IDE メニューとして提供する。

```properties
mega.menu.cpu.atmega2560=ATmega2560 (Mega 2560)
mega.menu.cpu.atmega2560.build.mcu=atmega2560
mega.menu.cpu.atmega2560.upload.maximum_size=253952
mega.menu.cpu.atmega1280=ATmega1280
mega.menu.cpu.atmega1280.build.mcu=atmega1280
mega.menu.cpu.atmega1280.upload.maximum_size=126976
```

> **注:** menu 構文は同一ボード内のオプション分岐であり、ボード間の継承ではない。

### boards.local.txt オーバーライド

`boards.txt` と同じディレクトリに `boards.local.txt` を置くことで、既存ボードの設定を上書きできる。

```properties
# boards.local.txt
uno.build.f_cpu=8000000L    # クロック周波数を変更
uno.upload.speed=57600       # アップロード速度を変更
```

ただし、プラットフォーム更新で上書きされるリスクがある。

### 継承の不在

Arduino には **ボード間の継承メカニズムが一切存在しない**。各ボードは完全に独立した設定を持つ。同一 MCU を使うボードでも、全てのプロパティを個別に記述する必要がある。リンカスクリプトは variant ディレクトリに手書きで配置する。

---

## 長所と短所

| 観点 | 評価 | 詳細 |
|------|------|------|
| シンプルさ | ◎ | プロパティファイルで直感的。学習コスト最小 |
| Boards Manager 配布 | ◎ | ワンクリックインストール。JSON インデックスで管理 |
| エコシステム規模 | ◎ | 世界最大のユーザーベースとライブラリ群 |
| 継承メカニズム | -- | **ボード間の継承なし。各ボード独立** |
| 構造化データ | -- | **フラット形式のみ。配列やネスト不可** |
| リンカスクリプト | △ | variant ディレクトリに手書き |
| 低レベル制御 | △ | 割り込み優先度、DMA 等の詳細制御が困難 |

---

## UMI への示唆

1. **シンプルさの価値** は最大の教訓。Arduino の爆発的普及はシンプルさによるもの。UMI のボード定義も「JSON 1 ファイル」に匹敵する簡潔さを目指すべき
2. **variant/pins_arduino.h** のピンマッピング方式は UMI の board.hh に対応する概念。ボード固有のピンマッピングを型安全に提供する
3. **Boards Manager** のパッケージ配布モデルは xmake パッケージシステムと対応する
4. **継承なしのフラット構造** はスケールしない。Arduino の成功はシンプルさだが、UMI が目指す多ボード対応には継承メカニズムが必須

---

## 参照

- [Arduino Platform Specification](https://arduino.github.io/arduino-cli/latest/platform-specification/)
- [Arduino Boards Manager Spec](https://arduino.github.io/arduino-cli/latest/package_index_json-specification/)
