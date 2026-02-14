# xmake ルール間通信とライフサイクル順序の問題分析

**ステータス:** 分析完了
**作成日:** 2026-02-08
**関連文書:** ARCHITECTURE_FINAL.md Section 9.1, MIGRATION_PLAN.md Phase 6

---

## 1. 問題の概要

`umiport.board` ルールが `embedded.linker_script` の値を `on_config` 内で `target:set("values", ...)` しても、embedded ルールの `on_load` は既に実行完了しているため、その値を読み取れない。結果として embedded ルールはデフォルトの `common.ld` をリンカスクリプトとして使用し、プロジェクト固有の `linker.ld` が適用されない。

```
期待した動作:
  umiport.board が embedded.linker_script を設定 → embedded がその値を使用

実際の動作:
  embedded:on_load() が linker_script=nil を読む → common.ld をフォールバック適用
  umiport.board:on_config() が値を設定 → 誰も読まない（手遅れ）
```

---

## 2. xmake ルールのライフサイクル

### 2.1 実行フェーズと順序

xmake のターゲットビルドは以下の順序で処理される:

```
Phase 0: ターゲット定義のパース
  └─ set_values(), add_files(), add_rules() 等が即時評価される

Phase 1: on_load()
  └─ 全ルールの on_load が add_rules() 宣言順（左→右）で実行
  └─ 全ターゲットの on_load が完了するまでブロック

Phase 2: after_load()
  └─ 全ルールの after_load が実行

Phase 3: on_config()
  └─ 全ルールの on_config が add_rules() 宣言順で実行

Phase 4: before_build() → コンパイル → after_link()
```

### 2.2 保証されていること

| 保証 | 説明 |
|------|------|
| `set_values()` の即時可視性 | Phase 0 で評価されるため、on_load/on_config の両方から `target:values()` で読める |
| `on_load` → `on_config` の順序 | **全ターゲットの** on_load が完了してから on_config が開始される |
| ルール内の順序 | 同一フェーズ内では `add_rules()` の宣言順（左→右）で実行 |

### 2.3 保証されていないこと

| 非保証 | 説明 |
|--------|------|
| ルール間の値伝播 | Rule A の on_load で `target:set("values", ...)` した値が、Rule B の on_load で見えるかは**順序依存** |
| on_config での values 書き込み | on_config で `target:set("values", ...)` しても、on_load で既に読まれた値は**遡及しない** |
| ルール間依存宣言 | ルール間に `add_deps()` のような依存メカニズムは**存在しない** |

---

## 3. 問題の構造的分析

### 3.1 二つのルールの設計意図

```
embedded ルール（arm-embedded パッケージ）
├─ on_load(): MCU情報読み込み、ツールチェイン設定、リンカスクリプト適用
├─ after_load(): 設定情報の表示
├─ before_build(): ビルド設定の最終調整
└─ after_link(): ELF/HEX/BIN 生成、サイズレポート

umiport.board ルール（プロジェクトローカル）
└─ on_config(): ボード固有 includedirs + startup/syscalls/linker 追加
```

### 3.2 問題が発生するケース

`add_rules("embedded", "umiport.board")` の場合:

```
1. embedded:on_load()
   ├─ target:values("embedded.linker_script") → nil（未設定）
   ├─ → フォールバック: common.ld を -T フラグとして追加
   └─ target:add("ldflags", "-T/path/to/common.ld")

2. umiport.board:on_config()
   ├─ target:set("values", "embedded.linker_script", "/path/to/linker.ld")
   └─ → embedded は既に on_load 完了、この値を読む機会がない
```

### 3.3 根本原因

ARCHITECTURE_FINAL.md Section 9.1 の設計は、`umiport.board` が `embedded.linker_script` を設定すれば embedded ルールがそれを使うと想定していた。しかし:

1. **embedded ルールは on_load でリンカスクリプトを処理する** — on_config ではない
2. **on_load は全ルール横断で on_config より先に実行される** — Phase 1 が完了してから Phase 3 が開始
3. **umiport.board は on_config しか持たない** — on_load 時点では介入できない

これは xmake のライフサイクル設計に起因する**構造的制約**であり、embedded ルール固有のバグではない。ルール間で値を伝播するには、**書き込み側が読み取り側と同じフェーズ以前に実行される**必要がある。

---

## 4. 現在の回避策とその評価

### 4.1 採用中: ldflags フィルタ方式

```lua
-- board.lua (on_config)
local old_flags = target:get("ldflags") or {}
local new_flags = {}
for _, flag in ipairs(old_flags) do
    if not flag:find("^%-T") then
        table.insert(new_flags, flag)
    end
end
target:set("ldflags", new_flags)
target:add("ldflags", "-T" .. ld_path, {force = true})
```

| 評価軸 | 判定 |
|--------|------|
| 動作 | 正しく動く（全 ARM ターゲットでビルド確認済み） |
| 堅牢性 | **脆い** — embedded ルールの ldflags 内部構造に依存 |
| 保守性 | embedded ルールが `-T` フラグの形式を変えると壊れる |
| 可読性 | 「なぜフィルタが必要なのか」が自明でない |

### 4.2 却下: on_load での set_values

```lua
-- board.lua (on_load)
target:set("values", "embedded.linker_script", ld_path)
```

| 評価軸 | 判定 |
|--------|------|
| 動作 | **動かない** — embedded の on_load が先に実行されるため |
| 理由 | `add_rules("embedded", "umiport.board")` で embedded が左側 |

> 順序を逆にして `add_rules("umiport.board", "embedded")` にすれば on_load の順序は変わるが、embedded ルールが umiport.board の on_load で追加したファイルを前提とする可能性があり、別の問題を生む。**ルール宣言順序への依存は設計として不適切**。

### 4.3 検討: 使用側で set_values を明示

```lua
target("umirtm_stm32f4_renode")
    add_rules("embedded", "umiport.board")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.linker_script",
        path.join(os.scriptdir(), "../../umiport/src/stm32f4/linker.ld"))
    set_values("umiport.board", "stm32f4-renode")
```

| 評価軸 | 判定 |
|--------|------|
| 動作 | 確実に動く（Phase 0 で評価されるため両ルールから可視） |
| 堅牢性 | 最も堅牢 — ルール間通信ではなく入力値として宣言 |
| ボイラープレート | **増加** — linker.ld のパスを使用側が知る必要がある |
| DRY 違反 | MCU 名から linker.ld パスは一意に決まるのに重複記述 |

---

## 5. 推奨される対処法

### 5.1 短期: ldflags フィルタ方式の維持（現状）

現在の実装は全ターゲットで動作確認済み。embedded ルールが `-T` フラグの形式を変更する可能性は低い（リンカの標準オプション）。コメントで意図を明記し、テストで動作を保証する。

### 5.2 中期: embedded ルールへの拡張ポイント追加

embedded ルール（arm-embedded パッケージ）側に「リンカスクリプト上書き」のための公式メカニズムを追加する:

```lua
-- arm-embedded/rules/embedded/xmake.lua (on_load 内、linker script 適用部分)
local linker_script = target:values("embedded.linker_script")
if not linker_script then
    -- フォールバック: common.ld を使用
    -- ...
else
    -- ユーザー指定のリンカスクリプトを使用
    target:add("ldflags", "-T" .. linker_script, {force = true})
end
```

**重要:** この変更は Phase 0（`set_values`）で `embedded.linker_script` が宣言されていれば正しく動く。問題は on_config 等のコールバックからの後付け設定のみ。

この方式の利点:
- `set_values("embedded.linker_script", ...)` は Phase 0 で即時評価されるため、on_load 内で確実に読める
- `umiport.board` ルールが on_load で MCU 名から linker script パスを算出して `target:add("values", ...)` すれば、embedded ルールの on_load で見える**可能性がある**（ただし add_rules 順序依存）

### 5.3 長期: xmake への機能要望

xmake にルール間通信の公式メカニズムが存在しない。以下のいずれかが実現されれば根本解決する:

**案 A: ルール間依存宣言**

```lua
rule("umiport.board")
    add_deps("embedded")  -- embedded の on_load 完了後に自分の on_load を実行
```

**案 B: 遅延評価 values**

```lua
-- Phase 0 で関数として登録、on_load 時に評価
set_values("embedded.linker_script", function(target)
    local mcu = target:values("embedded.mcu")
    local family = mcu:match("^(stm32%a%d)")
    return path.join(umiport_src, family, "linker.ld")
end)
```

**案 C: on_config での ldflags 再計算トリガ**

embedded ルールが on_config フェーズでも linker script の変更を検知して再適用するようにする。

---

## 6. ARCHITECTURE_FINAL.md の修正が必要な箇所

Section 9.1 のコード例には以下の問題がある:

### 6.1 修正が必要

```lua
-- 現在の記述（動作しない）
target:set("values", "embedded.linker_script",
    path.join(mcu_src, "linker.ld"))
```

**理由:** `on_config` 内での `set("values", ...)` は embedded ルールの `on_load` には遡及しない。

### 6.2 修正案

Section 9.1 のコード例を実際の実装（ldflags フィルタ方式）に合わせるか、制約事項として注記を追加する:

```
> **注意:** `embedded.linker_script` は `set_values()` でターゲット定義時に
> 宣言する必要がある。ルールの on_config 内から後付けで設定しても、
> embedded ルールの on_load には反映されない（xmake ライフサイクル制約）。
> umiport.board ルールでは ldflags の直接操作で回避している。
```

---

## 7. 影響範囲

| 対象 | 影響 |
|------|------|
| ホストテスト | 影響なし（embedded ルール未使用） |
| WASM テスト | 影響なし（embedded ルール未使用） |
| ARM ターゲット（umiport.board 使用） | ldflags フィルタで対処済み |
| ARM ターゲット（umiport.board 未使用） | 影響なし（set_values で直接指定） |
| 新規ボード追加時 | board.lua の ldflags フィルタに依存する点を認識する必要あり |

---

## 8. テストによる保護

現在の ldflags フィルタ方式が壊れた場合、以下のシンボルが未定義となりリンクエラーで即座に検出される:

- `_sdata`, `_edata`, `_sidata` — data セクション境界
- `_sbss`, `_ebss` — BSS セクション境界
- `_estack` — スタック頂点
- `__init_array_start`, `__init_array_end` — グローバルコンストラクタ

ARM ターゲットのビルド確認（`xmake build <target>`）がこの問題のリグレッションテストとして機能する。
