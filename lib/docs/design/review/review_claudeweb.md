# umiport アーキテクチャ設計レビュー

**対象**: `ARCHITECTURE.md` (umiport アーキテクチャ設計)
**レビュー日**: 2026-02-08
**ステータス**: 設計段階レビュー

---

## 総合評価

基本方針「ソースにハード依存を漏らさない」は一貫しており、同名ヘッダ＋includedirs 切り替えは組み込み開発で実績のあるパターン。STM32F4 + newlib 環境では十分に機能する設計だが、マルチプラットフォーム展開時にいくつかの構造的課題がある。

| 観点 | 評価 | コメント |
|------|------|---------|
| 基本方針 | ◎ | `#ifdef` 禁止、同名ヘッダ戦略は明確 |
| Cortex-M + newlib での完成度 | ◎ | STM32F4 については十分動く設計 |
| 非 ARM への拡張性 | △ | `_write()` 依存、startup 構造が ARM 前提 |
| プラグイン (Host) への拡張性 | △ | パイプラインの大部分が不要になる |
| ボード追加の容易さ | ○ | 手順は明確だが `umiport-boards` 単一パッケージが懸念 |
| IDE/ツール親和性 | △ | 同名ヘッダは clangd を混乱させる |
| パッケージ粒度 | △ | やや細かすぎ、arch 層の独立性に疑問 |
| ドキュメントの質 | ◎ | 非常に明快、図表も適切 |

---

## 1. 構造上の問題点

### 1.1 `_write()` syscall 依存が前提に組み込まれすぎている

newlib の `_write()` を経由する出力経路設計は Cortex-M + newlib 環境では動作するが、以下の環境で問題が生じる。

| 環境 | 問題 |
|------|------|
| **WASM** | newlib syscall が存在しない。`_write()` のルーティング先が未定義 |
| **Linux Host (plugin)** | stdout 直書きなら syscall 不要。ログシステムへの統合が自然 |
| **ESP-IDF (Xtensa/RISC-V)** | 独自の `esp_log` システムがあり、`_write()` オーバーライドと衝突する可能性 |
| **ベアメタル RISC-V** | picolibc や自前 libc 使用時、syscall の形式が異なる |

`_write()` による暗黙ルーティングは **Cortex-M + newlib 固有の統合パターン**であり、アーキテクチャの中核に据えるには脆弱。

**改善案**: 出力経路を syscall 依存ではなく、link-time 注入に変更する。

```cpp
// umirtm は write_bytes の存在だけを要求する
namespace rt::detail {
    extern void write_bytes(std::span<const std::byte> data);
}
```

各ボードの `platform.cc` がこのシンボルを定義する。syscall 経由の実装は Cortex-M 向けの**一実装**に格下げする。

### 1.2 `umiport` パッケージの責務が曖昧

現在の `umiport` には以下が混在している。

- Cortex-M MMIO（`arm/cortex-m/` ヘッダ）— アーキテクチャ共通
- startup.cc / syscalls.cc / linker.ld — STM32F4 固有

「共通インフラ」と称しながら MCU 固有コード（`umiport/src/stm32f4/`）が含まれている。

**改善案**: startup.cc / syscalls.cc / linker.ld を `umiport-stm32f4` または `umiport-boards` に移動し、`umiport` は本当に共通のもの（DWT、Cortex-M 共通レジスタ定義）だけに絞る。あるいは `umiport` 自体を廃止して `umiport-arch` に吸収する。

### 1.3 `umiport-boards` の単一パッケージ問題

全ボードが一つの `umiport-boards` に入っている設計は、ボード増加時に問題が生じる。

- `daisy_seed` 追加 → STM32H7 依存が入る → STM32F4 のみのプロジェクトにも H7 ヘッダが見える
- RP2040 追加 → ARM + RP2040 SDK の依存が混在
- xmake の依存グラフが「全ボードの全依存」を引きずる

**改善案A**: ボードごとに独立パッケージにする。

```
umiport-board-stm32f4-renode/
umiport-board-stm32f4-disco/
umiport-board-daisy-seed/
umiport-board-rp2040-pico/
```

**改善案B**: `boards/` ディレクトリにフラット配置し、選択ボードだけがビルドに参加する構造にする。

### 1.4 アーキテクチャ層とファミリー層の境界が曖昧

`umiport-arch`（cm4, cm7, cm33...）と `umiport-stm32`（ファミリー共通）の境界で判断に迷うケースがある。

| ケース | arch? | stm32? | 判断が難しい理由 |
|--------|-------|--------|-----------------|
| STM32H7 の Cortex-M7 キャッシュ操作 | `cm7/cache.hh` | `stm32h7/cache.hh` | IP は ARM、設定は SoC 依存 |
| CM33 の TrustZone 設定 | `cm33/` | SoC 層 | パーティション設定は SoC ごとに異なる |
| デュアルコア MCU (STM32H755 = CM7 + CM4) | 両方必要 | — | ボードが複数 arch を持つモデルが未定義 |

---

## 2. 拡張シナリオ検証

### 2.1 ESP32-S3 を追加する場合

```
umiport-arch/include/arch/xtensa_lx7/   ← スロットは存在する
umiport-esp32s3/include/esp32s3/         ← 新規作成可能
umiport-boards/include/board/esp32s3-devkit/  ← ここが問題
```

**主な課題**:

- ESP-IDF は独自ビルドシステム（CMake + idf.py）を持ち、xmake との統合が困難
- `_write()` syscall パターンが使えない（ESP-IDF の VFS レイヤーと衝突）
- FreeRTOS が前提の環境で UMI-OS カーネルとの共存方法が未定義

**リスク**: ESP32 追加時に設計の根本的見直しが必要になる可能性が高い。

### 2.2 Linux Host（VST3/CLAP プラグイン）の場合

- startup.cc 不要、linker.ld 不要、syscall 不要
- `Platform::Output` は `fprintf(stderr, ...)` か plugin host のログ API
- ベクタテーブルも割り込みも存在しない

現設計の「startup → syscall → Output」パイプラインの大部分がバイパスされ、**ボード層だけが意味を持つ**。設計として問題はないが、この経路をドキュメントに明記すべき。

### 2.3 RISC-V（CH32V, GD32VF）を追加する場合

```
umiport-arch/include/arch/rv32/   ← スロットは存在する
umiport-ch32v3/include/ch32v3/    ← 新規作成可能
```

比較的スムーズだが、startup.cc が Cortex-M 前提（ベクタテーブル形式、リセットハンドラ）なので RISC-V 向け startup は完全に別物になる。`umiport/src/` 以下に `rv32/startup.cc` を追加するとパッケージの責務がさらに肥大化する。

---

## 3. 設計の美しさに関する指摘

### 3.1 同名ヘッダ戦略の脆弱性

`#include "platform.hh"` が includedirs で解決される設計は動作するが、以下の弱点がある。

- **IDE の補完・解析が壊れやすい**: clangd が正しい `platform.hh` を見つけられない
- **ビルドエラーが暗号的になる**: 間違った `platform.hh` が選ばれても型エラーまで気づけない
- **コード追跡が困難**: 同名ファイルが複数存在するため grep/検索で曖昧

組み込み業界では許容されるトレードオフだが、「最もクリーン」とは言い切れない。

**代替案**: Concept ベースの明示的注入。

```cpp
template <umi::hal::Platform P>
void boot() {
    P::init();
    main();
}

// board 側で explicit instantiation
#include <board/stm32f4-renode/platform.hh>
template void boot<umi::port::Platform>();
```

startup のような低レイヤーでは現実的でない場面もあるため、**現方式との併用**が妥当。

### 3.2 命名規則の不統一

パッケージ命名に2種類のパターンが混在している。

| パターン | 例 |
|---------|-----|
| ハイフン区切り | `umiport-stm32f4`, `umiport-arch` |
| 連結 | `umirtm`, `umibench`, `umimmio`, `umihal` |

**提案**: 明確な命名規則を定める。例:「HW 非依存ライブラリは `umi<name>`、ポート層は `umiport-<target>`」として文書化する。

---

## 4. 代替アプローチの提案

### 4.1 ビルド時ボード型の明示注入（Zig/Rust スタイル）

includedirs による暗黙切り替えの代わりに、ビルド時に**ボード型を明示的に渡す**アプローチ。

```cpp
// config.hh (xmake が自動生成)
#include <board/stm32f4-renode/platform.hh>
using CurrentPlatform = umi::board::stm32f4_renode::Platform;
```

すべてのコードが `CurrentPlatform` を型として受け取る。同名ヘッダのマジックが不要になり、型安全性とIDE親和性が向上する。

### 4.2 HAL Concept の徹底活用

現在 `umihal` に Concept があるが、実際の統合は `platform.hh` の typedef で行われている。Concept をもっと積極的に使い、**コンパイル時にボード実装が HAL 契約を満たすことを検証**する。

```cpp
namespace umi::hal {
    template <typename T>
    concept OutputDevice = requires(T, char c) {
        { T::init() } -> std::same_as<void>;
        { T::putc(c) } -> std::same_as<void>;
    };

    template <typename T>
    concept Platform = requires {
        requires OutputDevice<typename T::Output>;
        { T::init() } -> std::same_as<void>;
    };
}

// 各 platform.hh でコンパイル時検証
static_assert(umi::hal::Platform<umi::port::Platform>);
```

ボード追加時に「何を実装すべきか」がコンパイラエラーで明確になる。

### 4.3 依存グラフの簡素化（3層モデル）

現在7+パッケージある構造を3層に整理する案。

```
Layer 1: umi-hal        (concepts + HW非依存ライブラリ: rtm, bench, mmio)
Layer 2: umi-chip-*     (SoC 実装: stm32f4, esp32s3, rp2040)
Layer 3: umi-board-*    (ボード統合: platform.hh + board.hh + startup)
```

arch 層を chip 層に吸収する（Cortex-M4 の知識は STM32F4 チップ実装に内包）。パッケージ数削減により依存管理の複雑さとビルド設定の肥大化を抑えられる。

---

## 5. 推奨アクション（優先度順）

### 最優先

1. **`_write()` syscall 依存の解消** — 出力経路の抽象を一段上げ、syscall 経由を Cortex-M 向け一実装に格下げする。WASM・Host・ESP32 への拡張が大幅にスムーズになる。

2. **`umiport` の責務整理** — startup.cc / syscalls.cc / linker.ld を適切なパッケージに移動し、`umiport` を純粋な共通コードだけにする。

### 高優先

3. **`umiport-boards` の分割** — ボードごとの独立パッケージ化で、不要な依存の伝播を防ぐ。

4. **Concept による static_assert** — 各 `platform.hh` で HAL 契約の充足をコンパイル時検証する。

### 中優先

5. **命名規則の明文化** — ハイフン区切り / 連結の使い分けを ARCHITECTURE.md に追記する。

6. **Host/WASM 経路のドキュメント化** — 「startup → syscall → Output」パイプラインが不要な環境の統合パスを明記する。

7. **デュアルコア MCU のモデル検討** — 一つのボードが複数の arch を持つ場合の表現方法を定義する。