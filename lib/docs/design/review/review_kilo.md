# umiport ARCHITECTURE.md レビュー (Kilo Code)

**レビュー日:** 2026-02-08  
**対象:** `lib/umiport/docs/design/ARCHITECTURE.md`  
**手法:** ドキュメントと実コードベースの照合検証、拡張性分析、構造評価

---

## 総合評価: ★★★★☆（優秀だが改善の余地あり）

ARCHITECTURE.md は「ソースにハード依存を漏らさない」という明確な哲学のもと、同名ヘッダ＋xmake includedirs による実装切り替え、Conceptベースの型制約、ライブラリのHW非依存化など、組み込みC++のベストプラクティスを体現した設計です。

ただし、ドキュメントと実装の乖離、パッケージ粒度の過剰分割、および非ARM環境への拡張性に課題があります。

---

## 検証結果: 現在の問題点

### 1. ドキュメントと実装の乖離（深刻度: 高）

| 項目 | ARCHITECTURE.md | 実際の実装 | 乖離度 |
|------|----------------|-----------|--------|
| `umiport-arch` | 存在すべき | **未作成** | 高 |
| `umiport-stm32` | 存在すべき | **未作成** | 高 |
| `umiport-stm32f4` | 存在すべき | **未作成** | 高 |
| `uart_output.hh` | `umiport-stm32f4` に配置 | `umiport/include/umiport/stm32f4/` に混在 | 高 |

現在の `umiport` は「MCU固有コード禁止」としながら `umiport/include/umiport/stm32f4/uart_output.hh` を含み、設計原則と矛盾しています。

### 2. パッケージ粒度の過剰分割（将来リスク）

現在の計画では MCU 1シリーズにつき 1パッケージ（`umiport-stm32f4`, `umiport-stm32h7`...）。10 MCU で 10+ パッケージになり、依存管理が複雑化します。

```
umihal → umiport-arch → umiport-stm32 → umiport-stm32f4 → umiport-boards
```

4段階の依存チェーンは、実際に共有されるコードが少ない割に複雑すぎます。

### 3. `_write()` syscall 依存の固定化

出力経路が newlib の `_write()` syscall に固定されており、以下の環境で問題が生じます：
- **WASM**: syscall が存在しない
- **ESP-IDF**: `esp_log` システムと衝突
- **ホストプラグイン**: stdout 直書きが自然

### 4. 同名ヘッダ戦略のIDE親和性問題

`#include "platform.hh"` が includedirs で解決される設計は：
- **clangd が混乱**: 正しいヘッダを見つけられない
- **ビルドエラーが暗号的**: 間違ったヘッダが選ばれても型エラーまで気づけない
- **コード追跡困難**: grep/検索で曖昧

---

## 拡張性評価

| シナリオ | 評価 | コメント |
|---------|------|---------|
| **同MCU内の新ボード追加** | ★★★★★ | `platform.hh` のみ作成すれば完了、完璧 |
| **新MCUシリーズ追加** | ★★☆☆☆ | 5つのパッケージを同時変更必要、パッケージ爆発リスク |
| **非ARMアーキテクチャ** | ★★☆☆☆ | `_write()` 前提、startup構造がARM前提 |
| **WASM/ホスト** | ★★★★☆ | `umirtm`/`umibench`/`umimmio` は独立して動作 |

---

## 改善提案（優先度順）

### 🔴 最優先: パッケージ構造の再設計

**問題**: `umiport` にMCU固有コード（`stm32f4/uart_output.hh`, `src/stm32f4/startup.cc`）が混在

**提案**: `umiport` を3層に整理し、MCU固有コードを明確なディレクトリに収める

```
lib/umiport/
├── include/umiport/
│   ├── arm/cortex-m/          # アーキ共通（現状通り）
│   ├── mcu/stm32f4/           # MCU固有レジスタ操作
│   └── mcu/stm32h7/           # 将来のMCU
├── src/
│   ├── stm32f4/               # startup/syscalls/linker
│   └── stm32h7/
└── xmake.lua                  # 条件付きでsrc/のサブセットを選択
```

**利点:**
- パッケージ数が爆発しない（umiport 1つに集約）
- MCU固有コードの場所が明確（`mcu/` サブディレクトリ）
- xmake の `add_files` / `add_includedirs` で必要な部分だけ選択

### 🔴 最優先: 出力経路の抽象化

**問題**: `_write()` syscall 固定が非ARM環境で機能しない

**提案**: link-time注入パターンに変更

```cpp
// umirtm は write_bytes の存在だけを要求
namespace rt::detail {
    extern void write_bytes(std::span<const std::byte> data);
}
```

各ボードの `platform.cc` がこのシンボルを定義。syscall経由実装はCortex-M向けの**一実装**に格下げ。

**効果:**
- newlib 非依存化
- WASM / host / RTOS 対応

### 🟡 高優先: Conceptの必須/オプション分離

**問題**: `Uart` concept が10+のrequired expressionを持ち、「NOT_SUPPORTEDを返してもよい」で太っている

**提案**: 最小限の必須Conceptと拡張Conceptを分離

```cpp
// 最小限の必須 Concept
template <typename T>
concept UartBasic = requires(T& u, const uart::Config& cfg, std::uint8_t byte) {
    { u.init(cfg) } -> std::same_as<Result<void>>;
    { u.write_byte(byte) } -> std::same_as<Result<void>>;
    { u.read_byte() } -> std::same_as<Result<std::uint8_t>>;
};

// 拡張 Concept（必要な場合のみ制約に使用）
template <typename T>
concept UartAsync = UartBasic<T> && requires(T& u, std::span<const std::uint8_t> data, uart::TxCallback cb) {
    { u.write_async(data, cb) } -> std::same_as<Result<void>>;
};

// 使用側で必要な制約だけ要求
template <UartBasic T> void simple_print(T& uart);
template <UartAsync T> void async_transfer(T& uart);
```

**利点:** 最小限の MCU（例: 8bit AVR）でも concept を満たせる。「NOT_SUPPORTED を返す」という escape hatch が不要に。

### 🟡 高優先: ClockTree Conceptの追加

**問題**: クロック設定の抽象化が不足、各ボードで統一的なクロック設定が困難

**提案**: 
```cpp
namespace umihal::concept {
template<typename T>
concept ClockTree = requires {
    { T::init() } -> std::same_as<void>;
    typename T::SysClk;
    typename T::HClk;
    typename T::PClk1;
    typename T::PClk2;
    requires ClockSource<typename T::SysClk>;
    requires ClockSource<typename T::HClk>;
};
}
```

**効果:**
- 各ボードで統一的なクロック設定が可能
- 周辺機器がボード非依存でクロック周波数を取得可能

### 🟢 中優先: 依存グラフの簡素化（3層モデル）

現在7+パッケージを3層に整理：

```
Layer 1: umi-hal        (concepts + HW非依存ライブラリ)
Layer 2: umi-chip-*     (SoC実装: stm32f4, esp32s3, rp2040)
Layer 3: umi-board-*    (ボード統合: platform.hh + board.hh + startup)
```

arch 層を chip 層に吸収する（Cortex-M4 の知識は STM32F4 チップ実装に内包）。パッケージ数削減により依存管理の複雑さとビルド設定の肥大化を抑えられる。

---

## 代替アプローチ

### 1. ビルド時ボード型の明示注入（Zig/Rustスタイル）

includedirs による暗黙切り替えの代わりに、ビルド時に**ボード型を明示的に渡す**アプローチ。

```cpp
// config.hh (xmakeが自動生成)
#include <board/stm32f4-renode/platform.hh>
using CurrentPlatform = umi::board::stm32f4_renode::Platform;
```

すべてのコードが `CurrentPlatform` を型として受け取る。同名ヘッダのマジックが不要になり、型安全性とIDE親和性が向上する。

### 2. Policy-based Design

```cpp
// よりC++らしい、コンパイル時に全て解決
template<typename ClockPolicy, typename OutputPolicy>
struct Platform : ClockPolicy, OutputPolicy {
    static void init() {
        ClockPolicy::init();
        OutputPolicy::init();
    }
};

// ボード定義
using Stm32f4Renode = Platform<
    stm32f4::HseClock<25000000, 168000000>,
    stm32f4::UartOutput<1>
>;

// 型エイリアスではなく、継承による実装再利用
```

**利点:**
- コンパイル時に全て解決（ゼロオーバーヘッド）
- 複数設定の組み合わせが容易
- IDEの補完が効きやすい

**欠点:**
- テンプレートエラーの可読性
- コンパイル時間増加の可能性

---

## 他レビューとの比較

### 共通して指摘されている問題

| 問題 | Opus46 | ClaudeWeb | ChatGPT | Gemini | Kilo |
|------|--------|-----------|---------|--------|------|
| ドキュメントと実装の乖離 | ◎ | ○ | ○ | - | ◎ |
| `_write()` syscall依存 | ◎ | ◎ | ◎ | - | ◎ |
| パッケージ粒度の過剰分割 | ◎ | ○ | ○ | - | ◎ |
| 同名ヘッダのIDE問題 | ○ | ◎ | ◎ | - | ◎ |
| startup/syscallsの配置 | ◎ | ◎ | ◎ | - | ◎ |

### 各レビューの独自の視点

- **Opus46**: パッケージ爆発の防止、`umiport` を3層に整理する具体的提案
- **ClaudeWeb**: CRT層の独立パッケージ化、非ARM対応の設計ガイドライン追加
- **ChatGPT**: 差し替え点を `config.hh` 1つに集約、複数ボード同時ビルドの安定化
- **Gemini**: ディレクトリ構成の再設計、IPブロックごとの定義（バージョン管理）
- **Kilo**: 上記を統合し、優先度付きアクションプランを提示

---

## 結論

### 維持すべき設計原則（非常に優秀）

1. **ライブラリはHWを知らない** - `umirtm`/`umibench`/`umimmio` の0依存設計
2. **同名ヘッダによる実装切り替え** - `#ifdef` 地獄を回避
3. **統合はboard層のみで行う** - 責務分離が明確

### 最優先アクション

1. **ARCHITECTURE.mdを「現状」に合わせて更新** - 存在しないパッケージを削除またはTODOとして明記
2. **`uart_output.hh`の配置を決定** - `mcu/`サブディレクトリに移動または新パッケージ作成
3. **`_write()` syscall依存の解消** - 出力経路の抽象を一段上げる

この設計は**採用価値が非常に高い**ですが、上記の改善により、より堅牢で長期的に安定したポータブル基盤になるでしょう。
