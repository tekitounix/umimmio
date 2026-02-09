# umiport アーキテクチャレビュー v2

最終版アーキテクチャの再評価と追加改善提案をまとめる。

---

# 総評

本改訂版は前回指摘された主要課題の大部分を解消しており、長期運用可能な構造に到達している。

特に以下の改善が大きい。

* umiport 単一パッケージ化
* umidevice の独立
* 出力の link-time 注入
* Platform::Timer 統合
* Concept の Basic / Extended 分離

設計成熟度は高い。

ただし長期運用で確実に問題化する論点がいくつか残っているため、本書で明文化する。

---

# 改善された点の評価

## 1. パッケージ境界が明確

umiport と umidevice の境界が MCU 依存性で固定された。

| 層         | 対象           |
| --------- | ------------ |
| umiport   | MCU 内蔵ペリフェラル |
| umidevice | 外部 IC        |

知識の帰属先が明確であり、拡張時の混乱が起きにくい。

---

## 2. 出力経路が newlib 依存から脱却

`rt::detail::write_bytes()` を必須シンボル化したことで

* WASM
* ホスト
* 非 newlib 環境

への拡張が可能になった。

---

## 3. Timer 二重定義問題の解消

Platform に Timer を統合したことで

* ボード追加時の作業削減
* 実装漏れ防止

が実現された。

---

## 4. xmake ルールの導入

ボード選択のボイラープレートが消え、
運用コストが大幅に削減された。

---

# 残るリスクと課題

## 1. umiport 単一パッケージ化の副作用

パッケージ爆発は回避されたが、
ヘッダ依存の密度が増加した。

### 想定される問題

* MCU ヘッダの巨大化
* 意図しない MCU ヘッダの混入
* ビルド時間の増加

### 推奨ルール

* MCU ヘッダは薄いフロントと詳細ヘッダに分割
* platform.hh の include を最小化
* MCU 集約ヘッダを禁止

---

## 2. board パス include とボード非依存の衝突

現在の例ではアプリが board パスを include 可能。

これは「ボード非依存」と矛盾し得る。

### 層の整理

| 層        | board.hh include | 許可 |
| -------- | ---------------- | -- |
| ライブラリ    | 禁止               | ×  |
| 共通アプリ    | 禁止               | ×  |
| ボード専用アプリ | 許可               | ○  |

### 推奨

共通アプリが必要な情報は Platform 経由で公開する。

---

## 3. platform.hh 肥大化リスク

統合点は必ず肥大化する。

### ルール提案

* platform.hh は型定義中心
* 重い初期化は cpp へ移動
* 必要なら board/platform.cc を許可

---

## 4. Result と Error 設計の未確定

Concept で Result が使用されているが、
エラー体系が未確定。

### 決定すべき事項

* Result 型の定義
* エラーの粒度
* Transport エラーと Device エラーの合成
* init 失敗の契約

これは HAL の中心設計要素であり、早期決定が必要。

---

## 5. startup / syscalls / linker の拡張性

現状は MCU 共有設計。

将来の変化

* ブートローダ併用
* libc 差異
* メモリ配置差異

### 推奨

* デフォルト実装として維持
* board での上書きを許可

---

# 追加改善提案

## 1. Platform 契約の強化

現在

* Output
* Timer

将来必要

* ClockTree
* Pins
* MemoryMap
* early_init / init 二段階初期化

---

## 2. Transport Concept を umihal に追加

umidevice 再利用性向上のため。

候補

* I2cTransport
* SpiTransport
* RegIo

---

## 3. CI 規約チェック導入

自動検出ルール例。

* platform.hh 以外で umiport include 禁止
* platform.hh 以外で umidevice include 禁止
* #ifdef 禁止
* mcu → board include 禁止
* umidevice → mcu include 禁止

---

# 結論

最終版設計は非常に完成度が高い。

残る重要課題は以下。

1. 層ルールの明文化
2. Result / Error 設計確定
3. Platform 契約の拡張
4. 規約チェックの自動化

これらが整えば、長期的に安定したポータブル基盤となる。
