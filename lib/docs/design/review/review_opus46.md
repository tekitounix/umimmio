# umiport ARCHITECTURE.md レビュー (Claude Opus 4.6)

**レビュー日:** 2025-02-08
**対象:** `lib/umiport/docs/design/ARCHITECTURE.md`
**手法:** ドキュメントと実コードベースの照合検証、拡張性分析、構造評価

---

## 総合評価: ★★★★☆（優秀、ただし構造的な問題あり）

基本方針「ソースにハード依存を漏らさない」は C++ embedded の世界で極めて正しいアプローチ。`#ifdef` 排除、同名ヘッダによる切り替え、Concept ベースの型制約は、vtable なしで型安全な多ターゲット展開を実現しており、設計の核は非常に美しい。

---

## A. 検出された問題点

### 1. ドキュメントと現実の重大な乖離

| 項目 | ARCHITECTURE.md | 現実 | 深刻度 |
|------|----------------|------|--------|
| `umiport-arch` | 存在すべき | **未作成** | 高 |
| `umiport-stm32` | 存在すべき | **未作成** | 高 |
| `umiport-stm32f4` | 存在すべき | **未作成** | 高 |
| `umiport-stm32h7` | 存在すべき | **未作成** | 中 |
| `uart_output.hh` | `umiport-stm32f4` に配置 | `umiport/include/umiport/stm32f4/` に混在 | 高 |
| umihal の `concept/` サブディレクトリ | `umihal/include/umihal/concept/` | フラット配置（`concept/` なし） | 低 |
| umihal のファイル数 | 4（gpio, uart, timer, dma） | **12ファイル**（arch, audio, board, codec 等追加） | 中 |
| `dma.hh` | 計画に記載 | **未作成** | 低 |

**判定:** ドキュメントが「あるべき未来の姿」と「現在の姿」を混在して記述しており、どちらが正かわからない状態。今後のメンテナンスで致命的。

### 2. umiport の責務過多（設計原則違反）

ARCHITECTURE.md 自身が「umiport は MCU 固有コード禁止」と明記しているのに、現実は：

- `umiport/include/umiport/stm32f4/uart_output.hh` — STM32F4 の USART レジスタ直接操作
- `umiport/src/stm32f4/startup.cc` — STM32F4 固有の起動コード
- `umiport/src/stm32f4/syscalls.cc` — platform.hh 経由だが stm32f4 ディレクトリ内

startup.cc / syscalls.cc は「同名ヘッダ経由で抽象化されている」ため許容できる面もあるが、**uart_output.hh は明確な違反**。

### 3. startup/syscalls の MCU 拡張パス不明

現在 `umiport/src/stm32f4/` にしか startup/syscalls がない。STM32H7 や RP2040 追加時：

- `umiport/src/stm32h7/startup.cc` を作る？ → umiport が MCU 固有コードの倉庫になる
- 別パッケージに分ける？ → ARCHITECTURE.md に方針なし

**これが最大の構造リスク。** 新 MCU 追加のたびに umiport が肥大化する。

---

## B. 拡張性の評価

### 新ボード追加（同じ MCU）: ★★★★★

例: STM32F4 の新しいカスタムボードを追加する場合

1. `umiport-boards/include/board/my-custom-board/platform.hh` を作成
2. xmake で `add_includedirs` を変更

→ **完璧。ソース変更ゼロ。**

### 新 MCU シリーズ追加: ★★☆☆☆

例: STM32H7 追加の場合、ARCHITECTURE.md の計画通りなら：

1. `umiport-stm32h7/` パッケージを新規作成
2. `umiport-arch/` に CM7 固有コード追加
3. `umiport-stm32/` に共通コード追加
4. `umiport-boards/include/board/daisy_seed/` にボード定義
5. `umiport/src/stm32h7/` に startup/syscalls/linker.ld を追加

→ パッケージが **5つ同時に変更対象** になり、さらに存在しないパッケージを3つ新規作成する必要がある。

### 非 ARM アーキテクチャ追加: ★★☆☆☆

例: ESP32 (Xtensa) や RISC-V 追加の場合：

- newlib syscall ベースの出力経路が前提 → ESP-IDF では `_write()` の仕組みが全く異なる
- `startup.cc` が Cortex-M 固有（ベクタテーブル、CPACR、FPU 初期化）
- linker.ld が STM32F4 専用

→ **「共通インフラ」が実質 Cortex-M 専用** になっている。

### WASM / ホスト追加: ★★★★☆

umirtm, umibench, umimmio は `platforms/wasm/` や `platforms/host/` を持ち、独立して動作。良い設計。ただし wasm の xmake.lua が各ライブラリに散在。

---

## C. 構造的な美しさの評価

### 美しい点

1. **Concept ベースの HAL** — vtable なし、コンパイル時型制約、ゼロオーバーヘッド
2. **同名ヘッダ + xmake includedirs** による切り替え — シンプルかつ強力
3. **`_write()` syscall ルーティング** — ライブラリが HW を知らない設計は elegant
4. **umirtm/umibench/umimmio の独立性** — 0 依存、どこでも再利用可能

### 美しくない点

1. **パッケージが多すぎる計画** — umiport-arch, umiport-stm32, umiport-stm32f4, umiport-stm32h7... MCU が増えるたびにパッケージ増殖。5シリーズで 8パッケージ以上
2. **階層が深すぎる依存チェーン** — `umiport-stm32f4 → umiport-stm32 → umiport-arch → umihal` の4段は、実際に共有されるコードが極めて少ない割に複雑
3. **startup/syscalls の居場所が不自然** — 「MCU 固有コード禁止」の umiport に stm32f4/ ディレクトリがある矛盾
4. **umihal の Concept 粒度** — `Uart` concept に非同期操作まで含め、「NOT_SUPPORTED を返してもよい」という escape hatch は Concept の哲学に反する

---

## D. 改善提案

### 提案 1: パッケージ爆発の防止（推奨度: 高）

**問題:** 現在の計画では MCU 1シリーズにつき 1パッケージ（umiport-stm32f4, umiport-stm32h7, umiport-rp2040...）。10 MCU で 10+ パッケージ。

**改善案: `umiport` を 3層に整理し、MCU 固有コードを `umiport/` 内の明確なディレクトリに収める**

```
lib/umiport/
├── include/umiport/
│   ├── arm/cortex-m/          # アーキ共通（現状通り）
│   │   ├── cortex_m_mmio.hh
│   │   └── dwt.hh
│   ├── mcu/stm32f4/           # MCU 固有レジスタ操作
│   │   └── uart_output.hh
│   ├── mcu/stm32h7/           # 将来の MCU
│   │   └── sai.hh
│   └── mcu/rp2040/
├── src/
│   ├── stm32f4/               # startup/syscalls/linker（現状通り）
│   └── stm32h7/               # 将来
└── xmake.lua                  # 条件付きで src/ のサブセットを選択
```

**利点:**
- パッケージ数が爆発しない（umiport 1つに集約）
- MCU 固有コードの場所が明確（`mcu/` サブディレクトリ）
- xmake の `add_files` / `add_includedirs` で必要な部分だけ選択

**懸念:** umiport が大きくなる → ただしヘッダオンリーなのでリンクサイズに影響なし。使わない MCU のヘッダは `#include` しなければ存在しないのと同じ。

### 提案 2: startup/syscalls をボード層に移動（推奨度: 中〜高）

**問題:** startup.cc は MCU 固有（ベクタテーブルサイズ、FPU 初期化等）で、umiport の「MCU 非依存」原則と矛盾。

**改善案:**

```
lib/umiport-boards/
├── include/board/
│   ├── stm32f4-renode/
│   │   └── platform.hh
│   └── stm32f4-disco/
│       └── platform.hh
└── src/
    ├── stm32f4/              # ← startup, syscalls, linker をここに
    │   ├── startup.cc
    │   ├── syscalls.cc
    │   └── linker.ld
    └── stm32h7/
```

**なぜボード層か:**
- startup/syscalls は `#include "platform.hh"` でボードの `Platform` 型に依存
- リンカスクリプトは MCU のメモリマップに依存
- これらは「ボード統合」の一部と見なすのが自然

**さらに進めるなら:** startup.cc をテンプレート化して MCU 間の差分を最小化:

```cpp
// umiport-boards/src/common/startup_cortex_m.cc
template<typename Platform, std::size_t VectorTableSize>
void reset_handler_impl() { ... }
```

### 提案 3: umihal Concept の分割（推奨度: 高）

**問題:** `Uart` concept が 10+ の required expression を持ち、「NOT_SUPPORTED を返してもよい」で太っている。これは本質的に Java interface のアンチパターン。

**改善案: 必須/オプション Concept の分離**

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

### 提案 4: 非 ARM 対応の設計ガイドライン追加（推奨度: 中）

ARCHITECTURE.md は暗黙に Cortex-M newlib 環境を前提としている。以下を明記すべき：

- **出力経路の一般化**: `_write()` newlib syscall は Cortex-M/newlib 環境固有。ESP-IDF や Zephyr では別の仕組みが必要
- **startup の責務範囲**: ベクタテーブル、.data/.bss 初期化、FPU はどこまでが umiport の責務か
- **RTOS 統合**: FreeRTOS / Zephyr 上で動かす場合の startup の扱い

### 提案 5: ドキュメントの「現状」と「計画」を明確に分離（推奨度: 最高）

**全体設計の最大の改善ポイント。** 現在の ARCHITECTURE.md は：

- 存在しない `umiport-arch` を「ある」かのように記述
- 移行指針で「現状の platforms/ から」と書くが、ARM platforms はすでに移行済み
- `umihal/include/umihal/concept/` と書くが実際はフラット配置

**推奨:** ドキュメントを **「現状」セクション** と **「計画 / TODO」セクション** に明確分離。

---

## E. 代替アプローチの検討

### Alternative: Zephyr スタイルの Devicetree + Kconfig

Zephyr は Devicetree (.dts) + Kconfig でボード/MCU を宣言的に定義。

- **メリット**: ボード追加が宣言的、ビルドシステムとの統合が強力
- **デメリット**: C++23 Concept ベースの型安全性と相性が悪い、xmake との統合コストが非常に高い
- **判定**: UMI の規模と哲学に対してオーバーエンジニアリング。**現行アプローチの方が適切。**

### Alternative: Platform Traits struct（単一型で全て表現）

```cpp
struct Stm32f4DiscoTraits {
    using Output = RttOutput;
    using AudioDriver = I2sDriver<CS43L22>;
    using Timer = DwtTimer;
    static constexpr uint32_t system_clock_hz = 168'000'000;
    static void init() { ... }
};
```

`Platform` と `Board` を統合し、1つの Traits 型で全ボード特性を表現。

- **メリット**: platform.hh + board.hh の2ファイルが1つに。テンプレートパラメータとして渡しやすい
- **デメリット**: 1ファイルが大きくなる可能性
- **判定**: 現在の Platform + Board 分離は特に問題がないため、大きな改善にはならない。将来の複雑化を防ぐオプションとして覚えておく価値あり。

---

## F. 総合判定

| 観点 | 評価 | コメント |
|------|------|---------|
| 基本設計思想 | ★★★★★ | Concept + 同名ヘッダ切替は最適解 |
| 現在のコード品質 | ★★★★☆ | 実装は clean、命名も一貫 |
| ドキュメントの正確性 | ★★☆☆☆ | 現実との乖離が激しい |
| 同一 MCU 内の拡張性 | ★★★★★ | ボード追加は完璧 |
| 新 MCU 追加の拡張性 | ★★☆☆☆ | パッケージ爆発リスク |
| 非 ARM 対応 | ★★☆☆☆ | newlib/_write() 前提が制約 |
| ビルドシステム統合 | ★★★★☆ | embedded ルールは秀逸 |

### 最優先アクション

1. ARCHITECTURE.md を「現状」に合わせて更新（存在しないパッケージを削除 or TODO として明記）
2. `uart_output.hh` の配置を決定（umiport 内の `mcu/` サブディレクトリに移動 or 新パッケージ作成）
3. Concept の必須/オプション分離方針を策定
