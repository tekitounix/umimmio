# umiport / umiport-boards 分離の妥当性検証 (Claude Code)

**レビュー日:** 2026-02-08
**論点:** umiport と umiport-boards の分離は正しい設計か？

---

## 結論: 正しくない。統合すべき。

---

## 1. 分離で得られているもの

**ほぼゼロ。** 理由:

1. **umiport-boards は umiport なしでは動かない** — platform.hh が `umiport/mcu/stm32f4/uart.hh` をインクルードする
2. **umiport は umiport-boards なしでは使えない** — startup.cc が `#include "platform.hh"` で boards 側を参照する
3. **独立してビルド/テストできない** — 片方だけでは意味をなさない
4. **利用者は常に両方を指定する** — xmake で両パッケージのパスを手動で毎回組み合わせる

相互に不可分なものを2つに割っている。これは分離ではなくただの分散。

## 2. 正当な分離との対比

| パッケージ | 単独で意味を持つか | 分離は正当か |
|-----------|:------------------:|:------------:|
| umimmio | ○ ホストテスト可能 | ○ |
| umirtm | ○ 完全独立 | ○ |
| umidevice | ○ MCU を変えても成立 | ○ |
| **umiport** | ✗ 常に boards とセット | **✗** |
| **umiport-boards** | ✗ 常に umiport とセット | **✗** |

パッケージ分割は**独立して使える単位**でやるべき。不可分なものを割るのはコストだけ増やす。

## 3. 統合した場合の構造

```
lib/umiport/
├── include/umiport/
│   ├── arm/cortex-m/          # アーキテクチャ共通
│   ├── mcu/stm32f4/           # MCU レジスタ操作
│   └── board/                 # ボード定義（旧 umiport-boards）
│       ├── stm32f4-renode/
│       │   ├── platform.hh
│       │   └── board.hh
│       └── stm32f4-disco/
└── src/
    └── stm32f4/               # startup, syscalls, linker
```

これは旧 ARCHITECTURE.md → ARCHITECTURE_FINAL.md で「umiport-arch, umiport-stm32, umiport-stm32f4 を分けない」と決めたのと同じ論理。

## 4. 利点

- startup.cc と platform.hh が同一パッケージ内 → cross-package 参照トリック不要
- xmake のボイラープレートが消える
- 「ポーティングに必要なもの全部」が一箇所にある
- ボード追加は `board/` にディレクトリを足すだけ
- MCU 追加は `mcu/` と `src/` と `board/` に足す（同一パッケージ内で完結）

## 5. 懸念と反論

| 懸念 | 反論 |
|------|------|
| umiport が大きくなる | ヘッダオンリー。使わないボードのヘッダは存在しないのと同じ |
| MCU コードとボードコードが混ざる | ディレクトリで分離（`mcu/` vs `board/`） |
| 全ボードの依存を引く | ヘッダオンリーなので `#include` しなければ影響なし |
