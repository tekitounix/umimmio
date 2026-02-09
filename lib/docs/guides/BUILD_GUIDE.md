# ビルドガイド

UMI プロジェクトにおける xmake の基本的な使い方と全コマンドリファレンスです。

## 基本フロー

```bash
# 1) 設定
xmake f

# 2) ビルド
xmake build

# 3) 実行（対話型）
xmake run <target>

# 4) テスト
xmake test

# 5) クリーン
xmake clean

# 6) 情報表示
xmake show
```

## コード品質（xmake標準機能）

### フォーマット

```bash
xmake format                    # 全ターゲットをフォーマット
xmake format -n                 # dry-run（変更なし確認のみ）
xmake format -e                 # エラーとして報告（CI向け）
xmake format -t <target>        # 特定ターゲットのみ
xmake format -g test            # testグループのみ
xmake format -f "src/*.cc"      # ファイル指定
xmake format --create -s LLVM   # .clang-format生成
```

### 静的解析チェック

```bash
xmake check                     # xmake.lua の API チェック
xmake check --list              # 利用可能なチェッカー一覧
xmake check --info=clang.tidy   # チェッカー詳細情報
```

#### clang-tidy

```bash
# ファイル指定（推奨）
xmake check clang.tidy -f lib/umibench/examples/instruction_bench.cc
xmake check clang.tidy -f 'lib/**/*.cc'

# 有効なチェック一覧
xmake check clang.tidy -l

# 自動修正
xmake check clang.tidy -f <file> --fix
```

**注意**: ターゲット指定 (`xmake check clang.tidy <target>`) は xmake v3.0.x では正常に動作しません。`-f` でファイルパターンを指定してください。

### デバッグビルド

```bash
# デバッグモードでビルド
xmake f -m debug && xmake

# リリースモードに戻す
xmake f -m release && xmake
```

---

## 組み込み開発（arm-embedded パッケージ）

arm-embedded パッケージは、ARM Cortex-M マイクロコントローラの開発に必要なビルドルール、プラグイン、IDE 統合を提供します。

### ルール一覧

#### `embedded` — コアビルドルール

ARM Cortex-M ターゲット向けのクロスコンパイルを自動構成します。

```lua
target("my_firmware")
    add_rules("embedded")
    set_values("embedded.mcu", "stm32f407vg")
    add_files("src/*.cc")
```

**設定オプション** (`set_values()` で指定):

| オプション | 説明 | デフォルト |
|-----------|------|----------|
| `embedded.mcu` | MCU名（例: stm32f407vg, stm32h533re） | 必須 |
| `embedded.toolchain` | `gcc-arm` / `clang-arm` | `clang-arm` |
| `embedded.optimize` | `size`/`speed`/`balanced`/`debug`/`none`/-O0〜-Ofast | `size` |
| `embedded.debug_level` | `minimal`/`standard`/`maximum`/-g/-g1〜-ggdb3 | `minimal` |
| `embedded.lto` | `none`/`thin`/`full` | `none` |
| `embedded.c_standard` | c99〜c23, gnu99〜gnu23 | `c23` |
| `embedded.cxx_standard` | c++98〜c++23 | `c++23` |
| `embedded.outputs` | 出力形式の配列 | `{"elf","hex","bin","map"}` |
| `embedded.linker_script` | カスタムリンカスクリプトパス | 汎用 `common.ld` |
| `embedded.semihosting` | セミホスティング有効化 | `false` |
| `embedded.flash_on_run` | `xmake run` 時に自動フラッシュ | `true` |
| `embedded.probe` | デバッグプローブ UID | なし |

**自動処理:**
- MCU DB に基づくコンパイラフラグ（Cortex-M コア、FPU、thumb モード）
- メモリレイアウトシンボル (`__flash_size`, `__ram_size` 等) をリンカに注入
- ELF/HEX/BIN/MAP ファイル自動生成
- ビルド後のメモリ使用量表示（90%超で警告）
- ターゲットグループの自動設定（`embedded/clang-arm` or `embedded/gcc-arm`）

**MCU データベース:**

組み込みルールの MCU 情報は JSON データベースで管理されています:

| ファイル | 内容 |
|---------|------|
| `mcu-database.json` | MCU 定義（コア、Flash/RAM サイズ、アドレス） |
| `cortex-m.json` | Cortex-M コア定義（M0〜M85、FPU、LLVM ターゲット） |
| `build-options.json` | 最適化レベル、デバッグ情報、LTO プリセット |
| `toolchain-configs.json` | ツールチェーンのパッケージパス、リンカシンボル |

プロジェクトルートに `mcu-local.json` を配置すると、パッケージの MCU DB を上書きできます。

#### `embedded.vscode` — VSCode 統合ルール

ビルド後に VSCode の設定ファイルを自動生成・更新します。`embedded` ルールの依存ルールとして自動的に適用されます。

**自動生成ファイル:**

| ファイル | 内容 |
|---------|------|
| `.vscode/settings.json` | clangd 引数（query-driver、clang-tidy 等） |
| `.vscode/tasks.json` | Build (Release/Debug)、Clean、Build & Flash タスク |
| `.vscode/launch.json` | Debug Embedded、RTT Debug (OpenOCD) 構成 |

- ユーザー定義のタスク・構成は保持されます（管理対象は名前で識別）
- `--query-driver` はツールチェーンのインストールパスから動的に解決
- RTT の開始アドレスは MCU DB の `ram_origin` から取得

#### `embedded.compdb` — マルチプラットフォーム compile_commands.json

ビルド後に `compile_commands.json` をプラットフォーム別に分割し、clangd の PathMatch 機能で使用します。

```lua
-- xmake.lua（プロジェクトルート）
add_rules("embedded.compdb")
```

**出力:**
```
build/compdb/
├── host/compile_commands.json   # ホストコンパイラ
├── arm/compile_commands.json    # ARM クロスコンパイラ（gcc-arm + clang-arm）
└── wasm/compile_commands.json   # Emscripten
```

**分類ロジック:**
- `arm-none-eabi` または `.xmake/packages/` を含む → arm
- `emcc` を含む → wasm
- その他 → host

`.clangd` の `PathMatch` と組み合わせて使用します:

```yaml
# .clangd
CompileFlags:
  CompilationDatabase: build/compdb/host

---
If:
  PathMatch: .*/(platforms|port)/arm/.*
CompileFlags:
  CompilationDatabase: build/compdb/arm
```

ビルド時に自動実行されます。手動で再生成する場合は `xmake compdb` を使用してください。ソースファイルやビルド設定に変更がない場合はスキップされます（オーバーヘッド 0）。

#### `host.test` — ホストテストルール

ホスト上で実行するユニットテスト用ルールです。

```lua
target("test_mylib")
    add_rules("host.test")
    add_files("tests/*.cc")
```

| オプション | 説明 | デフォルト |
|-----------|------|----------|
| `test.runner` | gtest/catch2/unity | なし |
| `test.coverage` | カバレッジ収集 | `false` |
| `test.coverage_tool` | gcov/llvm-cov | なし |
| `test.sanitizers` | サニタイザ配列 | なし |

#### `embedded.test` — 組み込みテストルール

ハードウェアまたはエミュレータ上でテストを実行します。

| オプション | 説明 | デフォルト |
|-----------|------|----------|
| `embedded.test_mode` | `hardware`/`qemu`/`renode` | `hardware` |
| `embedded.test_framework` | `unity`/`minunit` | なし |
| `embedded.test_timeout` | タイムアウト（秒） | `30` |
| `embedded.test_output` | `semihosting`/`rtt`/`uart` | なし |

---

### プラグイン（コマンド）

#### Flash

```bash
# ターゲットを書き込み
xmake flash -t <target>
xmake flash -t stm32f4_kernel
xmake flash -t synth_app -a 0x08060000

# オプション
xmake flash -t <target> -e           # チップ消去後に書き込み
xmake flash -t <target> --probe <uid> # プローブ指定
xmake flash -t <target> -y           # CI/CD モード（確認なし）

# ツール状態/接続プローブ確認
xmake flash.status
xmake flash.probes
```

PyOCD を使用。デバイスパックの自動インストールに対応。ソースに変更があれば自動リビルドします。

#### compdb

```bash
# マルチプラットフォーム compile_commands.json を手動生成
xmake compdb
```

`embedded.compdb` ルール（ビルド時自動実行）の手動版です。

#### test

```bash
# テスト検出・実行
xmake test
xmake test -g host              # グループでフィルタ
xmake test -p "test_umibench*"  # パターンでフィルタ
```

---

### 開発ワークフロー

#### パッケージ同期（開発中）

arm-embedded パッケージのソースを直接変更しながら開発する場合:

```bash
# ローカルソースを ~/.xmake に直接同期（即座に反映）
xmake dev-sync

# リリース/CI では正式なパッケージインストールを使用
xmake require --force arm-embedded
```

`dev-sync` は `xmake-repo/synthernet/` 配下のルールとプラグインを `~/.xmake/` にコピーします。ファイル追加・削除も自動検出され、レガシーディレクトリのクリーンアップも行われます。

---

## テスト

```bash
# 全テスト実行（add_tests() 登録ターゲット）
xmake test

# パターンで絞り込み（umibench の例）
xmake test "test_umibench/*"
xmake test "test_umibench_compile_fail/*"
xmake test "umibench_wasm/*"
```

## ターゲットグルーピング

embedded ルールと host.test ルールは、ターゲットグループを**自動設定**します。

### 自動設定されるグループ

| ルール | 自動グループ | 例 |
|--------|-------------|-----|
| `embedded` (clang-arm) | `embedded/clang-arm` | umibench_stm32f4_renode |
| `embedded` (gcc-arm) | `embedded/gcc-arm` | umibench_stm32f4_renode_gcc |
| `host.test` | `host/test` | test_umibench |

### グループ指定でのビルド

```bash
# ツールチェーン別
xmake build -g "embedded/clang-arm"   # clang-arm ターゲットのみ
xmake build -g "embedded/gcc-arm"     # gcc-arm ターゲットのみ
xmake build -g "embedded/*"           # 全組み込みターゲット
xmake build -g "host/*"               # 全ホストターゲット

# 複数グループ
xmake build -g "embedded/clang-arm" -g "host/test"
```

### カスタムグループ

明示的に `set_group()` を設定すると、自動設定はオーバーライドされます：

```lua
target("my_target")
    add_rules("embedded")
    set_group("custom/group")  -- 自動設定より優先
```

---

## 全コマンドリファレンス

### xmake標準アクション

| コマンド | 説明 | タイプ |
|----------|------|--------|
| `xmake build` / `xmake b` | ターゲットをビルド | 非対話型 |
| `xmake clean` / `xmake c` | バイナリと一時ファイルを削除 | 非対話型 |
| `xmake config` / `xmake f` | プロジェクトを設定 | 非対話型 |
| `xmake create` | 新規プロジェクトを作成 | 非対話型 |
| `xmake global` / `xmake g` | xmakeのグローバルオプション設定 | 非対話型 |
| `xmake install` / `xmake i` | ターゲットバイナリをパッケージ化・インストール | 非対話型 |
| `xmake package` / `xmake p` | ターゲットをパッケージ化 | 非対話型 |
| `xmake require` / `xmake q` | 必要なパッケージをインストール・更新 | 非対話型 |
| `xmake run` / `xmake r` | プロジェクトターゲットを実行 | **対話型** |
| `xmake test` | プロジェクトテストを実行 | 非対話型 |
| `xmake uninstall` / `xmake u` | プロジェクトバイナリをアンインストール | 非対話型 |
| `xmake update` | xmakeプログラムを更新・削除 | 非対話型 |

### xmake標準プラグイン

| コマンド | 説明 | タイプ |
|----------|------|--------|
| `xmake check` | プロジェクトソースコードと設定をチェック | 非対話型 |
| `xmake doxygen` | Doxygenドキュメントを生成 | 非対話型 |
| `xmake format` | 現在のプロジェクトをフォーマット | 非対話型 |
| `xmake lua` / `xmake l` | Luaスクリプトを実行 | 非対話型 |
| `xmake macro` / `xmake m` | 指定マクロを実行 | 非対話型 |
| `xmake pack` | バイナリインストールパッケージを作成 | 非対話型 |
| `xmake plugin` | xmakeプラグインを管理 | 非対話型 |
| `xmake project` | プロジェクトファイルを生成 | 非対話型 |
| `xmake repo` | パッケージリポジトリを管理 | 非対話型 |
| `xmake show` | プロジェクト情報を表示 | 非対話型 |
| `xmake watch` | プロジェクトディレクトリを監視してコマンド実行 | **対話型** |

### arm-embedded パッケージ

| コマンド | 説明 | タイプ | 主なオプション |
|----------|------|--------|----------------|
| `xmake flash` | ARM ターゲットを書き込み | **対話型**※ | `-t`, `-d`, `-e`, `-r`, `--probe`, `-y` |
| `xmake flash.probes` | デバッグプローブ一覧 | 非対話型 | - |
| `xmake flash.status` | フラッシュツール状態 | 非対話型 | - |
| `xmake compdb` | マルチプラットフォーム compdb 生成 | 非対話型 | - |
| `xmake test` | テスト検出・実行 | 非対話型 | `-g`, `-p`, `-v` |

※`-y` 付与時は非対話型

### arm-embedded ルール（自動適用）

| ルール | 種別 | 説明 |
|--------|------|------|
| `embedded` | target | ARM Cortex-M クロスコンパイル |
| `embedded.vscode` | project | VSCode 設定自動生成 |
| `embedded.compdb` | project | compile_commands.json プラットフォーム分割 |
| `host.test` | target | ホストユニットテスト |
| `embedded.test` | target | 組み込みテスト（HW/QEMU/Renode） |

### UMI カスタムタスク

| コマンド | 説明 | タイプ | 主なオプション |
|----------|------|--------|----------------|
| `xmake release` | バージョン更新・アーカイブ生成・タグ作成 | 非対話型 | `--ver=X.Y.Z`, `--libs=name`, `--dry-run` |
| `xmake dev-sync` | ローカルパッケージソースを ~/.xmake に同期 | 非対話型 | - |

詳細は [Release ガイド](RELEASE_GUIDE.md) を参照。

### 非推奨タスク

| コマンド | 代替方法 |
|----------|----------|
| `xmake flash-kernel` | `xmake flash -t stm32f4_kernel` |
| `xmake flash-synth-app` | `xmake flash -t synth_app -a 0x08060000` |

---

*最終更新: 2026-02-07*
