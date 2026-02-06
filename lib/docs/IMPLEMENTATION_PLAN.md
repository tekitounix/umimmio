# UMI ライブラリ標準化実装計画

**Created**: 2026-02-07  
**Phase**: Planning → Execution  
**Target**: umibench (完成) → lib/docs (統合) → umimmio/umitest (移行)

---

## フェーズ概略

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Phase 1       │ ──▶ │   Phase 2       │ ──▶ │   Phase 3       │
│   umibench      │     │   lib/docs      │     │   Migration     │
│   完成・検証    │     │   統一標準化    │     │   移行・適用    │
└─────────────────┘     └─────────────────┘     └─────────────────┘
       │                       │                       │
       ▼                       ▼                       ▼
  ・xmake.lua最適化      ・UMI_LIBRARY_STANDARD  ・umimmio移行
  ・CI整備              ・実装テンプレート作成   ・umitest移行
  ・package追加         ・統合ドキュメント構成   ・新規ライブラリ作成
  ・動作検証            ・チェックリスト整備
```

---

## Phase 1: umibench 完成・検証

### 1.1 現状分析

**完了済み**:
- ✅ ディレクトリ構造（UMI_LIBRARY_STANDARD 準拠）
- ✅ 基本ドキュメント構成
- ✅ CIワークフロー（ホスト/WASM/ARMビルド）
- ✅ compile_commands.autoupdate 設定

**要修正**:
- ⚠️ tests/xmake.lua - `add_deps("umitest")` をヘルパー関数化
- ⚠️ platforms/arm/cortex-m/stm32f4/xmake.lua - 同上
- ⚠️ platforms/wasm/xmake.lua - 同上
- ⚠️ xmake.lua - umitest/umimmio の require を条件分岐化

### 1.2 実装タスク

#### Task 1.2.1: xmake.lua 依存関係改善

```lua
-- 改善後の構成（概略）
local standalone_repo = os.projectdir() == os.scriptdir()
_G.UMIBENCH_STANDALONE_REPO = standalone_repo

if standalone_repo then
    set_project("umibench")
    set_version("0.9.0-beta.1")
    set_xmakever("2.8.0")

    set_languages("c++23")
    add_rules("mode.debug", "mode.release")
    add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", lsp = "clangd"})
    set_warnings("all", "extra", "error")

    -- 依存関係は optional にし、ビルド時に解決
    add_requires("arm-embedded", {optional = true})
    add_requires("umimmio", {optional = true})
    add_requires("umitest", {optional = true})
end

-- ヘルパー関数
function _G.umibench_add_umimmio_dep()
    if standalone_repo then
        add_packages("umimmio")
    else
        add_deps("umimmio")
    end
end

function _G.umibench_add_umitest_dep()
    if standalone_repo then
        add_packages("umitest")
    else
        add_deps("umitest")
    end
end
```

#### Task 1.2.2: サブモジュール xmake.lua 修正

- `tests/xmake.lua`:
  - `add_deps("umitest")` → `umibench_add_umitest_dep()`

- `platforms/arm/cortex-m/stm32f4/xmake.lua`:
  - `add_deps("umitest")` → `umibench_add_umitest_dep()`

- `platforms/wasm/xmake.lua`:
  - `add_deps("umitest")` → `umibench_add_umitest_dep()`

#### Task 1.2.3: arm-embedded-xmake-repo package 追加

`.refs/arm-embedded-xmake-repo/packages/u/umibench/xmake.lua` を新規作成：

```lua
package("umibench")
    set_homepage("https://github.com/tekitounix/umibench")
    set_description("UMI cross-target micro-benchmark library")
    set_license("MIT")
    
    set_kind("library", {headeronly = true})
    
    add_versions("dev", "git:../../../lib/umibench")
    -- add_versions("1.0.0", "https://github.com/tekitounix/umibench/archive/v1.0.0.tar.gz")
    
    add_configs("backend", {
        description = "Target backend",
        default = "host",
        values = {"host", "wasm", "embedded"}
    })
    
    on_load(function(package)
        if package:config("backend") == "embedded" then
            package:add("deps", "arm-embedded")
            package:add("deps", "umimmio")
        end
    end)
    
    on_install(function(package)
        os.cp("include", package:installdir())
        os.cp("platforms", package:installdir())
    end)
    
    on_test(function(package)
        -- インクルードテスト
        assert(package:check_cxxsnippets({test = [[
            #include <umibench/bench.hh>
            void test() {
                umi::bench::Runner<int> runner;
            }
        ]]}, {configs = {languages = "c++23"}}))
    end)
package_end()
```

#### Task 1.2.4: ローカル開発用 xmake repo 設定

開発時にローカルの arm-embedded-xmake-repo を参照する設定：

```lua
-- umibench/xmake.lua 内（standalone_repo 時）
if standalone_repo then
    -- ローカルrepo優先、なければGitHub
    local local_repo = path.join(os.scriptdir(), "..", "..", ".refs", "arm-embedded-xmake-repo")
    if os.isdir(local_repo) then
        add_repositories("arm-embedded " .. local_repo)
    else
        add_repositories("arm-embedded https://github.com/tekitounix/arm-embedded-xmake-repo.git")
    end
end
```

### 1.3 検証タスク

| 検証項目 | コマンド | 期待結果 |
|---------|---------|---------|
| ホストテスト | `xmake test "test_umibench/*"` | パス |
| compile-fail | `xmake test "test_umibench_compile_fail/*"` | パス |
| WASMビルド | `xmake build umibench_wasm` | 成功 |
| ARM GCCビルド | `xmake build umibench_stm32f4_renode_gcc` | 成功 |
| CI動作 | GitHub Actions | 全ジョブパス |

### 1.4 Phase 1 完了基準

- [ ] xmake.lua 依存関係改善完了
- [ ] 全サブモジュール xmake.lua 修正完了
- [ ] arm-embedded-xmake-repo package 追加完了
- [ ] 全検証項目パス
- [ ] CI 全ジョブパス

---

## Phase 2: lib/docs 統一標準化

### 2.1 現状の lib/docs 分析

| ファイル | 内容 | 処遇 |
|---------|------|------|
| INSTRUCTION.md | ライブラリ作成ルール（リンク集） | 統合・更新 |
| LIBRARY_STRUCTURE.md | ディレクトリ構造規約 | UMI_LIBRARY_STANDARD に統合 |
| XMAKE.md | xmake コマンドリファレンス | 維持・参照追加 |
| TESTING.md | テスト戦略 | 維持・参照追加 |
| CODING_STYLE.md | コーディングスタイル | 維持 |
| CLANG_TOOLING.md | clang ツール設定 | 維持 |
| DEBUG_GUIDE.md | デバッグガイド | 維持 |
| DOXYGEN_STYLE.md | Doxygen スタイル | 維持 |

### 2.2 新規作成・更新ファイル

#### Task 2.2.1: UMI_LIBRARY_STANDARD.md（作成済み）

`lib/docs/UMI_LIBRARY_STANDARD.md` に統合：
- ディレクトリ構造（LIBRARY_STRUCTURE.md から移行）
- xmake.lua 標準構造
- CI/CD 標準
- ドキュメント標準
- パッケージ化標準

#### Task 2.2.2: INSTRUCTION.md 更新

新しい入口ドキュメントとして再構成：

```markdown
# UMI ライブラリ作成ガイド

## クイックスタート

新規ライブラリ作成：
1. [UMI_LIBRARY_STANDARD.md](UMI_LIBRARY_STANDARD.md) を読む
2. テンプレートをコピー: `cp -r lib/umibench lib/<newlib>`
3. 置換とカスタマイズ
4. テスト実行: `xmake test`

## ドキュメント構成

| 目的 | 参照先 |
|------|--------|
| 標準構造・規約 | [UMI_LIBRARY_STANDARD.md](UMI_LIBRARY_STANDARD.md) |
| xmake コマンド | [XMAKE.md](XMAKE.md) |
| テスト戦略 | [TESTING.md](TESTING.md) |
| コーディングスタイル | [CODING_STYLE.md](CODING_STYLE.md) |
| ツール設定 | [CLANG_TOOLING.md](CLANG_TOOLING.md) |
| デバッグ | [DEBUG_GUIDE.md](DEBUG_GUIDE.md) |
| Doxygen | [DOXYGEN_STYLE.md](DOXYGEN_STYLE.md) |

## リファレンス実装

- [umibench](../umibench/) - 完成された標準実装
```

#### Task 2.2.3: LIBRARY_STRUCTURE.md 統合

内容を UMI_LIBRARY_STANDARD.md に統合し、以下の内容で置き換え：

```markdown
# LIBRARY_STRUCTURE.md

このドキュメントは [UMI_LIBRARY_STANDARD.md](UMI_LIBRARY_STANDARD.md) に統合されました。

- ディレクトリ構造: UMI_LIBRARY_STANDARD.md 第2章
- namespace規約: UMI_LIBRARY_STANDARD.md 第2章
```

#### Task 2.2.4: ライブラリ作成テンプレート

`lib/docs/template/` ディレクトリを作成し、最小テンプレートを提供：

```
lib/docs/template/
├── README.md.template
├── xmake.lua.template
├── docs/
│   └── INDEX.md.template
└── include/
    └── libname/
        └── libname.hh.template
```

またはスクリプト化：

```bash
# 使用例
python3 lib/docs/tools/create_library.py <libname>
```

### 2.3 Phase 2 完了基準

- [ ] UMI_LIBRARY_STANDARD.md 完成
- [ ] INSTRUCTION.md 更新
- [ ] LIBRARY_STRUCTURE.md 統合完了
- [ ] テンプレート or スクリプト作成
- [ ] 全リンク動作確認

---

## Phase 3: 移行・適用

### 3.1 umimmio 移行

#### Task 3.1.1: 構造分析

```bash
ls -la lib/umimmio/
# 現状を確認し、UMI_LIBRARY_STANDARD との差分を洗い出し
```

#### Task 3.1.2: 必要な変更

| 項目 | 現在 | 変更後 |
|------|------|--------|
| xmake.lua | モノレポのみ対応 | 単体repo対応追加 |
| docs/ | 最小構成 | 標準構成へ拡張 |
| CI | なし | umimmio-ci.yml 追加 |
| tests/ | なし？ | 標準テスト構成追加 |
| examples/ | なし？ | 追加検討 |

#### Task 3.1.3: 実装ステップ

1. xmake.lua 単体repo対応
2. docs/ 構成拡張
3. tests/xmake.lua 作成
4. .github/workflows/umimmio-ci.yml 作成
5. arm-embedded-xmake-repo package 追加
6. 全テストパス確認

### 3.2 umitest 移行

umimmio と同様のステップを実施。

umitest は他のライブラリの依存先なので、優先度を高く設定。

### 3.3 Phase 3 完了基準

- [ ] umimmio 標準化完了
- [ ] umitest 標準化完了
- [ ] arm-embedded-xmake-repo 全package追加完了
- [ ] 全CIパス確認

---

## スケジュール案

| フェーズ | 期間 | 主な作業 |
|---------|------|---------|
| Phase 1 | 1-2日 | umibench 完成・検証 |
| Phase 2 | 1-2日 | lib/docs 統一標準化 |
| Phase 3 | 2-3日 | umimmio/umitest 移行 |
| **合計** | **4-7日** | |

---

## タスク実行順序

```
Day 1: Phase 1
├── Task 1.2.1: xmake.lua 依存関係改善
├── Task 1.2.2: サブモジュール xmake.lua 修正
├── Task 1.2.3: arm-embedded-xmake-repo package
├── Task 1.2.4: ローカルrepo設定
└── 検証・CI確認

Day 2: Phase 2
├── Task 2.2.2: INSTRUCTION.md 更新
├── Task 2.2.3: LIBRARY_STRUCTURE.md 統合
├── Task 2.2.4: テンプレート作成
└── 全ドキュメント整合性確認

Day 3-4: Phase 3 (umimmio)
├── 構造分析
├── xmake.lua 更新
├── docs/ 拡張
├── CI追加
└── 検証

Day 5-6: Phase 3 (umitest)
├── umimmio と同様のステップ
└── 検証

Day 7: 最終確認
├── 全ライブラリ統合ビルド確認
├── CI全パス確認
└── ドキュメント最終レビュー
```

---

## リスクと対策

| リスク | 影響 | 対策 |
|--------|------|------|
| umitest/umimmio 構造が大きく異なる | Phase 3 遅延 | 事前に構造分析を徹底 |
| arm-embedded-xmake-repo 循環依存 | パッケージ解決失敗 | 慎重な on_load 設計 |
| CI 環境差異 | ローカルでは動くがCIで失敗 | 早期にCI検証 |
| xmake バージョン差異 | 機能非互換 | xmakever 明示、latest固定 |

---

## 次のアクション

この計画を承認いただければ、**Phase 1: umibench 完成** から即座に実装を開始します。

Phase 1 の最初のタスクは：
1. `lib/umibench/xmake.lua` の依存関係改善
2. `lib/umibench/tests/xmake.lua` のヘルパー関数使用修正

よろしければ実装を開始します。
