# umiport アーキテクチャ設計（最終版）レビュー

**対象**: `ARCHITECTURE_FINAL.md`
**レビュー日**: 2026-02-08
**ステータス**: 改訂版レビュー（前回レビュー: ARCHITECTURE-REVIEW.md）

---

## 総合評価

前回レビューで指摘した主要課題がほぼすべて解決されており、設計の完成度は大幅に向上している。

| 観点 | 旧 | 新 | コメント |
|------|:--:|:--:|---------|
| 基本方針 | ◎ | ◎ | 変わらず明確 |
| Cortex-M + newlib での完成度 | ◎ | ◎ | link-time 注入との併存も自然 |
| 非 ARM への拡張性 | △ | ◎ | `write_bytes()` link-time 注入で解決 |
| プラグイン (Host) への拡張性 | △ | ◎ | §13.5 で経路が明示された |
| ボード追加の容易さ | ○ | ◎ | xmake ルール1行で完結 |
| IDE/ツール親和性 | △ | △ | 残存課題（軽微） |
| パッケージ粒度 | △ | ◎ | 7+パッケージ → 合理的な統合 |
| ドキュメントの質 | ◎ | ◎+ | 付録の設計根拠が特に優秀 |
| 外部デバイス対応 | — | ◎ | umidevice の対称構造が美しい |

**前回の最優先指摘3件はすべて解決済み。** 残存課題は軽微なものに限られる。

---

## 1. 解決済みの課題

### 1.1 `_write()` syscall 依存

`rt::detail::write_bytes()` への移行と syscall の一実装格下げ（§4）。Cortex-M 互換性を壊さずに WASM/Host/RISC-V への道を開く理想的な設計。

### 1.2 `umiport` の責務曖昧

`arm/cortex-m/`, `mcu/`, `board/`, `src/` の4サブディレクトリによる内部責務分離は、パッケージ数を増やさずに明確さを確保する良い落とし所。

### 1.3 `umiport-boards` 単一パッケージ問題

umiport への統合で cross-package 参照トリックが消滅。xmake ルールとの組み合わせで使用側のボイラープレートもほぼゼロに。

### 1.4 パッケージ粒度

arch → stm32 → stm32f4 → boards の4段チェーンが消え、umiport 単体に集約。「単独で意味を持つか」という判定基準が付録 A.3 で明文化されている。

### 1.5 命名規則

§12 で `umi<name>`（HW 非依存）vs `umiport`/`umidevice`（Hardware Driver）の区分が明確に。

---

## 2. 特に優れている点

### 2.1 umiport / umidevice の対称構造

§3 の「Hardware Driver Layer の対称構造」テーブルは設計の核心を端的に表現している。MCU 依存性の有無という一点でパッケージ境界を切る判断基準は明快かつ堅牢。

「umidevice は umiport を知らない。umiport は umidevice を知らない。両者が board/platform.hh で初めて出会う」というフレーズは設計意図を完璧に伝えている。

### 2.2 Concept の階層化

`CodecBasic → CodecWithVolume → AudioCodec` の段階的 Concept 設計は、AK4556（パッシブ）から CS43L22（フル機能）までの現実のデバイス差異を正確に反映している。「NOT_SUPPORTED を返す escape hatch は使わない」方針と合わせて、型安全性の高い設計。

### 2.3 付録 A の設計根拠

通常のアーキテクチャ文書にはない「なぜその判断をしたか」の記録が、将来の設計変更時の羅針盤になる。特に A.1（umiport-boards 廃止理由）と A.2（umidevice 独立理由）は、同じ問題に直面した開発者への明確な回答。

### 2.4 xmake ルール設計

§9 のルールベースボード選択により、使用側が1行で完結する。旧設計の手動パス組み合わせボイラープレートの完全な解消。

---

## 3. 残存課題と改善提案

### 3.1 同名ヘッダと IDE 親和性（軽微・前回から残存）

同名ヘッダ＋includedirs 戦略は維持されており、clangd との摩擦は残る。ただし umiport 統合により同名ファイルの散在は減っているので、実害は以前より軽減。

**対策案**: `.clangd` または `compile_commands.json` でアクティブボードの includedirs を明示する。xmake の `compile_commands` エクスポートで自動化できれば開発者が手動管理する必要がなくなる。文書に「推奨 IDE 設定」セクションを追加する程度で十分。

### 3.2 umiport 内の MCU 間ヘッダ可視性（軽微）

includedirs に `umiport/include/umiport/` が入ると `mcu/stm32h7/` のヘッダも見えてしまう。実害は小さい（コンパイルされない限り問題ない）が、誤って `#include <umiport/mcu/stm32h7/rcc.hh>` を STM32F4 ボードで書いてしまう事故は起こりうる。

**対策案**: xmake 側で includedirs を `umiport/include/umiport/mcu/stm32f4/` のように MCU 固有パスだけ公開する。ルール内で制御可能なので、§9.1 に一言追記するだけで足りる。

### 3.3 `write_bytes()` のリンクエラー診断性（軽微）

link-time 注入は強力だが、`write_bytes()` の定義を忘れた場合のリンクエラーが初心者には不親切（`undefined reference to rt::detail::write_bytes` のみ表示）。

**対策案**: 以下のいずれか。

- `write_bytes()` の宣言に `[[gnu::weak]]` でデフォルト実装（`static_assert(false, "...")` やトラップ）を配置
- リンカスクリプトで `ASSERT(DEFINED(write_bytes), "Board must provide rt::detail::write_bytes()")` のようなメッセージを出力

§4.2 に「診断性」の一項を追加する程度で対処可能。

### 3.4 デュアルコア MCU のモデル（前回から未定義・将来課題）

STM32H755（CM7 + CM4）のような MCU の扱いが依然として未定義。`umiport/include/umiport/arm/cortex-m/` は単一コア前提に見える。

現時点で対応 MCU がないなら「将来課題」として明記するだけで十分。必要時に `board/stm32h755-<name>/platform.hh` 内で両コアの型を定義する拡張が自然。

**提案**: §13 の拡張ガイドに「13.6 デュアルコア MCU」を一項追加し、方針だけ示す。

### 3.5 Transport 型注入の具体例が不足

§6 で「全ドライバが umimmio Transport テンプレートで通信バスを注入される」とあるが、Transport テンプレートの具体的なシグネチャが文書中にない。`I2cTransport` が何を満たすべきかの Concept 定義がないと、デバイスドライバ実装者が困る。

**提案**: §5 に `I2cTransport` / `SpiTransport` の Concept を追加する。

```cpp
namespace umihal {

template <typename T>
concept I2cTransport = requires(T& t, uint8_t addr, uint8_t reg,
                                std::span<const uint8_t> tx,
                                std::span<uint8_t> rx) {
    { t.write(addr, reg, tx) } -> std::same_as<Result<void>>;
    { t.read(addr, reg, rx) } -> std::same_as<Result<void>>;
};

template <typename T>
concept SpiTransport = requires(T& t,
                                std::span<const uint8_t> tx,
                                std::span<uint8_t> rx) {
    { t.transfer(tx, rx) } -> std::same_as<Result<void>>;
    { t.select() } -> std::same_as<void>;
    { t.deselect() } -> std::same_as<void>;
};

} // namespace umihal
```

### 3.6 エラー型 `Result<T>` が未定義

§5.2 で `Result<void>`, `Result<std::uint8_t>` が使われているが、`Result` 型自体の定義場所と設計が文書にない。

**提案**: §5 の冒頭に `Result<T>` の定義を追加し、所属パッケージを明記する。C++23 との整合性から `std::expected<T, ErrorCode>` ベースが妥当。

```cpp
// umihal/include/umihal/result.hh
#pragma once
#include <expected>
#include <cstdint>

namespace umihal {

enum class ErrorCode : uint8_t {
    ok = 0,
    timeout,
    nack,
    bus_error,
    overrun,
};

template <typename T>
using Result = std::expected<T, ErrorCode>;

} // namespace umihal
```

---

## 4. 最終判定

この設計は「最もクリーンで美しい構造か」という問いに対して、**ほぼ Yes** と言える。

前回の構造的問題がすべて解消され、パッケージ境界の判定基準（「単独で意味を持つか」「知識の帰属先が異なるか」）が明確に言語化されている。umiport/umidevice の対称構造と board 層での統合という設計パターンは、組み込みの HAL 設計として教科書的な美しさがある。

残存課題は IDE 設定、リンクエラー診断、Transport Concept の明文化など、いずれも設計の骨格を変えるものではなく、文書の補足レベルで解決できるものに限られる。

### 残存課題の優先度

| # | 課題 | 優先度 | 対処レベル |
|---|------|:------:|-----------|
| 3.5 | Transport Concept の明文化 | 高 | §5 に追記 |
| 3.6 | `Result<T>` の定義と所属 | 高 | §5 に追記 |
| 3.1 | IDE 設定ガイド | 中 | 新セクション追加 |
| 3.2 | MCU 間ヘッダ可視性 | 低 | §9.1 に一言追記 |
| 3.3 | リンクエラー診断性 | 低 | §4.2 に一項追加 |
| 3.4 | デュアルコア MCU | 低 | §13 に将来課題として追記 |