# VSCode + Cortex-Debug デバッグ設定

このドキュメントは、UMI リポジトリで **VSCode の cortex-debug 拡張**を使うための設定生成手順と、生成される `launch.json` の内容を確認するためのものです。

本リポジトリには **2 系統の生成経路**があります。両方とも共通モジュール (`launch_generator.lua`) を使用し、同じ設定形式を生成します。

- **`xmake debugger --vscode`**: `xmake debugger -t <target> --vscode` で launch.json を生成（デバッガプラグイン経由）
- **`embedded.vscode` ルール**: `xmake build` 後に `.vscode/settings.json` / `tasks.json` / `launch.json` を自動生成

## 前提

- VSCode 拡張: `marus25.cortex-debug`
- デバッグバックエンド: `pyocd` または `openocd`
- (推奨) ARM ツールチェーン: `arm-none-eabi-gdb` を含む gcc-arm

## データベース駆動設定

設定生成は **JSON データベース**から情報を取得します。

### 参照データベース

| データベース | 内容 |
|-------------|------|
| `mcu-database.json` | `device`, `svd` |
| `flash-targets-v2.json` | `openocd_target`, `openocd_interface`, `pyocd_target` |

### MCU ファミリー解決

MCU 名から自動的にファミリーを検出し、対応する OpenOCD target config を選択します。

| MCU | ファミリー | OpenOCD Target |
|-----|-----------|----------------|
| `stm32f407vg` | `stm32f4` | `stm32f4x.cfg` |
| `stm32f103c8` | `stm32f1` | `stm32f1x.cfg` |
| `stm32h743zi` | `stm32h7` | `stm32h7x.cfg` |

## 生成経路 1: `xmake debugger --vscode`

### コマンド

```bash
xmake debugger -t <target> --vscode
```

### 生成される `launch.json`

この経路は **単一の config を生成**します。内容は共通モジュール (`launch_generator.lua`) によって生成されます。

- `name`: `Debug <target>`
- `type`: `cortex-debug`
- `request`: `launch`
- `servertype`: `--backend` オプションまたは `auto` 検出結果
- `executable`: ターゲットの実際の出力バイナリ
- `device`: MCU データベースから取得した device 名
- `svdFile`: MCU データベースに `svd` がある場合 `.vscode/<svd>`
- `armToolchainPath`: xmake パッケージ内の `gcc-arm` から自動検出
- `runToEntryPoint`: `main`

### MCU データベースの参照順

1. `mcu-local.json` (プロジェクトルート)
2. `mcu-database.json` (arm-embedded パッケージ側)

### SVD ファイルの扱い

`svdFile` が設定されても **自動ダウンロードは行いません**。`.vscode/` 配下に SVD を配置してください。

### 補足

- `--backend` で `pyocd` / `openocd` / `auto` を指定できます
- `--port` で GDB サーバーポートを指定できます
- `--interactive` は VSCode 内では禁止されます（外部ターミナル専用）

## 生成経路 2: `embedded.vscode` ルール

### 生成タイミング

`embedded.vscode` ルールは **build 後 (`after_build`) に自動生成**します。

### 生成されるファイル

- `.vscode/settings.json`
- `.vscode/tasks.json`
- `.vscode/launch.json`

### `tasks.json` の managed タスク

既存の `tasks.json` がある場合は **managed タスクのみ上書き**し、ユーザー定義のタスクは保持します。

- `Build (Release)`
- `Build (Debug)`
- `Clean`
- `Build & Flash`

### `launch.json` の managed config

既存の `launch.json` がある場合は **managed config のみ上書き**し、ユーザー定義の config は保持します。

以下の config が生成されます（ターゲット名 `<target>` に応じて）：

- `<target> (OPENOCD)` - OpenOCD バックエンド
- `<target> (PYOCD)` - PyOCD バックエンド
- `<target> (RTT)` - RTT 対応版（OpenOCD）

### 設定内容

| フィールド | 内容 |
|-----------|------|
| `type` | `cortex-debug` |
| `request` | `launch` |
| `servertype` | `openocd` または `pyocd` |
| `cwd` | `${workspaceFolder}` |
| `executable` | `build/<target>/debug/<target>` |
| `runToEntryPoint` | `main` |
| `preLaunchTask` | `Build (Debug)` |
| `configFiles` | (OpenOCD のみ) `interface/stlink.cfg`, `target/<family>x.cfg` |
| `device` | MCU データベースから取得 (`STM32F407VG` など) |
| `svdFile` | (あれば) `.vscode/<mcu>.svd` |

### RTT 設定

`<target> (RTT)` config には以下が含まれます：

- `rttConfig.enabled = true`
- `rttConfig.address = 0x20000000`
- `rttConfig.searchSize = 131072`
- `rttConfig.searchId = RT MONITOR`

## 使い分けの目安

| 用途 | 推奨コマンド |
|------|-------------|
| 素早く launch.json だけ作りたい | `xmake debugger -t <target> --vscode` |
| VSCode タスク/設定も一式生成 | `embedded.vscode` ルール（`xmake build`） |
| デバッグサーバーも起動 | `xmake debugger -t <target>` |

## 共通モジュール

両経路は同じ Lua モジュールを使用します：

```
rules/embedded/modules/launch_generator.lua
```

このモジュールは以下を提供します：

- `get_mcu_debug_info(mcu, database_dir)` - MCU 情報をデータベースから取得
- `generate_launch_json(targets, database_dir, options)` - launch.json 生成
- `format_launch_json(launch_data)` - JSON フォーマット
- `merge_with_existing(new_launch, file_path, managed_names)` - 既存設定とのマージ

## トラブルシューティング

### デバッガが応答しない

```bash
xmake debugger --status
xmake debugger --kill
```

必要に応じて残留プロセスも確認します。

```bash
pgrep -fl pyocd
pgrep -fl openocd
kill <pid>
```

### ポートが使用中

```bash
xmake debugger -t <target> -p 3334 --vscode
```

### OpenOCD config が見つからない

`flash-targets-v2.json` の `mcu_families` に対応ファミリーが登録されているか確認してください。

## 既存のサンプル

リポジトリ直下にある `.vscode/launch.json` は、`pyocd` を使った最小例です。
