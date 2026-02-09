# umiport アーキテクチャレビュー (Claude Code)

## 概要

本ドキュメントは `lib/umiport/docs/design/ARCHITECTURE.md` の設計妥当性を、**実装コードとの突き合わせ**に基づいて評価したものである。

レビュー対象:
- ARCHITECTURE.md の設計方針・構成図・原則
- umibench / umimmio / umirtm / umitest の実装状態
- umiport / umiport-boards の実装状態
- 各ライブラリの xmake.lua（tests/, examples/ 含む）

---

# 総合評価: B+

基本設計思想は非常に優れている。「ソースにハード依存を漏らさない」という原則は実装レベルで一貫しており、ライブラリ本体のコードに `#ifdef` や MCU 固有インクルードは存在しない。

しかし、ドキュメントと実装の間に構造的な乖離があり、拡張性においても改善の余地がある。

| 評価軸 | 評価 | コメント |
|--------|------|---------|
| 基本設計思想 | ★★★★★ | 「ソースにHW依存を漏らさない」は正しく、美しい |
| 現時点のクリーンさ | ★★★☆☆ | umiport 内の MCU 固有コード、二重 Platform が気になる |
| ドキュメント精度 | ★★☆☆☆ | 将来ビジョンと現実が区別なく混在している |
| 拡張性（新ボード） | ★★★☆☆ | ボイラープレートが比例的に増加する |
| 拡張性（新アーキ） | ★★☆☆☆ | startup.cc が Cortex-M 前提でハードコード |
| 認知負荷 | ★★★☆☆ | 二重 Platform の暗黙知、相対パスの嵐 |

---

# 強み

## 1. 同名ヘッダ方式の切り替えメカニズム

`startup.cc` の `#include "platform.hh"` を xmake の includedirs で解決する設計は非常にクリーンである。ソースコードに `#ifdef` を一切持ち込まず、ビルドシステムのみで切り替えを実現している。

検証結果: `startup.cc`, `syscalls.cc` ともに `#include "platform.hh"` のみで、MCU 名や条件分岐は一切含まれない。

## 2. 出力経路の抽象化

```
rt::println("Hello {}", 42)
    ↓
rt::printf(fmt, args...)        ← umirtm（HW 非依存）
    ↓
::_write(1, buf, len)           ← newlib syscall（umiport/src/*/syscalls.cc）
    ↓
umi::port::Platform::Output::putc(c)  ← platform.hh で切り替え
    ↓
┌─ stm32f4-renode:  USART1 レジスタ書き込み
├─ stm32f4-disco:   RTT リングバッファ
└─ ホスト:          stdout
```

ライブラリが HW を完全に知らないまま機能する理想的な形である。`umirtm` のソースに `#include "platform.hh"` は不要であり、`_write()` syscall が暗黙的にボードの Output にルーティングする。

## 3. ライブラリの 0 依存性

umirtm, umibench, umimmio のライブラリ本体が**本当に**HW 非依存であることをコードレベルで確認した。

- `umirtm/include/` — HW 参照なし
- `umibench/include/` — HW 参照なし
- `umimmio/include/` — HW 参照なし（レジスタ抽象化のみ）

## 4. ボード層による統合

MCU 実装とボード実装の責務分離が明確である。同一ソース（`print_demo.cc`）が `stm32f4-renode` と `stm32f4-disco` の両方でビルドできることを、`umirtm/examples/xmake.lua` で確認した。

---

# 構造的問題点

## 1. ドキュメントと実態の深刻な乖離

ARCHITECTURE.md は**将来ビジョン**と**現在の実装**を区別なく混在して記述している。

### 存在しない層

| ARCHITECTURE.md に記載 | 実態 |
|------------------------|------|
| `umiport-arch/` | 存在しない |
| `umiport-stm32/` | 存在しない |
| `umiport-stm32f4/` | 存在しない |
| `umiport-stm32h7/` | 存在しない |

### umiport の責務違反

ARCHITECTURE.md の原則表では:

> `umiport` — 禁止事項: **MCU固有コード**

にもかかわらず、現在の umiport には以下の MCU 固有コードが含まれている。

| ファイル | 内容 | MCU 固有性 |
|---------|------|-----------|
| `umiport/include/umiport/stm32f4/uart_output.hh` | USART1 レジスタ操作（`0x40011000`） | STM32F4 固有 |
| `umiport/src/stm32f4/startup.cc` | ベクタテーブル、FPU 有効化（`0xE000ED88`） | Cortex-M 固有 |
| `umiport/src/stm32f4/syscalls.cc` | newlib syscall 実装 | toolchain 固有 |
| `umiport/src/stm32f4/linker.ld` | メモリレイアウト | STM32F407 固有 |

パッケージの責務定義と実装が矛盾している。

---

## 2. umibench の二重 Platform 問題

`platform.hh` という同名ファイルが**2つの全く異なる型**を定義している。

| ファイル | 定義する型 | 目的 |
|---------|-----------|------|
| `umiport-boards/.../platform.hh` | `umi::port::Platform` | 起動・syscall 用 |
| `umibench/tests/stm32f4-renode/umibench/platform.hh` | `umi::bench::Platform` | ベンチマーク用 |

### 衝突リスク

`startup.cc` は `#include "platform.hh"`（引用符）で `umi::port::Platform` を取得し、テストコードは `#include <umibench/platform.hh>`（山括弧＋パス付き）で `umi::bench::Platform` を取得する。この区別は includedirs の順序に依存しており、ARCHITECTURE.md に文書化されていない。

### 新規ボード追加時の認知負荷

STM32H7 を追加する場合、umiport-boards に `platform.hh` を作るだけでなく、umibench 側にも別の `platform.hh` を作る必要がある。この暗黙の知識は危険である。

### 禁止規約との緊張関係

`umibench/tests/stm32f4-renode/umibench/platform.hh` は以下をインクルードしている:

```cpp
#include <umiport/stm32f4/uart_output.hh>
#include <umiport/arm/cortex-m/dwt.hh>
```

ARCHITECTURE.md は「ライブラリが umiport を直接参照」することを禁止しているが、テストの platform.hh がこれに該当するかの判断基準が不明確である。

---

## 3. startup/syscalls のスコープ問題

`umiport/src/stm32f4/` に配置された startup.cc, syscalls.cc, linker.ld は MCU 固有コードそのものである。

- `startup.cc`: Cortex-M ベクタテーブル、FPU CPACR レジスタ（`0xE000ED88`）
- `linker.ld`: STM32F407VG のメモリレイアウト（FLASH 1M, RAM 128K）

ARCHITECTURE.md の依存関係図では:

```
umiport (deps: umimmio)  ← MCU固有コード禁止
```

現実には完全に MCU 固有である。

---

## 4. ボイラープレートの増加リスク

各ライブラリの `tests/xmake.lua` を比較すると、以下の完全に同一のパターンが 4 回繰り返されている:

```lua
local umiport_stm32f4 = path.join(os.scriptdir(), "../../umiport/src/stm32f4")
local board_stm32f4_renode = path.join(os.scriptdir(), "../../umiport-boards/include/board/stm32f4-renode")

target("xxx_stm32f4_renode")
    add_files(path.join(umiport_stm32f4, "startup.cc"))
    add_files(path.join(umiport_stm32f4, "syscalls.cc"))
    set_values("embedded.linker_script", path.join(umiport_stm32f4, "linker.ld"))
    add_deps("xxx", "umiport")
    add_includedirs(board_stm32f4_renode, {public = false})
```

新しいボード（STM32H7, RP2040 等）を追加するたびに、**全ライブラリの tests/xmake.lua を手動で修正**する必要がある。ボードの数 × ライブラリの数で増加し、スケールしない。

---

## 5. インクルードパスの矛盾

ARCHITECTURE.md のソースコード規約セクションでは:

```cpp
// 正解：同名ヘッダ（xmakeがパスを解決）
#include <stm32f4/gpio.hh>
```

しかし実際の実装では:

```cpp
#include <umiport/stm32f4/uart_output.hh>  // フルパス
#include <umiport/arm/cortex-m/dwt.hh>     // フルパス
```

禁止しているはずのフルパスインクルードが使われている。`umiport-stm32f4` パッケージが未実装であることが原因だが、ドキュメント上の禁止規約と矛盾する。

---

# 拡張性評価

## 新 MCU 追加シナリオ（例: STM32H7）

| 必要な作業 | 場所 | 問題 |
|-----------|------|------|
| startup.cc, syscalls.cc, linker.ld | `umiport/src/stm32h7/` を新設 | umiport の責務違反が拡大 |
| レジスタ操作（UART 等） | `umiport/include/umiport/stm32h7/` | MCU 固有コードがさらに増加 |
| platform.hh | `umiport-boards/include/board/stm32h7-xxx/` | 問題なし |
| テスト定義 | 4つ全ライブラリの tests/xmake.lua に手動追加 | ボイラープレート爆発 |
| umibench platform.hh | `umibench/tests/stm32h7-xxx/` を新設 | 暗黙の知識が必要 |

## 新アーキテクチャ追加シナリオ（例: RISC-V）

さらに深刻である。startup.cc の FPU 有効化、ベクタテーブル構造が完全に Cortex-M 前提のため、`umiport-arch` 相当のアーキテクチャ分離なしには対応不可能。

| 項目 | 評価 |
|------|------|
| MCU 追加（同アーキテクチャ） | 可能だが責務違反が拡大 |
| Board 追加（同 MCU） | 良好 |
| アーキテクチャ追加 | 構造変更が必要 |
| toolchain 切替 | 要改善（syscalls が newlib 前提） |
| 複数ボード同時ビルド | 良好（xmake ターゲット分離） |

---

# 改善提案

## 改善1: ドキュメントの正直化（最優先）

### 現状

ARCHITECTURE.md が将来構想と現在の実装を区別なく記述しており、読み手に混乱を与える。

### 提案

ドキュメントを「現在の実装」と「将来の拡張計画」に明確に分離する。

```markdown
## 現在の構成（実装済み）
...

## 将来の拡張計画（未実装）
以下は MCU/アーキテクチャが増加した際の分離計画である。
...
```

### 効果

- 新規参加者の混乱を排除
- 「何が動いていて何が未実装か」が一目で分かる

---

## 改善2: xmake ヘルパーによるボイラープレート削減

### 現状

各ライブラリの tests/xmake.lua に同一のボード設定コードが繰り返されている。

### 提案

共有ヘルパー関数を導入する。

```lua
-- lib/umiport/board_helpers.lua
function umiport_add_stm32f4_renode(deps)
    local umiport_stm32f4 = path.join(os.scriptdir(), "src/stm32f4")
    local board = path.join(os.scriptdir(), "../umiport-boards/include/board/stm32f4-renode")

    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.optimize", "size")
    add_files(path.join(umiport_stm32f4, "startup.cc"))
    add_files(path.join(umiport_stm32f4, "syscalls.cc"))
    set_values("embedded.linker_script", path.join(umiport_stm32f4, "linker.ld"))
    add_deps(deps, "umiport")
    add_includedirs(board, {public = false})
end
```

使用側:

```lua
target("umirtm_stm32f4_renode")
    set_kind("binary")
    set_default(false)
    umiport_add_stm32f4_renode("umirtm")
    add_files("test_*.cc")
target_end()
```

### 効果

- 新ボード追加時の修正箇所が 1 ファイルに集約
- 各ライブラリのボイラープレートが大幅削減

---

## 改善3: umiport の責務再定義

### 現状

umiport の責務定義が「MCU 固有コード禁止」なのに、MCU 固有コードが含まれている。

### 提案A: ドキュメントを現実に合わせる（保守的）

umiport を「ARM 組み込みの共通インフラ + 対応 MCU の低レベルコード」と再定義する。将来分離が必要になった時点で `umiport-arch` 等を切り出す。

### 提案B: startup/syscalls をボード層に移動（構造的改善）

```
lib/umiport-boards/
├── include/board/
│   ├── stm32f4-renode/
│   │   └── platform.hh
│   └── stm32f4-disco/
│       └── platform.hh
└── src/
    └── stm32f4/                # ← umiport/src/stm32f4/ から移動
        ├── startup.cc
        ├── syscalls.cc
        └── linker.ld
```

startup.cc は `#include "platform.hh"` で board の platform.hh を参照する。platform.hh が board に属するなら、startup も board 層に属するのが自然である。

### 提案C: MCU 固有ヘッダを umiport-boards 内部に統合

`umiport/include/umiport/stm32f4/uart_output.hh` の `RenodeUartOutput` は `stm32f4-renode` ボード以外では使われない。board 定義に統合（インライン化）することで、umiport から MCU 固有コードを排除できる。

---

## 改善4: umibench 二重 Platform の解消

### 現状

`umi::port::Platform`（起動用）と `umi::bench::Platform`（ベンチマーク用）が別々の `platform.hh` で定義されている。新ボード追加時に両方を作る必要がある。

### 提案

ボード定義に Timer 情報を含め、umibench はそこから取得する。

```cpp
// umiport-boards/include/board/stm32f4-renode/platform.hh
namespace umi::port {
struct Platform {
    using Output = ...;
    using Timer = cortex_m::DwtTimer;  // ← 追加
    static constexpr const char* name() { return "stm32f4-renode"; }
    static void init() { ... }
};
}
```

umibench 側はテンプレートでボードの型を利用する:

```cpp
template<typename BoardPlatform>
struct BenchPlatform {
    using Timer = typename BoardPlatform::Timer;
    using Output = typename BoardPlatform::Output;
};
```

### 効果

- 新ボード追加時に umibench 固有の platform.hh が不要になる
- Platform の定義が一元化され、認知負荷が低下

---

## 改善5: xmake ルールベースのボード選択（発展的）

### 提案

各ボードを宣言的に定義する xmake ルールを導入する。

```lua
-- 使用側（1行で完結）
target("umirtm_stm32f4_renode")
    add_rules("umiport.board", {board = "stm32f4-renode"})
    add_files("test_*.cc")
    add_deps("umirtm")
```

ルール内部で startup, syscalls, linker.ld, includedirs をすべて自動設定する。

### 効果

- ボード追加時のライブラリ側修正がゼロ
- ボイラープレート完全排除
- ビルド設定の一元管理

---

# 代替アプローチ

## Toolchain File 方式

CMake の toolchain file に相当する、ボード設定を1ファイルに集約する方式。

```
lib/umiport/
├── toolchains/
│   ├── stm32f4-renode.lua
│   ├── stm32f4-disco.lua
│   └── stm32h7-daisy.lua
```

各 toolchain ファイルが startup, linker.ld, platform.hh のパスをすべて宣言的に保持する。

### 利点

- ボード定義の完全な一元管理
- 新ボード追加が 1 ファイル作成で完結

### 欠点

- 既存の `embedded` ルールとの統合が必要

---

# 結論

## 維持すべき原則

以下の設計原則は優れており、今後も維持すべきである:

1. **ライブラリは HW を知らない** — umirtm, umibench, umimmio の 0 依存性
2. **統合は board 層のみ** — platform.hh によるワンポイント切り替え
3. **同名ヘッダ方式** — ソースコードに `#ifdef` を持ち込まない
4. **syscall 経由の出力ルーティング** — ライブラリが出力先を知らない

## 優先的に改善すべき点

| 優先度 | 改善内容 | 理由 |
|--------|---------|------|
| 高 | ドキュメントの正直化（現実と将来の分離） | 読み手の混乱を即座に排除 |
| 高 | xmake ヘルパーによるボイラープレート削減 | 日常の開発体験が大幅向上 |
| 中 | umiport の責務再定義または分離 | MCU 追加時の構造的矛盾を解消 |
| 中 | umibench 二重 Platform の解消 | 新ボード追加の認知負荷を低下 |
| 低 | xmake ルールベースのボード選択 | ボード数が増えてから検討で十分 |
