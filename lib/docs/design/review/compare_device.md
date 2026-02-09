# 外部デバイス＋パッケージ分離 レビュー比較分析（統合版）

**作成日:** 2026-02-08（更新: 同日）
**目的:** device レビュー4件 + separation レビュー2件を横断比較し、理想的なパッケージ構成を導出する

---

## 1. レビュー一覧

### 外部デバイスの扱い

| レビュアー | ファイル | 結論 |
|-----------|---------|------|
| **Kimi** | device_kimi.md | 理想は独立、現実は umiport-boards 内に短期配置 |
| **Claude Opus 4.6** | device_claude46.md | 独立パッケージが原理的に正解 |
| **Claude Code** | device_claudecode.md | 独立パッケージ（umiport と同格） |

### umiport / umiport-boards の分離

| レビュアー | ファイル | 結論 |
|-----------|---------|------|
| **Kimi** | separation_kimi.md | 分離は正しいが境界に改善余地あり |
| **Claude Opus 4.6** | umiport_split_claude46.md | 分離の正当化は弱い。統合が自然 |
| **Claude Code** | separation_claudecode.md | 正しくない。統合すべき |

---

## 2. 外部デバイスの扱い：全員一致の点

| 合意事項 | 全3レビュー |
|---------|:-----------:|
| デバイスドライバは MCU 非依存 | ○ |
| Concept レベルでは MCU ペリフェラルと同格 | ○ |
| 結合はボード層で行う | ○ |
| umiport/mcu/ に直接入れるのは不適切 | ○ |
| カテゴリ分類（audio/, display/, sensor/ 等）を導入すべき | ○ |

### 対立点

| 論点 | Kimi | Opus 4.6 | Claude Code |
|------|------|----------|-------------|
| 短期配置先 | umiport-boards 内 | 独立パッケージ | 独立パッケージ |
| 独立化のタイミング | 10種類以上になったら | 最初から | 最初から |
| umibus（バス抽象）の独立化 | 必要 | 不要（umimmio Transport で十分） | 不要 |

### 分析

**Kimi の「量が増えたら分離」は設計原則として誤り。** Opus 4.6 が指摘する通り:

> **正しい場所にあるものは量が増えても移動する必要がない。**

分離の理由は「量」ではなく「知識の帰属先が異なる」こと。IC に帰属する知識をボード層に入れるのは、量が少なくても帰属先の誤りであり、量が増えたときに「やっぱり移動」が必要になる時点で、最初の配置が間違っていたことの証拠。

**結論: umidevice は最初から独立パッケージ。** 2/3 が支持し、Kimi も「理想は独立」と認めている。

---

## 3. umiport / umiport-boards 分離：核心的対立

| 論点 | Kimi | Opus 4.6 | Claude Code |
|------|------|----------|-------------|
| 分離は正しいか | 正しい | 正しくない | 正しくない |
| 根拠 | 依存方向（ボード→MCU）が自然 | 単独で使えないものを分離する意味がない | 不可分なものを割っているだけ |
| 推奨 | 軽微な境界修正 | umiport に統合 | umiport に統合 |

### Kimi の「分離は正しい」の論拠を検証

Kimi は以下を根拠に分離を肯定:

1. **依存方向が自然**（ボード→MCU）
2. **startup.cc はボード統合コード**（platform.hh に依存するから）
3. **同一 MCU の複数ボードで startup/syscalls を共有できる**

しかし:
- 依存方向が自然なことは、**分離すべきことの証明にはならない**。同一パッケージ内のディレクトリ間でも依存方向は維持できる
- startup.cc がボード統合コードだとしても、MCU レジスタ操作と不可分にセットで使われるなら同一パッケージでよい
- 同一 MCU での共有は、`src/stm32f4/` ディレクトリをパッケージ内に持てば同様に実現できる

**Kimi の論拠は「分離しても問題ない」を示しているが「分離すべき」を示していない。** 問題がないだけでは、分離のコスト（2パッケージ管理、xmake ボイラープレート）を正当化できない。

### Opus 4.6 / Claude Code の「統合すべき」の論拠

共通の核心:

> **パッケージ分割は「独立して使える単位」でやるべき。** umiport は umiport-boards なしでは使えず、逆も同様。

具体的な証拠:
- xmake で常に両パッケージのパスを手動で組み合わせている（ボイラープレート）
- startup.cc が `#include "platform.hh"` で cross-package 参照している
- 全ライブラリのテストで同一のパス組み立てパターンが繰り返されている

**結論: umiport と umiport-boards は統合すべき。** 2/3 が統合を支持。Kimi も境界の曖昧さは認めている。

---

## 4. 両論点の統合：理想的なパッケージ構成

外部デバイスの独立性と umiport 統合を組み合わせると:

### 4.1 パッケージ一覧

| パッケージ | 責務 | 依存 | 単独で意味を持つか |
|-----------|------|------|:------------------:|
| **umihal** | Concept 定義 | なし | ○ |
| **umimmio** | レジスタ抽象化 | なし | ○ |
| **umiport** | MCU + アーキ + ボード統合（全ポーティング知識） | umimmio | ○（ボード選択で動作） |
| **umidevice** | 外部デバイスドライバ | umimmio | ○（Mock Transport で動作） |
| **umirtm** | printf/print/Monitor | なし | ○ |
| **umibench** | ベンチマーク | なし | ○ |
| **umitest** | テスト基盤 | なし | ○ |

→ **全パッケージが単独で意味を持つ。** これが正しい分割の証拠。

### 4.2 umiport の内部構造

```
lib/umiport/
├── include/umiport/
│   ├── arm/cortex-m/        # アーキテクチャ共通
│   │   ├── cortex_m_mmio.hh
│   │   └── dwt.hh
│   ├── mcu/                 # MCU 固有レジスタ操作
│   │   ├── stm32f4/
│   │   │   ├── rcc.hh
│   │   │   ├── gpio.hh
│   │   │   └── uart.hh
│   │   └── stm32h7/         # 将来
│   └── board/               # ボード定義（旧 umiport-boards）
│       ├── stm32f4-renode/
│       │   ├── platform.hh
│       │   └── board.hh
│       ├── stm32f4-disco/
│       │   ├── platform.hh
│       │   └── board.hh
│       └── host/
│           └── platform.hh
├── src/
│   └── stm32f4/             # startup / syscalls / linker
│       ├── startup.cc
│       ├── syscalls.cc
│       └── linker.ld
└── xmake.lua
```

### 4.3 理想的な依存関係図

```
                    umihal (0 deps)
               ┌─── Concept 定義 ───┐
               │                     │
         ┌─────┴─────┐      ┌───────┴───────┐
         │  umiport  │      │  umidevice    │
         │           │      │               │
         │ arm/      │      │ audio/        │
         │ mcu/      │      │ display/      │
         │ board/    │      │ sensor/       │
         │ src/      │      │               │
         └─────┬─────┘      └───────┬───────┘
               │                     │
               │    umimmio (0 deps) │
               │    ← 両者が利用 →   │
               │                     │
               └──────────┬──────────┘
                          │
                    platform.hh
               ─── board/ 内で MCU × Device を結合
                          │
                    application

        umirtm (0 deps)  ─── HW 非依存
        umibench (0 deps)
```

**ポイント:**
- umiport 内部の `board/platform.hh` が MCU ドライバとデバイスドライバの統合点
- umiport-boards は廃止、`board/` はumiport 内のサブディレクトリ
- umidevice は独立パッケージとして MCU 非依存を維持

### 4.4 board/platform.hh での統合例

```cpp
// umiport/include/umiport/board/stm32f4-disco/platform.hh

// MCU ドライバ（同一パッケージ内）
#include <umiport/mcu/stm32f4/i2c.hh>
#include <umiport/arm/cortex-m/dwt.hh>

// 外部デバイスドライバ（umidevice パッケージ）
#include <umidevice/audio/cs43l22.hh>

// ボード定数（同一パッケージ内）
#include <umiport/board/stm32f4-disco/board.hh>

namespace umi::port {

struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;
    using Codec  = device::CS43L22Driver<I2cTransport>;

    static void init() { ... }
};

static_assert(umihal::Platform<Platform>);

} // namespace umi::port
```

---

## 5. 独自の視点まとめ

### Kimi（device）の独自価値
- **レイヤー構造図**（アプリ → デバイス → バス → MCU）が階層を明快に視覚化
- **合成 > 継承**（Composition over Inheritance）の設計原則
- **2フェーズ初期化**（コンストラクタで通信しない）
- **エラー型の階層化**（BusError / DeviceError / StateError の分離）
- **umibus 独立パッケージ**の提案（ただし現時点では過剰設計と判断）

### Opus 4.6（device）の独自価値
- **「知識の帰属先」フレームワーク**（アーキ → MCU → IC → ボード）で配置を原理的に導出
- **Transport の本質的差異分析**（メモリマップド vs バス経由が唯一の構造的違い）
- **Concept 対称性**（umihal が MCU Concept と Device Concept の両方を定義する対称構造）

### Kimi（separation）の独自価値
- **startup.cc のボード統合コードとしての性質分析**
- **UartOutput のレジスタ操作 vs 出力ポリシーの分離案**（案B）

### Opus 4.6（separation）の独自価値
- **「単独で意味を持つか」を分離正当性の判定基準として確立**
- **xmake ボイラープレートが「無理な分離のサイン」であることの指摘**

---

## 6. レビュアー評価

| レビュアー | テーマ | 分析深度 | 論理的整合性 | 総合 |
|-----------|--------|---------|------------|------|
| **Kimi** | device | ★★★★★ | ★★★★☆ | A |
| **Kimi** | separation | ★★★★☆ | ★★★☆☆ | B+ |
| **Opus 4.6** | device | ★★★★★ | ★★★★★ | A+ |
| **Opus 4.6** | separation | ★★★★★ | ★★★★★ | A+ |
| **Claude Code** | device | ★★★★☆ | ★★★★★ | A |
| **Claude Code** | separation | ★★★★☆ | ★★★★★ | A |

**Kimi device レビューの特筆:** 「理想的なデバイスインターフェース」の設計（BME280 例、状態管理、エラー処理）は最も具体的かつ実用的。ドライバの API 設計ガイドとして高い価値を持つ。

**Kimi separation レビューの弱点:** 「依存方向が自然」を「分離が正しい」の根拠としているが、パッケージ内ディレクトリでも同じ依存方向は維持できるため、分離の必然性を示せていない。

---

## 7. 最終結論

### 採用する方針

| 決定 | 根拠 | 支持 |
|------|------|------|
| **umiport と umiport-boards を統合** | 単独で意味を持たないものを分離する必然性がない | Opus 4.6 + Claude Code (2/3) |
| **umidevice は独立パッケージ** | IC に帰属する知識は MCU にもボードにも属さない | 全員 (3/3) |
| **umiport 内部は `arm/` `mcu/` `board/` `src/` で構造化** | ディレクトリで責務分離を維持 | Opus 4.6 + Claude Code |
| **platform.hh が MCU × Device の唯一の統合点** | ボード層の責務を明確化 | 全員 |
| **カテゴリ分類を umidevice に導入** | 将来のスケーラビリティ | 全員 |

### 不採用とする方針

| 不採用 | 理由 |
|--------|------|
| umiport-boards の維持 | 分離のコストに対して利点がない |
| umidevice の umiport 統合 | MCU 非依存性を維持するため |
| umidevice の umiport-boards 統合 | IC 知識をボード層に入れる帰属先の誤り |
| umibus 独立パッケージ | 現時点で過剰設計。umimmio Transport で十分 |
