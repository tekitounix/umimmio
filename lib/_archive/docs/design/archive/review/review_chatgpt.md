# umiport アーキテクチャレビュー

## 概要

本ドキュメントは umiport アーキテクチャ設計の妥当性、拡張性、長期保守性を評価し、改善提案を整理したものである。

---

# 総合評価

設計思想は明確であり、特に次の点は非常に強い。

* ソースからハード依存を排除する方針
* board 層のみで統合する構造
* ライブラリを HW 非依存に保つ徹底
* 同一ソースを複数ボードでビルド可能

小規模から中規模までは非常にクリーンに運用可能な設計である。

一方で、長期拡張時に問題化する可能性のある箇所がいくつか存在する。

---

# 強み

## 1. ハード依存排除ポリシーが一貫している

禁止事項が明確であり、CI による静的検査が可能。

* #ifdef 禁止
* フルパス include 禁止
* board 層のみで統合

これは長期保守性に大きく寄与する。

---

## 2. Board 層による統合は適切

MCU 実装と Board 実装の責務分離が明確。

| 層     | 責務     |
| ----- | ------ |
| MCU   | レジスタ操作 |
| Board | 構成と接続  |

この分離は将来の MCU 追加時に非常に有利。

---

## 3. 出力経路の一本化

syscall 経由の printf 経路は実用的で、次が実現できている。

* UART / RTT / host stdout の切替
* ライブラリが出力先を知らない

---

# 将来問題になり得る点

## 1. 同名ヘッダ差し替えのスケール問題

現在の最大リスク。

### 起こり得る問題

* include 解決順序の事故
* IDE と実ビルドの差異
* 複数ボード同時ビルドの不安定化
* include path の複雑化

プロジェクト規模が大きくなるほど破綻確率が上がる。

---

## 2. platform.hh の肥大化

将来的に platform.hh に次が集約されやすい。

* クロック設定
* 割り込み優先度
* DMA 設定
* キャッシュ設定
* ワークアラウンド

結果として

* ビルド時間増大
* 依存の肥大化
* 初期化順序の不透明化

が起こる。

---

## 3. startup / syscalls の配置問題

現状は SoC 配下に存在するが、実際は次の性質を持つ。

* toolchain 依存
* libc 依存
* boot 構成依存
* RTOS 導入で変化

SoC 層に置くと将来整理が崩れる可能性が高い。

---

## 4. HAL Concept の意味論不足

型制約のみでは不十分。

将来必要になる項目。

* blocking / non‑blocking
* IRQ safe
* DMA 併用前提
* init ライフサイクル
* エラー表現

仕様の最小契約が必要。

---

## 5. newlib syscall 依存の固定化

現在の設計は newlib 前提。

将来の対象。

* freestanding 環境
* WASM
* Windows host
* RTOS 環境

出力経路の抽象が不足している。

---

# 拡張性評価

| 項目           | 評価  |
| ------------ | --- |
| MCU 追加       | 良好  |
| Board 追加     | 良好  |
| toolchain 切替 | 要改善 |
| libc 切替      | 要改善 |
| 複数ボード同時ビルド   | 要改善 |

---

# 改善提案

## 改善1: 差し替え点を config.hh 1つに集約

### 現状

多数の同名ヘッダを差し替え。

### 提案

差し替え対象を 1 ファイルのみに縮約。

```cpp
// umiport/config.hh
#pragma once
#include <board/stm32f4-disco/platform.hh>
namespace umi::port {
using ActivePlatform = Platform;
}
```

### 効果

* include 解決事故の大幅減少
* IDE とビルドの整合
* 複数ターゲット同時ビルド安定化

---

## 改善2: 出力経路を sink 差し替え可能に

syscall 固定依存を緩和。

### 追加する抽象

```cpp
namespace rt::detail {
void write_bytes(std::span<const std::byte>);
}
```

デフォルト実装は weak symbol で提供し、board 側で override 可能にする。

### 効果

* newlib 非依存化
* WASM / host / RTOS 対応

---

## 改善3: CRT 層を独立パッケージ化

startup / linker / syscalls を分離。

例

* umiport-crt-newlib-cm4
* umiport-crt-newlib-cm7
* umiport-crt-host
* umiport-crt-wasi

### 効果

* toolchain 切替が容易
* boot 構成の拡張が容易

---

## 改善4: board.hh を定数専用に制限

| ファイル        | 内容    |
| ----------- | ----- |
| board.hh    | 定数のみ  |
| platform.hh | 宣言のみ  |
| platform.cc | 初期化実装 |

### 効果

* ビルド時間削減
* 初期化順序の明確化

---

## 改善5: SoC 実装の二層化

将来の driver バリエーション増加に備える。

層分割。

1 レジスタ層
2 ドライバ層

---

# 代替アプローチ

## Link-time 差し替え

weak symbol による platform 実装差し替え。

### 利点

* include 依存減少
* IDE 整合性向上
* 同時ビルド安定

---

## config 生成方式

xmake が config.hh を生成。

### 利点

* board 選択の一元化
* CI 安定化

---

# 結論

現行設計は方向性として非常に優れている。

特に次の原則は維持すべき。

* ライブラリは HW を知らない
* 統合は board のみ

長期安定性の観点では次の改善が重要。

1. 差し替え点の一元化
2. 出力経路の抽象化
3. CRT 層の分離

これらを導入することで、より長期的に安定したポータブル基盤になる。
