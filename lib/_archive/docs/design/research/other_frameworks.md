# その他のフレームワーク・ビルドシステム

本ファイルでは、個別の調査ファイルを設けるほどではないが UMI の設計判断に影響を与えるフレームワークとビルドシステムを概括する。

---

## 1. NuttX RTOS

**分類:** POSIX 互換 RTOS
**特徴:** Kconfig のみ (DeviceTree なし)、arch/chip/board の 3 層ディレクトリ構造

### アーキテクチャ

```
nuttx/
├── arch/arm/src/
│   ├── common/               アーキテクチャ共通（コンテキストスイッチ等）
│   ├── armv7-m/              ISA 固有
│   └── stm32/                チップファミリ固有
├── boards/arm/stm32/
│   └── stm32f4discovery/
│       ├── configs/           構成バリアント
│       │   ├── nsh/defconfig  NSH シェル構成
│       │   ├── usbnsh/defconfig  USB-NSH 構成
│       │   └── audio/defconfig   オーディオ構成
│       ├── scripts/ld.script  リンカスクリプト（手書き）
│       └── src/               ボード初期化コード
```

### 主要特徴

- **Kconfig + defconfig + Make.defs** で構成管理。DeviceTree は使用しない
- **同一ボードの複数構成**: `configs/` ディレクトリに複数の defconfig を配置。`tools/configure.sh stm32f4discovery:nsh` で選択
- **Out-of-Tree ボード**: `-B` オプションで外部ディレクトリ指定可能
- **リンカスクリプトは手書き**: テンプレート生成なし。ボードごとに個別管理

### 評価

| 観点 | 評価 | 詳細 |
|------|------|------|
| ディレクトリ構造 | ◎ | arch/chip/board が物理ディレクトリに直接対応 |
| 構成バリアント | ◎ | 同一ボードで複数構成の切り替え |
| リンカスクリプト | △ | ボードごとに手書き |
| データ継承 | △ | Kconfig 依存関係のみ。明示的継承なし |

### UMI への示唆

- **構成バリアント** の概念は、UMI のデバッグ/リリース/テスト構成に応用可能
- **3 層ディレクトリ** は物理構造の直感的表現として参考になるが、データ継承は別途必要

---

## 2. libopencm3

**分類:** 軽量ペリフェラルライブラリ
**特徴:** ボード概念なし (デバイスレベルのみ)、devices.data パターンマッチ継承

### devices.data パターンマッチング

```
# パターン           親          データ
stm32f407?g*      stm32f407   ROM=1024K RAM=128K
stm32f407??       stm32f40x
stm32f40x         stm32f4     RAM=128K CCM=64K
stm32f4           stm32       ROM_OFF=0x08000000 RAM_OFF=0x20000000
stm32             END         CPPFLAGS=-mthumb ARCH_FLAGS=-mcpu=cortex-m4...
```

`?` は任意の 1 文字、`*` は任意の文字列。右の親列をたどることでデータが累積的にマージされる。

### genlink.py リンカ生成

```
devices.data + MCU 名
  → genlink.py → generated.<mcu>.ld (#define)
    → C プリプロセッサ → linker.ld.S テンプレート展開
      → 最終リンカスクリプト
```

C プリプロセッサの `#if defined(CCM)` で CCM の有無に応じた条件分岐が可能。

### ペリフェラル IP バージョン共有

```
lib/stm32/common/
├── usart_common_all.c          全 STM32 ファミリ共通
├── gpio_common_f0234.c         F0, F2, F3, F4 の GPIO IP 共通
├── i2c_common_v1.c             I2C v1 IP を持つファミリ群
└── i2c_common_v2.c             I2C v2 IP を持つファミリ群
```

**ファミリではなくペリフェラル IP バージョンで共有単位を決定** する設計が秀逸。

### 評価

| 観点 | 評価 | 詳細 |
|------|------|------|
| 単一源泉 | ◎ | devices.data がメモリ定義の唯一源泉 |
| リンカ生成 | ◎ | テンプレート + genlink で生成 |
| IP ベース共有 | ★ | common/ の IP バージョン別共有が秀逸 |
| ボード抽象化 | -- | ボード概念が存在しない |
| ボード追加 | ◎ | devices.data に 1 行追加するだけ |

---

## 3. ビルドシステムにおけるオーバーレイパターン

### CMake (stm32-cmake)

CMSIS-Pack のデバイスデータを直接利用し、CMake のネイティブ機能でリンカスクリプトを生成する。

```cmake
function(stm32_generate_linker_script TARGET)
    stm32_get_memory_info(${DEVICE} FLASH_ORIGIN FLASH_SIZE RAM_ORIGIN RAM_SIZE)
    configure_file(${STM32_LINKER_TEMPLATE} ${CMAKE_BINARY_DIR}/linker.ld @ONLY)
endfunction()
```

### Bazel / Buck2

`constraint_setting` / `constraint_value` / `platform` の 3 要素で宣言的にプラットフォームを定義。`select()` で条件分岐。**継承メカニズムはない** が、constraint の組み合わせで表現力を確保。

```python
platform(
    name = "stm32f407_discovery",
    constraint_values = [":stm32f4", "@platforms//cpu:armv7e-m", "@platforms//os:none"],
)
cc_library(
    name = "hal",
    srcs = select({":stm32f4": ["hal_stm32f4.cc"], ":stm32h7": ["hal_stm32h7.cc"]}),
)
```

### Meson

INI 形式のクロスファイルで全クロスコンパイル設定を宣言。ボード/MCU の概念はなく、ツールチェーン設定のみ。

### 他ドメインのオーバーレイパターン

| パターン | 上書き方式 | BSP への示唆 |
|---------|----------|-------------|
| Docker Layer | 不変レイヤー積層 | ファミリ定義を不変ベースとし、MCU/ボードを上位レイヤーに |
| Nix Overlay | 関数合成 (final/prev) | Lua dofile() チェーンはこのパターンの簡易版 |
| Terraform Module | パラメータ注入 | MCU 定義をパラメータ化モジュールとして扱う |
| Helm values.yaml | YAML ディープマージ | Lua テーブルマージと同等 |
| CSS Cascade | 詳細度ベース | アーキテクチャ < ファミリ < MCU < ボードの詳細度順 |

### xmake の BSP 関連機能

- **target:values() / set_values()**: ボードパラメータの宣言的設定
- **target:data() / data_set()**: ルール間でのランタイムデータ共有（テーブル格納可能）
- **add_configfiles()**: テンプレート変数置換による linker.ld 生成
- **io.writefile()**: Lua コードによる動的ファイル生成
- **option()**: CLI からのボード選択 (`xmake f --board=stm32f4-disco`)
- **ルールライフサイクル**: Phase 0 (パース) → Phase 1 (on_load) → Phase 2 (after_load) → Phase 3 (on_config) → Phase 4 (ビルド)

---

## 総合比較表

| ビルドシステム | プラットフォーム表現 | 継承 | BSP 概念 |
|--------------|-------------------|------|---------|
| CMake (stm32-cmake) | CMake 変数 + CMSIS-Pack | なし | あり (CMSIS 依存) |
| Bazel/Buck2 | constraint + platform | なし (組み合わせ) | なし |
| Meson | INI クロスファイル | なし | なし |
| Nix | Nix 式 | overlay | なし |
| Yocto/OE | MACHINE conf | require/include チェーン | BSP レイヤー |
| xmake | Lua ルール + values | dofile() チェーン | カスタム実装可能 |

---

## 参照

- [NuttX Custom Boards](https://nuttx.apache.org/docs/latest/guides/customboards.html)
- [libopencm3 devices.data](https://github.com/libopencm3/libopencm3/blob/master/ld/devices.data)
- [stm32-cmake (GitHub)](https://github.com/ObKo/stm32-cmake)
- [Bazel Platforms](https://bazel.build/concepts/platforms)
- [Meson Cross-compilation](https://mesonbuild.com/Cross-compilation.html)
- [Yocto BSP Developer's Guide](https://docs.yoctoproject.org/bsp-guide/bsp.html)
- [xmake Rules](https://xmake.io/#/manual/custom_rule)
