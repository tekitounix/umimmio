# リンカスクリプト設計: 理想的な配置と責務分離

**ステータス:** 設計提案
**作成日:** 2026-02-08
**前提文書:** XMAKE_RULE_ORDERING.md（xmake ルール間通信の制約分析）

---

## 1. 現状の問題

### 1.1 リンカスクリプトの3つの関心事

リンカスクリプトの内容は本質的に3つの独立した関心事に分解できる:

| 関心事 | 決定者 | 例 |
|--------|--------|-----|
| **MEMORY レイアウト** | MCU + ボード | FLASH origin/size, SRAM origin/size, CCM |
| **標準 SECTIONS** | ベアメタル C/C++ 共通 | .text, .data, .bss, .init_array, .stack |
| **アプリケーション固有 SECTIONS** | OS / アプリケーション | .dma_buffer, .audio_ccm, .shared, .app_header |

### 1.2 現状の実装

現在、これら3つが**1ファイルに混在**しているか、**ファイル分割が不完全**:

```
lib/umiport/src/stm32f4/linker.ld     ← MEMORY + 標準SECTIONS（ライブラリテスト用）
examples/stm32f4_kernel/kernel.ld     ← MEMORY + 標準 + kernel固有(.dma_buffer, .shared等)
lib/umi/app/app.ld                    ← MEMORY のみ（INCLUDE app_sections.ld で分離済み）
lib/umi/app/app_sections.ld           ← SECTIONS のみ（app.ld から INCLUDE）
arm-embedded common.ld                ← MEMORY(__defsym) + 標準SECTIONS
```

`app.ld` / `app_sections.ld` は分離設計のモデルケースだが、kernel.ld と umiport の linker.ld にはこの設計が適用されていない。

### 1.3 xmake ルール順序との関連

XMAKE_RULE_ORDERING.md で分析した通り、`umiport.board` ルールの `on_config` から `embedded.linker_script` を設定しても embedded ルールの `on_load` には遡及しない。

しかし **リンカスクリプトを分割し、MEMORY とアプリ SECTIONS を別ファイルにすれば**、この問題構造が変わる:

- **MEMORY**: MCU/ボード依存 → umiport が提供（`-Wl,--defsym` またはファイル）
- **標準 SECTIONS**: 普遍的 → embedded ルールの common.ld が担当
- **アプリ SECTIONS**: アプリ依存 → 使用側が追加の `-T` で提供

GNU ld は **複数の `-T` オプションを受け付ける**。セクション定義は結合（union）されるため、ファイルを分割しても最終結果は同じになる。ただし **MEMORY 定義は1つのファイルにのみ存在する必要がある**。

---

## 2. リンカスクリプト分割の設計

### 2.1 理想的なファイル配置

```
lib/umiport/
├── src/
│   └── stm32f4/
│       ├── startup.cc
│       ├── syscalls.cc
│       └── linker/
│           ├── memory.ld        ← MCU の MEMORY 定義のみ
│           └── sections.ld      ← ベアメタル C/C++ 標準 SECTIONS
│
├── include/umiport/board/
│   ├── stm32f4-renode/
│   │   ├── platform.hh
│   │   ├── board.hh
│   │   └── memory.ld           ← ボード固有の MEMORY 上書き（任意）
│   └── stm32f4-disco/
│       ├── platform.hh
│       ├── board.hh
│       └── memory.ld           ← ボード固有の MEMORY 上書き（任意）

examples/
├── stm32f4_kernel/
│   ├── kernel.ld               ← MEMORY + kernel固有SECTIONS（完全カスタム）
│   └── sections/
│       └── kernel_extra.ld     ← .dma_buffer, .audio_ccm, .shared 等
└── synth_app/
    └── （app.ld + app_sections.ld を使用 — 既存設計のまま）
```

### 2.2 各ファイルの責務

#### `umiport/src/stm32f4/linker/memory.ld` — MCU メモリマップ

```ld
/* STM32F407VG Memory Map */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1M
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    CCM   (rwx) : ORIGIN = 0x10000000, LENGTH = 64K
}

ENTRY(_start)
```

MCU の物理メモリだけを定義する。パーティショニング（kernel/app 分割等）はここに含めない。

ボードが SRAM サイズの異なる MCU バリアントを使う場合（STM32F407VE vs VG 等）、`board/<name>/memory.ld` で上書きする。

#### `umiport/src/stm32f4/linker/sections.ld` — 標準セクション

```ld
/* Standard bare-metal C/C++ sections */
/* Expects MEMORY regions to be defined by a preceding -T file */

_stack_size = DEFINED(_stack_size) ? _stack_size : 8K;
_heap_size  = DEFINED(_heap_size)  ? _heap_size  : 32K;

SECTIONS
{
    .isr_vector : { ... } > FLASH
    .text       : { ... } > FLASH
    .rodata     : { ... } > FLASH
    .init_array : { ... } > FLASH
    .data       : { ... } > SRAM AT> FLASH
    .bss        : { ... } > SRAM
    .heap       : { ... } > SRAM
    .stack      : { ... } > SRAM
}

PROVIDE(_estack = ...);
/* 標準シンボルの PROVIDE */
```

これは embedded ルールの `common.ld` と同等の役割。ただし `common.ld` は `--defsym` で MEMORY サイズを注入する設計であり、umiport 版は MCU 固有の `memory.ld` と組み合わせる設計。

#### kernel.ld — アプリケーション完全カスタム

kernel.ld のような複雑なケースでは、MEMORY もSECTIONS も完全にカスタムになる:

- FLASH を kernel 用 (384K) と app 用 (128K) に分割
- SRAM を kernel 用 (48K), app 用 (48K), shared (16K) に分割
- `.dma_buffer`, `.audio_ccm`, `.shared` 等の独自セクション

このケースでは **`memory.ld` + `sections.ld` の合成では表現しきれない**。アプリケーションが完全なリンカスクリプトを持つのが正しい。

### 2.3 合成パターンの分類

| パターン | MEMORY | SECTIONS | 使用者 |
|----------|--------|----------|--------|
| **A: 全自動** | umiport MCU 固定 | umiport 標準 | ライブラリテスト, 単純なデモ |
| **B: セクション追加** | umiport MCU 固定 | 標準 + アプリ追加 | .ccm 等を使う中規模アプリ |
| **C: メモリ分割** | アプリがカスタム | アプリがカスタム | kernel, bootloader |

---

## 3. xmake での実現方法

### 3.1 パターン A: 全自動（ライブラリテスト）

```lua
target("umirtm_stm32f4_renode")
    add_rules("embedded", "umiport.board")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("umiport.board", "stm32f4-renode")
    -- linker script は umiport.board ルールが自動設定
```

`umiport.board` ルールが `memory.ld` + `sections.ld` を `-T` で渡す。embedded ルールの `common.ld` は使用しない。

### 3.2 パターン B: セクション追加

```lua
target("audio_demo_stm32f4")
    add_rules("embedded", "umiport.board")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("umiport.board", "stm32f4-disco")
    -- 追加セクション定義
    add_ldflags("-T" .. path.join(os.scriptdir(), "extra_sections.ld"), {force = true})
```

`extra_sections.ld`:
```ld
/* Additional sections for audio application */
SECTIONS
{
    .audio_buf (NOLOAD) : {
        *(.audio_buf*)
    } > CCM
}
```

GNU ld の複数 `-T` を利用して、umiport の標準リンカスクリプトに追加セクションを結合する。

### 3.3 パターン C: 完全カスタム（kernel, bootloader）

```lua
target("stm32f4_kernel")
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script", path.join(os.scriptdir(), "kernel.ld"))
    -- umiport.board は使わない（linker script が完全カスタムのため）
    -- ただしボード includedirs だけ必要なら:
    add_includedirs(path.join(os.projectdir(), "lib/umiport/include/umiport/board/stm32f4-disco"))
```

kernel.ld はアプリケーションが完全所有。MEMORY 分割、独自セクション、スタック配置すべてがアプリの責務。

### 3.4 umiport.board ルールの改修

現在の ldflags フィルタ方式を、分割リンカスクリプト方式に変更する:

```lua
rule("umiport.board")
    on_config(function(target)
        local board = target:values("umiport.board")
        if not board then return end

        local umiport_dir = path.join(os.scriptdir(), "..")
        local board_include = path.join(umiport_dir, "include/umiport/board", board)

        target:add("includedirs", board_include, {public = false})

        local use_startup = target:values("umiport.startup")
        if use_startup == "false" then return end

        local mcu = target:values("embedded.mcu")
        if not mcu then return end

        local mcu_family = mcu:match("^(stm32%a%d)")
        if not mcu_family then return end

        local mcu_src = path.join(umiport_dir, "src", mcu_family)
        target:add("files", path.join(mcu_src, "startup.cc"))
        target:add("files", path.join(mcu_src, "syscalls.cc"))

        -- リンカスクリプト: embedded rule の common.ld を除去し、
        -- umiport の memory.ld + sections.ld を設定
        local linker_dir = path.join(mcu_src, "linker")

        -- ボード固有 memory.ld があればそちらを優先
        local board_memory = path.join(board_include, "memory.ld")
        local memory_ld = os.isfile(board_memory)
            and board_memory
            or  path.join(linker_dir, "memory.ld")

        local sections_ld = path.join(linker_dir, "sections.ld")

        -- embedded rule の -T フラグを除去
        local old_flags = target:get("ldflags") or {}
        local new_flags = {}
        for _, flag in ipairs(old_flags) do
            if not flag:find("^%-T") then
                table.insert(new_flags, flag)
            end
        end
        target:set("ldflags", new_flags)

        -- 分割リンカスクリプトを順序付きで追加
        target:add("ldflags", "-T" .. memory_ld, {force = true})
        target:add("ldflags", "-T" .. sections_ld, {force = true})
    end)
```

### 3.5 embedded.linker_script との関係

embedded ルールの `set_values("embedded.linker_script", ...)` は **パターン C（完全カスタム）専用** として位置づける:

| 設定方法 | 用途 | 動作 |
|----------|------|------|
| `umiport.board` ルール | パターン A, B | memory.ld + sections.ld を自動設定 |
| `set_values("embedded.linker_script", ...)` | パターン C | embedded ルールが直接使用（on_load 内） |

この2つは排他的。`umiport.board` を使うターゲットでは `embedded.linker_script` を設定しない。`embedded.linker_script` を使うターゲットでは `umiport.board` ルールを使わないか、`umiport.startup = "false"` で startup/linker の自動設定を無効化する。

---

## 4. embedded ルール（common.ld）との棲み分け

### 4.1 common.ld の設計

embedded ルールの `common.ld` は `--defsym` でメモリサイズを外部注入する設計:

```ld
MEMORY
{
    FLASH (rx)  : ORIGIN = __flash_origin, LENGTH = __flash_size
    RAM (rwx)   : ORIGIN = __ram_origin,   LENGTH = __ram_size
    CCMRAM (rw) : ORIGIN = __ccm_origin,   LENGTH = __ccm_size
}
```

```lua
-- embedded rule (on_load)
target:add("ldflags", "-Wl,--defsym,__flash_size=" .. flash_bytes)
target:add("ldflags", "-Wl,--defsym,__ram_size=" .. ram_bytes)
```

**利点:** MCU データベースから自動的にメモリサイズを取得でき、リンカスクリプトを書かなくてよい。

**制約:**
- MEMORY 定義が固定（FLASH/RAM/CCMRAM の3領域のみ）
- カスタムセクションの追加が困難
- シンボル名が umiport の startup.cc と異なる（`_data_start` vs `_sdata` 等）

### 4.2 棲み分け方針

```
embedded rule の common.ld:
  - umiport を使わないプロジェクト向け
  - MCU データベースだけで完結する単純なターゲット
  - 外部パッケージとしての汎用性を優先

umiport の memory.ld + sections.ld:
  - UMI プロジェクト内のライブラリテスト・デモ向け
  - umiport の startup.cc/syscalls.cc と整合するシンボル名
  - ボード固有のメモリ上書きをサポート

アプリの kernel.ld 等:
  - OS、ブートローダー等の特殊用途
  - 完全にアプリケーションが所有
```

---

## 5. xmake ルール順序問題への影響

リンカスクリプトを分割しても、ldflags フィルタ（`-T` の除去と再追加）は引き続き必要。しかし設計意図が明確になる:

| 従来 | 分割後 |
|------|--------|
| 「なぜ `-T` をフィルタしているのか不明」 | 「common.ld を umiport の memory.ld + sections.ld に置換している」 |
| linker.ld が1ファイルで全責務を担う | 各ファイルの責務が明確 |
| アプリが追加セクションを入れるには linker.ld をフォーク | `-T extra.ld` を追加するだけ |

**根本解決（xmake のルール間通信改善）がなくても、分割によって問題の影響範囲が限定される。**

---

## 6. 実装優先度

| 優先度 | 作業 | 理由 |
|--------|------|------|
| 今すぐ不要 | linker.ld の分割 | 現在の1ファイル方式で全テスト・ARM ビルドが通る |
| 中期 | memory.ld + sections.ld 分割 | 新ボード追加時やアプリのセクションカスタマイズ時に価値が出る |
| kernel.ld 対応時 | パターン C の整備 | stm32f4_kernel が umiport.board 対応する際に必要 |

現時点では設計方針の文書化に留め、実装は必要が生じた時点で行う。
