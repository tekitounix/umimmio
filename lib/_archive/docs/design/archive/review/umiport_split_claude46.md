# umiport / umiport-boards 分離の妥当性検証 (Claude Opus 4.6)

**レビュー日:** 2026-02-08
**論点:** umiport と umiport-boards の分離は正しい設計か？

---

## 結論: 分離の正当化は弱い。統合が自然。

---

## 1. 現状の事実

| | umiport | umiport-boards |
|---|---|---|
| 種類 | headeronly | headeronly |
| 内容 | MCU レジスタ定義 + アーキ共通コード | board 定義（platform.hh, board.hh） |
| 依存関係 | umimmio のみ | なし（宣言上） |
| 実際の使われ方 | **常に umiport-boards と一緒に使う** | **常に umiport と一緒に使う** |

startup.cc / syscalls.cc は `umiport/src/stm32f4/` にあるが `#include "platform.hh"` で umiport-boards のヘッダを参照している。xmake のターゲット定義では両パッケージのパスを手動で結合している。

---

## 2. 分離がおかしい理由

### 2.1 知識の帰属先で考えると同一カテゴリ

device レビューで確立した原理を適用する:

| 知識 | 帰属先 |
|------|--------|
| STM32F4 USART レジスタ | MCU（チップ設計） |
| Cortex-M DWT | アーキテクチャ（コア設計） |
| 「Discovery ボードは RTT で出力」 | ボード（基板設計） |
| 「STM32F4 の startup シーケンス」 | MCU（チップ設計） |

これらはすべて **「ハードウェアにポーティングするための知識」** という一つのカテゴリに属する。MCU 知識もボード知識も「ポーティング」の文脈では不可分。umidevice が「IC に帰属する独立した知識」だったのとは対照的に、ボード知識は MCU 知識と常にセットで使われる。

### 2.2 実際に単独利用されることがない

umiport だけ使って umiport-boards を使わないケースは存在しない（逆も同様）。「単独で意味を持つか」は分離の正当性の強い指標であり、両者はこの基準を満たさない。

対照的に:
- umimmio は umiport なしで使える（ホストテスト等）
- umirtm は完全に独立
- umidevice も MCU を変えても単独で成立する

これらの分離は正当。

### 2.3 startup.cc の位置が矛盾を生んでいる

startup.cc は MCU 知識（ベクタテーブル、FPU 初期化）を持つが、`#include "platform.hh"` でボード知識を参照する。現在 umiport に置いてあるが、ARCHITECTURE_FINAL.md では umiport-boards への移動を計画している。どちらに置いても「パッケージ境界をまたぐ参照」が発生する。**これは分離する必要がないものを分離しているサイン。**

### 2.4 xmake の実態が分離を否定している

```lua
-- 全ターゲットでこのパターンが繰り返される
local umiport_stm32f4 = path.join(os.scriptdir(), "../../umiport/src/stm32f4")
local board_stm32f4_disco = path.join(os.scriptdir(), "../../umiport-boards/include/board/stm32f4-disco")

add_files(path.join(umiport_stm32f4, "startup.cc"))
add_files(path.join(umiport_stm32f4, "syscalls.cc"))
add_includedirs(board_stm32f4_disco, {public = false})
```

2つのパッケージのパスを手動で毎回組み合わせている。これは「本来一つであるものを無理に分離した」ときに現れるボイラープレート。

---

## 3. 統合した場合の構造

```
lib/umiport/
├── include/umiport/
│   ├── arm/cortex-m/          # アーキテクチャ共通
│   │   ├── cortex_m_mmio.hh
│   │   └── dwt.hh
│   ├── mcu/stm32f4/           # MCU 固有レジスタ
│   │   └── uart_output.hh
│   └── board/                 # ボード定義 ← 旧 umiport-boards
│       ├── stm32f4-renode/
│       │   ├── platform.hh
│       │   └── board.hh
│       └── stm32f4-disco/
│           ├── platform.hh
│           └── board.hh
├── src/
│   └── stm32f4/               # startup / syscalls / linker
│       ├── startup.cc
│       ├── syscalls.cc
│       └── linker.ld
└── xmake.lua
```

### 利点

- startup.cc と platform.hh が同一パッケージ内 → `#include "platform.hh"` の cross-package トリックが不要
- xmake のボイラープレートが消える
- 「ポーティングに必要なもの全部」が一箇所にある
- ボード追加時は `board/` にディレクトリを足すだけ（既存ファイルの変更なし）
- MCU 追加時は `mcu/` と `src/` と `board/` に足す（すべて同一パッケージ内で完結）

### 懸念と反論

| 懸念 | 反論 |
|------|------|
| umiport が大きくなる | ヘッダオンリー。使わないボードのヘッダは `#include` しなければ存在しないのと同じ |
| MCU コードとボードコードが混ざる | ディレクトリで明確に分離されている（`mcu/` vs `board/`） |
| 単体リポジトリとして使えなくなる | 元々 umiport-boards は standalone 対応していない |

---

## 4. 分離の正当性を持つパッケージとの対比

| パッケージ | 単独で意味があるか | 知識の帰属先が異なるか | 分離は正当か |
|-----------|:------------------:|:---------------------:|:------------:|
| umihal | ○ | ○（契約のみ） | ○ |
| umimmio | ○ | ○（汎用基盤） | ○ |
| umirtm | ○ | ○（HW 非依存） | ○ |
| umibench | ○ | ○（HW 非依存） | ○ |
| umidevice | ○ | ○（IC に帰属） | ○ |
| umitest | ○ | ○（テスト基盤） | ○ |
| **umiport** | ✗（常にボード定義とセット） | ✗（同じ「ポーティング」カテゴリ） | **✗** |
| **umiport-boards** | ✗（常に umiport とセット） | ✗（同上） | **✗** |

分離の正当性は「単独で意味を持つか」と「知識の帰属先が異なるか」の2軸で判断すべきであり、umiport / umiport-boards はどちらの軸でも分離を正当化できない。

---

## 5. まとめ

| 問い | 答え |
|------|------|
| 分離は正しいか | No。両者は常にセットで使われ、知識の帰属先も同一カテゴリ |
| なぜ分離されたのか | おそらく「MCU 知識」と「ボード知識」は異なるという直感から。だが「ポーティング」という文脈では不可分 |
| 統合の副作用 | なし。ディレクトリ構造で内部的な分離は維持される |
| パッケージ数 | umiport-boards を廃止し umiport に吸収。トップレベルのパッケージが1つ減る |
