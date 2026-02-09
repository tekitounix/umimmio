# umiport ARCHITECTURE_FINAL.md 完全移行 実装計画書

**ステータス:** 計画確定版
**策定日:** 2026-02-08
**基準文書:** ARCHITECTURE_FINAL.md（7エージェントレビュー＋6 device/separation レビュー統合）

---

## Context

ARCHITECTURE_FINAL.md で定義された設計に従い、現在の lib/ 構造を完全移行する。
移行対象は umihal, umiport, umiport-boards, umidevice, umirtm, umibench の6パッケージ。

**実行方針:**
- 全ステップをノンストップで連続実装する（完了まで停止しない）
- 各 Phase 完了時に `xmake test`（Host）で全テスト通過を確認してから次に進む
- Renode ビルド可能な Phase では `xmake build <target>` で ARM ビルドも確認する
- テスト失敗時はその Phase 内で修正を完了させてから次へ進む。前 Phase へのロールバック禁止

---

## 現状と設計目標の差分サマリー

| 現状 | 設計目標 | Gap |
|------|----------|-----|
| umihal: 単一 `Uart` concept | `UartBasic` / `UartAsync` 分割 | 要分割 |
| umihal: 単一 `AudioCodec` concept | `CodecBasic` / `CodecWithVolume` / `AudioCodec` 3段階 | 要分割 |
| umihal: Transport/Platform concept なし | `I2cTransport`, `SpiTransport`, `Platform`, `ClockTree` | 要追加 |
| umihal: ErrorCode に NACK 等なし | `NACK`, `BUS_ERROR`, `OVERRUN`, `INVALID_CONFIG` | 要追加 |
| umiport: `stm32f4/uart_output.hh` | `mcu/stm32f4/uart_output.hh` | 要移動 |
| umiport-boards: 別パッケージ | umiport/board/ に統合 | 要統合 |
| umiport: board.hh なし | `board/<name>/board.hh` 定数ファイル | 要作成 |
| umiport: platform.hh に static_assert なし | `static_assert(umi::hal::Platform<Platform>)` | 要追加 |
| umidevice: フラット構造 | `audio/` サブディレクトリ | 要分類 |
| umirtm: `::write(1, ...)` 固定出力 | `umi::rt::detail::write_bytes()` link-time 注入 | 要変更 |
| xmake: 手動ボイラープレート | `umiport.board` ルール | 要作成 |
| umibench: 独自 Platform | `umi::port::Platform::Timer` 経由 | 要統合 |
| umibench: `UMIBENCH_HOST` 等の define | 未使用なので削除 | 要削除 |

---

## 依存順序

```
Phase 1 → Phase 2 → Phase 3 ─┬→ Phase 5 → Phase 6 → Phase 7
                               └→ Phase 4 (独立実行可)
```

---

## Phase 1: umihal Concept 分割

**目的:** 単一巨大 Concept → Basic/Extended 分離、新規 Concept 追加。後続全 Phase の前提。

### Step 1.1: 新規 concept/ ヘッダ作成

| ファイル (新規作成) | 内容 |
|---|---|
| `lib/umihal/include/umihal/concept/platform.hh` | `umi::hal::OutputDevice`, `umi::hal::Platform` concept |
| `lib/umihal/include/umihal/concept/transport.hh` | `umi::hal::I2cTransport`, `umi::hal::SpiTransport` concept |
| `lib/umihal/include/umihal/concept/clock.hh` | `umi::hal::ClockSource`, `umi::hal::ClockTree` concept |
| `lib/umihal/include/umihal/concept/uart.hh` | `umi::hal::UartBasic`, `umi::hal::UartAsync` concept（旧 `Uart` を分割）|
| `lib/umihal/include/umihal/concept/codec.hh` | `umi::hal::CodecBasic`, `umi::hal::CodecWithVolume`, `umi::hal::AudioCodec`（3段階）|

**実装詳細:** ARCHITECTURE_FINAL.md Section 5.2〜5.6 のコード例に忠実に実装。namespace は `umi::hal`。

### Step 1.2: 旧ヘッダのフォワード

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umihal/include/umihal/uart.hh` | 型定義（DataBits, Config等）は維持、`concept/uart.hh` を include |
| `lib/umihal/include/umihal/codec.hh` | 旧 AudioCodec は維持、`concept/codec.hh` を include |

### Step 1.3: テスト更新

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umihal/tests/test_concepts.cc` | 新 concept ヘッダの include、新 stub 追加（UartBasicDev, TransportI2c, TransportSpi, Clock, ClockTreeImpl, PlatformStub, CodecBasicDev, CodecWithVol）、static_assert + ランタイムテスト追加 |

### 検証
```bash
xmake test -g test_umihal  # umihal テスト通過
xmake test                  # 全テスト通過（16/16）
```

---

## Phase 2: Result<T> ErrorCode 拡張

**目的:** Transport/Bus エラーコードの追加。

### Step 2.1: ErrorCode 更新

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umihal/include/umihal/result.hh` | `NACK`, `BUS_ERROR`, `OVERRUN`, `INVALID_CONFIG` を末尾に追加（既存値は削除しない） |

最終的な ErrorCode:
```cpp
enum class ErrorCode : std::uint8_t {
    OK = 0,
    INVALID_PARAMETER,
    TIMEOUT,
    BUSY,
    NOT_READY,
    NO_MEMORY,
    HARDWARE_ERROR,
    NOT_SUPPORTED,
    ABORTED,
    // Transport/Bus errors
    NACK,
    BUS_ERROR,
    OVERRUN,
    INVALID_CONFIG,
};
```

### Step 2.2: テスト更新

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umihal/tests/test_concepts.cc` | 新 ErrorCode のテストケース追加 |

### 検証
```bash
xmake test  # 全テスト通過
```

---

## Phase 3: umiport-boards → umiport 統合 + MCU ヘッダ再配置 + board.hh 作成

**目的:** パッケージ構造を ARCHITECTURE_FINAL.md Section 2 のディレクトリツリーに合わせる。

### Step 3.1: MCU ヘッダ移動

```
lib/umiport/include/umiport/stm32f4/uart_output.hh
  → lib/umiport/include/umiport/mcu/stm32f4/uart_output.hh
```

### Step 3.2: board/ ディレクトリを umiport に統合

```
lib/umiport-boards/include/board/stm32f4-renode/platform.hh
  → lib/umiport/include/umiport/board/stm32f4-renode/platform.hh

lib/umiport-boards/include/board/stm32f4-disco/platform.hh
  → lib/umiport/include/umiport/board/stm32f4-disco/platform.hh
```

### Step 3.3: 移動した platform.hh の修正

| ファイル (修正) | 変更内容 |
|---|---|
| `.../board/stm32f4-renode/platform.hh` | include パスを `<umiport/mcu/stm32f4/uart_output.hh>` に変更、`<umihal/concept/platform.hh>` を include、`static_assert(umi::hal::Platform<Platform>)` 追加 |
| `.../board/stm32f4-disco/platform.hh` | 同様に `static_assert` 追加 |

### Step 3.4: board.hh 新規作成

| ファイル (新規作成) | 内容 |
|---|---|
| `lib/umiport/include/umiport/board/stm32f4-renode/board.hh` | `umi::board::Stm32f4Renode`（HSE=25MHz, sysclk=168MHz, Pin, Memory 定数）|
| `lib/umiport/include/umiport/board/stm32f4-disco/board.hh` | `umi::board::Stm32f4Disco`（HSE=8MHz, sysclk=168MHz 等）|

### Step 3.5: 全 xmake.lua のボードパス更新

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umirtm/tests/xmake.lua` | board パスを `umiport/include/umiport/board/stm32f4-renode` に変更 |
| `lib/umirtm/examples/xmake.lua` | 同上（renode + disco 両方）|
| `lib/umibench/tests/xmake.lua` | 同上 |
| `lib/umimmio/tests/xmake.lua` | 同上 |
| `lib/umitest/tests/xmake.lua` | 同上 |
| `lib/umibench/tests/stm32f4-renode/umibench/platform.hh` | include を `<umiport/mcu/stm32f4/uart_output.hh>` に変更 |

### Step 3.6: umiport xmake.lua 更新

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umiport/xmake.lua` | `add_deps("umihal")` 追加 |

### Step 3.7: root xmake.lua と旧パッケージ削除

| ファイル (修正) | 変更内容 |
|---|---|
| `xmake.lua` | `includes("lib/umiport-boards")` を削除 |
| `lib/umiport-boards/` | ディレクトリ全体を削除（trash コマンド）|

### 検証
```bash
xmake test                              # 全ホストテスト通過
xmake build umirtm_stm32f4_renode       # ARM ビルド確認
xmake build umibench_stm32f4_renode     # ARM ビルド確認
xmake build umimmio_stm32f4_renode      # ARM ビルド確認
xmake build umitest_stm32f4_renode      # ARM ビルド確認
```

---

## Phase 4: umidevice カテゴリ化

**目的:** ドライバを `audio/` サブディレクトリに分類（ARCHITECTURE_FINAL.md Section 6）。

### Step 4.1: ファイル移動

```
lib/umidevice/include/umidevice/cs43l22/  → lib/umidevice/include/umidevice/audio/cs43l22/
lib/umidevice/include/umidevice/wm8731/   → lib/umidevice/include/umidevice/audio/wm8731/
lib/umidevice/include/umidevice/pcm3060/  → lib/umidevice/include/umidevice/audio/pcm3060/
lib/umidevice/include/umidevice/ak4556/   → lib/umidevice/include/umidevice/audio/ak4556/
```

### Step 4.2: テスト include 更新

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umidevice/tests/test_drivers.cc` | 全 include パスに `audio/` を挿入 |

### 検証
```bash
xmake test -g test_umidevice  # デバイスドライバテスト通過
xmake test                     # 全テスト通過
```

---

## Phase 5: write_bytes() link-time 注入

**目的:** umirtm 出力経路を `::write(1,...)` 固定から `umi::rt::detail::write_bytes()` link-time 注入に変更（ARCHITECTURE_FINAL.md Section 4）。

### Step 5.1: write.hh 宣言ヘッダ作成

| ファイル (新規作成) | 内容 |
|---|---|
| `lib/umirtm/include/umirtm/detail/write.hh` | `extern void umi::rt::detail::write_bytes(std::span<const std::byte>)` 宣言 |

### Step 5.2: printf.hh 出力経路変更

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umirtm/include/umirtm/printf.hh` | `#include <unistd.h>` 削除、`#include "detail/write.hh"` 追加、`vprintf()` 内の `::write(1, &ch, 1)` を `umi::rt::detail::write_bytes()` に変更 |

### Step 5.3: ホスト用 write_bytes 実装

| ファイル (新規作成) | 内容 |
|---|---|
| `lib/umiport/src/host/write.cc` | `umi::rt::detail::write_bytes()` → `::write(1, data.data(), data.size())` |

### Step 5.4: STM32F4 syscalls.cc に write_bytes 追加

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umiport/src/stm32f4/syscalls.cc` | `umi::rt::detail::write_bytes()` 実装追加（Platform::Output::putc() 委譲）。既存 `_write()` / `write()` は維持 |

### Step 5.5: ホストテスト xmake.lua 更新

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umirtm/tests/xmake.lua` | `test_umirtm` に `umiport/src/host/write.cc` を add_files |

**注意:** umirtm 以外のホストテスト（umibench, umimmio, umitest）は `rt::printf` を呼ばないため write.cc 不要。

### Step 5.6: host board platform.hh 作成

| ファイル (新規作成) | 内容 |
|---|---|
| `lib/umiport/include/umiport/board/host/platform.hh` | `umi::port::Platform`— Output::putc は `::write(1, &c, 1)`、`name()` = `"host"`、static_assert 付き |

### Step 5.7: WASM 用 write_bytes 実装

| ファイル (新規作成) | 内容 |
|---|---|
| `lib/umiport/src/wasm/write.cc` | `umi::rt::detail::write_bytes()` → WASM 出力（`console.log` 等へのブリッジ）|
| `lib/umiport/include/umiport/board/wasm/platform.hh` | `umi::port::Platform`— Output::putc は `std::fputc(c, stdout)`、`name()` = `"wasm"`、static_assert 付き |

WASM 実装例:
```cpp
// umiport/src/wasm/write.cc
#include <umirtm/detail/write.hh>
#include <cstdio>

namespace umi::rt::detail {

void write_bytes(std::span<const std::byte> data) {
    std::fwrite(data.data(), 1, data.size(), stdout);
}

} // namespace umi::rt::detail
```

**注意:** WASM テストターゲット（`*_wasm`）にも `write.cc` のリンクが必要。対象は `lib/umirtm/tests/xmake.lua` の WASM ターゲット。

### 検証
```bash
xmake test -g test_umirtm              # umirtm ホストテスト通過
xmake test                              # 全テスト通過
xmake build umirtm_stm32f4_renode       # ARM ビルド確認
```

---

## Phase 6: xmake umiport.board ルール + ターゲットリファクタ

**目的:** ボイラープレート排除。ARM ターゲットを `umiport.board` ルール1行で設定可能に（ARCHITECTURE_FINAL.md Section 9）。

### Step 6.1: ルール作成

| ファイル (新規作成) | 内容 |
|---|---|
| `lib/umiport/rules/board.lua` | `rule("umiport.board")` — `on_config` で board includedirs + startup/syscalls/linker を自動設定 |

ルール実装（ARCHITECTURE_FINAL.md Section 9.1 準拠）:
```lua
rule("umiport.board")
    on_config(function(target)
        local board = target:values("umiport.board")
        local umiport_dir = path.join(os.scriptdir(), "..")
        local board_include = path.join(umiport_dir, "include/umiport/board", board)
        local src_dir = path.join(umiport_dir, "src")

        target:add("includedirs", board_include, {public = false})

        local use_startup = target:values("umiport.startup")
        if use_startup == "false" then return end

        local mcu = target:values("embedded.mcu")
        if mcu then
            local mcu_family = mcu:match("^(stm32%a%d)")  -- "stm32f4" にマッチ（%w+ は貪欲で stm32f407vg 全体に食い込む）
            if mcu_family then
                local mcu_src = path.join(src_dir, mcu_family)
                target:add("files", path.join(mcu_src, "startup.cc"))
                target:add("files", path.join(mcu_src, "syscalls.cc"))
                target:set("values", "embedded.linker_script",
                    path.join(mcu_src, "linker.ld"))
            end
        end
    end)
```

### Step 6.2: umiport xmake.lua 更新

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umiport/xmake.lua` | `includes("rules/board.lua")` 追加 |

### Step 6.3: 全 ARM ターゲットリファクタ（10ターゲット）

| ファイル (修正) | 対象ターゲット |
|---|---|
| `lib/umirtm/tests/xmake.lua` | `umirtm_stm32f4_renode`, `umirtm_stm32f4_renode_gcc` |
| `lib/umirtm/examples/xmake.lua` | 全 ARM ターゲット |
| `lib/umibench/tests/xmake.lua` | `umibench_stm32f4_renode`, `umibench_stm32f4_renode_gcc` |
| `lib/umimmio/tests/xmake.lua` | `umimmio_stm32f4_renode`, `umimmio_stm32f4_renode_gcc` |
| `lib/umitest/tests/xmake.lua` | `umitest_stm32f4_renode`, `umitest_stm32f4_renode_gcc` |

変更パターン（旧 → 新）:
```lua
-- 旧: 手動パス設定（各ファイル先頭で定義 + 各ターゲットで参照）
local umiport_stm32f4 = path.join(os.scriptdir(), "../../umiport/src/stm32f4")
local board_stm32f4_renode = path.join(os.scriptdir(), "../../umiport/include/umiport/board/stm32f4-renode")
-- ターゲット内:
add_files(path.join(umiport_stm32f4, "startup.cc"))
add_files(path.join(umiport_stm32f4, "syscalls.cc"))
set_values("embedded.linker_script", path.join(umiport_stm32f4, "linker.ld"))
add_includedirs(board_stm32f4_renode, {public = false})

-- 新: ルール1行
add_rules("umiport.board")
set_values("umiport.board", "stm32f4-renode")
```

### 検証
```bash
xmake test                              # 全ホストテスト通過
xmake build umirtm_stm32f4_renode       # ARM clang ビルド確認
xmake build umirtm_stm32f4_renode_gcc   # ARM gcc ビルド確認
xmake build umibench_stm32f4_renode     # ARM ビルド確認
xmake build umimmio_stm32f4_renode      # ARM ビルド確認
xmake build umitest_stm32f4_renode      # ARM ビルド確認
```

---

## Phase 7: umibench 二重 Platform 解消 + クリーンアップ

**目的:** umibench 独自 Platform を `umi::port::Platform::Timer` 経由に統合。不要 define 削除。

### Step 7.1: Platform に Timer 追加

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umiport/include/umiport/board/stm32f4-renode/platform.hh` | `using Timer = umi::port::cortex_m::DwtTimer;` 追加、`name()` メソッド追加 |
| `lib/umihal/include/umihal/concept/platform.hh` | `PlatformWithTimer` concept 追加 |

### Step 7.2: umibench stm32f4-renode platform.hh を刷新

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umibench/tests/stm32f4-renode/umibench/platform.hh` | `umi::port::Platform` から Timer/Output を委譲取得するアダプターに変更 |

アダプター設計:
- `Timer` → `umi::port::Platform::Timer` を直接使用
- `Output` → `putc` は `umi::port::Platform::Output::putc` に委譲、`puts/print_uint/print_double` は自前実装を維持（umibench 固有 API のため）

### Step 7.3: 不要 define 削除

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umibench/xmake.lua` | `UMIBENCH_HOST`, `UMIBENCH_WASM`, `UMIBENCH_EMBEDDED` の `add_defines` 行を削除（ソースコードで未使用確認済み）|

### Step 7.4: compile_fail テスト追加

| ファイル (新規作成) | 内容 |
|---|---|
| `lib/umihal/tests/compile_fail/test_platform_concept.cc` | `static_assert(umi::hal::Platform<IncompleteType>)` が失敗することを検証 |
| `lib/umihal/tests/compile_fail/test_transport_concept.cc` | `umi::hal::I2cTransport`, `umi::hal::SpiTransport` の不完全実装が拒否されることを検証 |

compile_fail テストにより、Concept 制約が正しく機能することをコンパイル失敗で保証する。

### Step 7.5: examples テスト追加

| ファイル (修正) | 変更内容 |
|---|---|
| `lib/umirtm/examples/xmake.lua` | ARM + ホスト両方のサンプルターゲットが `umiport.board` ルールを使用していることを確認 |

### 検証（最終確認）
```bash
xmake test                              # 全ホストテスト通過（compile_fail 含む）
xmake build umirtm_stm32f4_renode       # ARM ビルド確認
xmake build umibench_stm32f4_renode     # ARM ビルド確認
xmake build umimmio_stm32f4_renode      # ARM ビルド確認
xmake build umitest_stm32f4_renode      # ARM ビルド確認
```

---

## 影響ファイル集計

| Phase | 新規 | 修正 | 移動 | 削除 | 合計 |
|:-----:|:----:|:----:|:----:|:----:|:----:|
| 1 | 5 | 3 | 0 | 0 | 8 |
| 2 | 0 | 2 | 0 | 0 | 2 |
| 3 | 2 | 8 | 3 | 1 dir | 14 |
| 4 | 0 | 1 | 4 dirs | 0 | 5 |
| 5 | 5 | 3 | 0 | 0 | 8 |
| 6 | 1 | 6 | 0 | 0 | 7 |
| 7 | 2 | 5 | 0 | 0 | 7 |
| **計** | **15** | **28** | **7** | **1** | **51** |

---

## 並列実行戦略とオーケストレーション

### 原則

- **サブエージェント:** 独立した作業単位を並列に実行する
- **オーケストレーター:** サブエージェント完了後に成果物を入念にチェック（ファイル内容、include パス整合性、namespace 一貫性、テスト通過）
- **チェックポイント:** 各 Phase 完了時にオーケストレーターが `xmake test` を実行し、通過を確認してから次 Phase に進む

### 並列実行マップ

```
Phase 1+2 (統合実行: umihal は単一パッケージのため分離不可)
│
├─ Agent A: concept/platform.hh + concept/transport.hh + concept/clock.hh 作成
├─ Agent B: concept/uart.hh + concept/codec.hh 作成
├─ Agent C: result.hh ErrorCode 拡張
│
├── Orchestrator: 全 Agent 成果物をチェック ──┐
│   - namespace が umi::hal か                │
│   - #include パスが正しいか                 │
│   - Result<T> を使う concept が result.hh   │
│     を include しているか                    │
│   - 旧ヘッダとの名前衝突がないか            │
│                                             │
├── Agent D: test_concepts.cc 更新             │
│   (Agent A,B,C の成果物を前提)              │
│                                             │
├── Orchestrator: テスト実行・チェック ────────┘
│   xmake test
│
Phase 3+4 (並列可能: umiport 統合 ∥ umidevice カテゴリ化)
│
├─ Agent E: Phase 3 全体                      ├─ Agent F: Phase 4 全体
│  - MCU ヘッダ移動                            │  - audio/ ディレクトリ作成
│  - board/ 統合                               │  - 4デバイスディレクトリ移動
│  - platform.hh 修正 + static_assert          │  - test_drivers.cc 更新
│  - board.hh 作成                             │
│  - 全 xmake.lua ボードパス更新               │
│  - umiport xmake.lua 更新                    │
│  - root xmake.lua 更新                       │
│  - umiport-boards 削除                       │
│                                              │
├── Orchestrator: 両 Agent 成果物をチェック ────┤
│   - Phase 3: include パスの一貫性            │
│     (mcu/stm32f4/ と board/ の整合)          │
│   - Phase 3: platform.hh の static_assert    │
│   - Phase 3: xmake.lua のパス更新漏れ        │
│   - Phase 4: include パスの audio/ 挿入      │
│   - Phase 4: test_drivers.cc の更新漏れ      │
│                                              │
├── Orchestrator: テスト実行 ──────────────────┘
│   xmake test
│   xmake build umirtm_stm32f4_renode (ARM)
│
Phase 5 (逐次実行: printf.hh 変更 → write.cc 作成 → xmake 更新)
│
├─ Agent G: Phase 5 全体
│  - write.hh 宣言ヘッダ作成
│  - printf.hh 出力経路変更
│  - host/write.cc + host/platform.hh 作成
│  - wasm/write.cc + wasm/platform.hh 作成
│  - syscalls.cc write_bytes 追加
│  - umirtm tests xmake.lua 更新（host + WASM）
│
├── Orchestrator: チェック
│   - write_bytes の宣言と実装のシグネチャ一致（host, wasm, stm32f4 の3実装）
│   - printf.hh から ::write / <unistd.h> が消えているか
│   - ホスト・WASM テストに write.cc がリンクされているか
│
├── Orchestrator: テスト実行
│   xmake test
│   xmake build umirtm_stm32f4_renode
│
Phase 6 (逐次実行: ルール作成 → ターゲットリファクタ)
│
├─ Agent H: rules/board.lua 作成 + umiport xmake.lua 更新
├─ (Agent H 完了待ち)
├─ Agent I: umirtm/umimmio ARM ターゲットリファクタ  ├─ Agent J: umibench/umitest ARM ターゲットリファクタ
│                                                    │
├── Orchestrator: チェック
│   - ルールのパス解決 (os.scriptdir)
│   - 全ターゲットが umiport.board ルールを使用しているか
│   - ボイラープレート変数 (umiport_stm32f4, board_stm32f4_renode) が残っていないか
│
├── Orchestrator: テスト実行
│   xmake test
│   xmake build 全 ARM ターゲット
│
Phase 7 (逐次実行: Platform 統合)
│
├─ Agent K: Phase 7 全体
│  - Platform に Timer 追加
│  - umibench platform.hh アダプター化
│  - UMIBENCH_* define 削除
│  - compile_fail テスト追加（Platform, Transport concept）
│  - examples xmake.lua 確認
│
├── Orchestrator: 最終チェック
│   - Timer 型が umi::port::Platform に含まれるか
│   - umibench platform.hh がアダプター方式になっているか
│   - UMIBENCH_* define がソースから消えているか
│   - compile_fail テストがビルド失敗を正しく検出するか
│
├── Orchestrator: 最終テスト
│   xmake test (全ホストテスト + compile_fail)
│   xmake build 全 ARM ターゲット
```

### オーケストレーターチェック体制

各 Phase 完了時のチェックも **複数のサブエージェントで並列実行** する。
オーケストレーター本体は結果の集約と判定のみ行う。

#### チェックエージェント構成

```
Phase 完了
│
├─ Check Agent α: コード品質チェック（並列）
│  - namespace 一貫性: grep で umi::hal / umi::port / umi::board 確認
│  - include パス整合性: grep で旧パスが残っていないか
│  - コード規約: constexpr, 命名規則, Left-aligned pointer
│  - static_assert 存在: platform.hh の契約検証
│
├─ Check Agent β: 構造チェック（並列）
│  - 旧ファイル残存: 移動元ディレクトリにファイルが残っていないか
│  - ディレクトリ構造: ARCHITECTURE_FINAL.md Section 2 のツリーとの差分
│  - xmake パス参照: grep で旧パッケージ参照がゼロか
│  - 設計文書との差分: 作成されたファイルが設計のコード例と一致するか
│
├─ Check Agent γ: ビルド・テスト実行（並列）
│  - xmake test で全ホストテスト通過
│  - xmake build で ARM ターゲットビルド確認
│  - リンクエラー、未定義シンボルの検出
│
└─ Orchestrator: 3 Agent の結果を集約
   - 全 Agent が PASS → 次 Phase に進行
   - いずれかが FAIL → 修正エージェントを起動し再チェック
```

#### Phase 別チェック対象

| Phase | Check α (コード品質) | Check β (構造) | Check γ (ビルド) |
|:-----:|---|---|---|
| 1+2 | 新 concept の namespace, include | concept/ ディレクトリ構造 | `xmake test` |
| 3+4 | platform.hh の static_assert, include パス | mcu/, board/, audio/ 構造 | `xmake test` + ARM build |
| 5 | write_bytes シグネチャ一致（host/wasm/stm32f4）, `<unistd.h>` 除去 | detail/write.hh, host/write.cc, wasm/write.cc 存在 | `xmake test` + ARM build |
| 6 | ルールの Lua 構文, パス解決 | ボイラープレート変数が残っていないか | `xmake test` + 全 ARM build |
| 7 | アダプター設計の整合性, compile_fail テスト | UMIBENCH_* define 除去, compile_fail/ 存在 | `xmake test` (compile_fail 含む) + 全 ARM build |

### サブエージェントへの指示テンプレート

各サブエージェントには以下の情報を与える:

1. **作業範囲:** 担当する Phase/Step の番号と対象ファイル一覧
2. **設計文書参照:** ARCHITECTURE_FINAL.md の該当セクション番号
3. **コード規約:** CLAUDE.md の Code Style Rules（namespace, 命名規則, constexpr）
4. **禁止事項:** 担当範囲外のファイルを変更しない
5. **完了条件:** 作成/修正した全ファイルの内容を報告

### 並列度サマリー

| Phase | 並列エージェント数 | 理由 |
|:-----:|:------------------:|------|
| 1+2 | 3 (A,B,C) → 1 (D) | concept ファイル群は独立、テスト更新は依存 |
| 3+4 | 2 (E,F) | umiport と umidevice は独立パッケージ |
| 5 | 1 (G) | printf.hh → write.cc → xmake の逐次依存 |
| 6 | 1 (H) → 2 (I,J) | ルール作成後に xmake リファクタを並列化 |
| 7 | 1 (K) | 単一パッケージの統合作業 |

---

## リスク分析

| Phase | リスク | 対策 |
|-------|--------|------|
| 1 | 旧 Uart/AudioCodec との名前衝突 | 旧 concept 維持＋新 concept は別名 (UartBasic ≠ Uart) |
| 2 | ErrorCode 数値変更による既存テスト破壊 | 末尾追加のため既存値の位置は不変 |
| 3 | startup.cc の `#include "platform.hh"` パス解決 | xmake includedirs で解決（既存メカニズム維持） |
| 3 | umiport-boards 削除後のビルド失敗 | 全 xmake.lua のパス更新を同時に行う |
| 4 | umidevice include パス変更 | 外部利用者なし、即時移行で問題なし |
| 5 | `<unistd.h>` 削除による暗黙依存の破壊 | printf.hh 経由の暗黙依存なし（grep 確認済み） |
| 5 | write_bytes 未リンクによるリンクエラー | 全テストターゲットに write.cc 追加を確認 |
| 6 | umiport.board ルールのパス解決 | `os.scriptdir()` + `path.join` でルール位置起点 |
| 7 | umibench::Platform::Output の互換性 | アダプター方式で putc 委譲、拡張メソッドは自前維持 |

---

## レビュー指摘事項（2026-02-08 実装前レビュー）

実コードベースとの突合せにより発見された問題。実装着手前に解決必須。

### E1. [致命的] Lua パターンマッチバグ（Phase 6 / ARCHITECTURE_FINAL.md Section 9.1）

**現象:** `mcu:match("^(stm32%w+)")` は `%w+` が貪欲なため、`"stm32f407vg"` 全体にマッチし `"stm32f4"` を抽出できない。`umiport/src/stm32f407vg/startup.cc` を探して全 ARM ターゲットが壊れる。

**修正:** 本計画および ARCHITECTURE_FINAL.md の両方で修正済み。
```lua
-- 旧（バグ）: local mcu_family = mcu:match("^(stm32%w+)")
-- 新（修正）:
local mcu_family = mcu:match("^(stm32%a%d)")  -- "stm32f4" にマッチ
```

**ステータス:** ✅ 文書修正済み

### E2. [要検討] disco platform.hh の umirtm 依存（Phase 3）

**現象:** 現在の `stm32f4-disco/platform.hh` は `<umirtm/rtm.hh>` を include して RTT リングバッファ経由で出力する。これを `umiport/board/stm32f4-disco/` に移動すると、umiport のヘッダが umirtm のヘッダを論理的に参照する。

**ビルドへの影響:** なし。umiport / umiport-boards は headeronly パッケージであり、`add_deps` で直接的な依存宣言はない。platform.hh がコンパイルされるのは使用側ターゲットのコンテキストであり（例: `umirtm_example_stm32f4_disco` は `add_deps("umirtm", "umiport")` と宣言）、umirtm の include パスは使用側で解決される。**移行によりビルドが壊れることはない。**

**設計への影響:** ARCHITECTURE_FINAL.md Section 3 の依存図では `umirtm (0 deps)` が独立として描かれ、umiport は umirtm を知らない想定。platform.hh 内の `#include <umirtm/rtm.hh>` はこの論理的依存に違反する。ただし**この違反は現在の umiport-boards でも同じ**であり、移行で新たに発生する問題ではない。

**補足:** ARCHITECTURE_FINAL.md Section 7.3 の disco コード例では `using Output = stm32f4::UartOutput` と記述しているが、Section 7.5 の表では disco の Output は「RTT リングバッファ」と明記しており、**設計文書内に矛盾がある。** disco の Output をUART / RTT のどちらで設計するかを確定する必要がある。

**解決案:**
1. Phase 5 の `write_bytes()` link-time 注入パターンに統一する。disco の `write_bytes()` 実装が RTT に書き込む形にすれば、platform.hh は umirtm を知らなくてよい
2. RTT 出力コードを `umiport/mcu/stm32f4/rtt_output.hh` として MCU ドライバ扱いにする
3. 現状維持。headeronly のため実害なし、設計図の注釈で論理的依存を明記する

**ステータス:** 📝 設計判断必要（ブロッカーではない）。ARCHITECTURE_FINAL.md Section 7.3 と 7.5 の矛盾を解消すること

### E3. [低] vprintf の per-char 出力構造（Phase 5）

**現象:** 現在の `vprintf()` はラムダで1文字ずつ `::write(1, &ch, 1)` を呼ぶコールバック（PutcFunc）構造。`write_bytes(std::span<const std::byte>)` に変更すると、毎文字 1 byte の span を生成することになる。

**検証結果:** ARM における現在の呼び出しパスは `vprintf ラムダ → ::write(1, &ch, 1) → syscalls.cc write() → write_buf() → Platform::Output::putc()` と3段の per-char 呼び出し。`write_bytes` 変更後は `vprintf ラムダ → write_bytes({&byte, 1}) → Platform::Output::putc()` と中間レイヤーが1段減る。**性能劣化ではなく、むしろ微改善。**

**影響:** Phase 5 Step 5.2 の変更は性能上のリスクなし。per-char → per-char のまま API 注入点が変わるだけ。

**バッファリングについて:** 将来的に `vprintf` 内にローカルバッファを設けて一括 `write_bytes()` する最適化は有用だが、Phase 5 のスコープ外で十分。

**ステータス:** ✅ 問題なし（Phase 5 はそのまま実行可能）

### E4. [低] RenodeUartOutput の命名（Phase 3）

**現象:** `uart_output.hh` の構造体名が `RenodeUartOutput` だが、ARCHITECTURE_FINAL.md のコード例では `stm32f4::UartOutput`。

**検証結果:** 実装を確認すると、`init()` ではボーレート設定を行っていない。Renode では USART がキャラクタストリームとして動作するためボーレート不要だが、実機 UART (例: disco でシリアル出力する場合）ではボーレート設定が必須。つまり**このドライバは Renode 前提の簡易版**であり、`RenodeUartOutput` という命名には合理性がある。

**選択肢:**
- (A) `RenodeUartOutput` のまま `mcu/stm32f4/` に置く — Renode 用 USART1 簡易ドライバの意味で命名が実態と一致
- (B) `UartOutput` にリネーム — 将来ボーレート設定等を追加して汎用化する前提

これは設計判断であり、どちらも正当。ARCHITECTURE_FINAL.md のコード例との一貫性を取るなら (B)。

**ステータス:** 📝 Phase 3 実施時に判断

### E5. [低] Phase 4 の実行タイミング最適化

**現象:** Phase 4（umidevice カテゴリ化）は他の全 Phase に依存しない。Phase 1 以前でも実行可能だが、計画では Phase 3 以降に配置。

**影響:** 並列度が若干低い。

**修正:** Phase 4 を Phase 1+2 と並列実行可能とマーク。

**ステータス:** 📝 最適化の余地あり（ブロッカーではない）

### E6. [低] umibench platforms/host/ の Platform 統合

**現象:** `lib/umibench/platforms/host/umibench/platform.hh` は `PlatformBase<ChronoTimer, StdoutOutput>` を使い、`umi::port::Platform` を参照しない。Phase 7 のスコープは embedded 側のみで、host platform は対象外。

**影響:** Phase 7 完了後も umibench host は独自 Platform のまま残る。

**修正:** Phase 7 完了後の将来 TODO として明記。ブロッカーではない。

**ステータス:** 📝 将来の TODO
