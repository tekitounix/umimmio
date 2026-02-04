# XMake ガイド

UMI プロジェクトにおける xmake の基本的な使い方をまとめます。

## 基本フロー

```bash
# 1) 設定
xmake f

# 2) ビルド
xmake build

# 3) 実行
xmake run <target>

# 4) テスト
xmake test

# 5) クリーン
xmake clean

# 6) 情報表示
xmake info

> 補足: xmake標準の `xmake show` も利用可能です。
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
xmake check                     # デフォルトチェッカー
xmake check clang.tidy          # clang-tidy によるチェック
xmake check --list              # 利用可能なチェッカー一覧
xmake check --info=clang.tidy   # チェッカー詳細情報
```

### デバッグビルド

```bash
# デバッグモードでビルド（xmake標準機能）
xmake f -m debug && xmake

# リリースモードに戻す
xmake f -m release && xmake
```

## 組み込み開発（arm-embedded パッケージ）

### Flash

```bash
# ターゲットを書き込み
xmake flash -t <target>

# 例
xmake flash -t stm32f4_kernel
xmake flash -t synth_app -a 0x08060000

# オプション
xmake flash --help              # 全オプションを表示

# ツール状態/接続プローブ確認
xmake flash.status
xmake flash.probes
```

### デバッグ

```bash
# GDBデバッガー起動
xmake debugger -t <target>
xmake debugger --help           # オプション一覧
```

### エミュレータ

```bash
# Renode対話セッション
xmake emulator.run

# 自動テスト（Robot Framework）
xmake emulator.test

# ヘルプ表示（--help は未対応）
xmake emulator
```

## デプロイ（arm-embedded パッケージ）

```bash
# ビルド成果物を指定ディレクトリにコピー
xmake deploy -t <target> [--dest <dir>]

# WASMホストのデプロイ（ショートカット）
xmake deploy.webhost

# デプロイしてローカルサーバー起動
xmake deploy.serve

> 注意: `deploy.webhost` / `deploy.serve` は `--help` 未対応です。
> Emscripten が無い場合は `webhost_sim.wasm` が生成されず失敗します。
```

## テスト

プロジェクトの `xmake test` タスクは以下のホストテストを実行します：

```bash
xmake test                      # 全ホストテストを実行
```

### グループ指定実行（xmake標準機能）

```bash
# 特定グループのみ実行
xmake test -g tests/umidi

# パターンマッチ
xmake test -g "tests/*"
```

## コマンド対応表

| 機能 | コマンド | 提供元 |
|------|----------|--------|
| フォーマット | `xmake format` | xmake標準 |
| 静的解析 | `xmake check clang.tidy` | xmake標準 |
| デバッグビルド | `xmake f -m debug && xmake` | xmake標準 |
| テスト | `xmake test` | プロジェクト定義 |
| プロジェクト情報 | `xmake info` | プロジェクト定義 |
| フラッシュ | `xmake flash -t <target>` | arm-embedded |
| GDBデバッガー | `xmake debugger -t <target>` | arm-embedded |
| エミュレータ | `xmake emulator.*` | arm-embedded |
| デプロイ | `xmake deploy -t <target>` | arm-embedded |

---

## 全コマンド・プラグイン 完全リスト

> 以下は `.refs/arm-embedded-xmake-repo` およびプロジェクトの xmake.lua から抽出した全コマンド一覧です。

### xmake標準アクション

| コマンド | 説明 |
|----------|------|
| `xmake build` / `xmake b` | ターゲットをビルド |
| `xmake clean` / `xmake c` | バイナリと一時ファイルを削除 |
| `xmake config` / `xmake f` | プロジェクトを設定 |
| `xmake create` | 新規プロジェクトを作成 |
| `xmake global` / `xmake g` | xmakeのグローバルオプション設定 |
| `xmake install` / `xmake i` | ターゲットバイナリをパッケージ化・インストール |
| `xmake package` / `xmake p` | ターゲットをパッケージ化 |
| `xmake require` / `xmake q` | 必要なパッケージをインストール・更新 |
| `xmake run` / `xmake r` | プロジェクトターゲットを実行 |
| `xmake uninstall` / `xmake u` | プロジェクトバイナリをアンインストール |
| `xmake update` | xmakeプログラムを更新・削除 |

### xmake標準プラグイン

| コマンド | 説明 |
|----------|------|
| `xmake check` | プロジェクトソースコードと設定をチェック |
| `xmake doxygen` | Doxygenドキュメントを生成 |
| `xmake format` | 現在のプロジェクトをフォーマット |
| `xmake lua` / `xmake l` | Luaスクリプトを実行 |
| `xmake macro` / `xmake m` | 指定マクロを実行 |
| `xmake pack` | バイナリインストールパッケージを作成 |
| `xmake plugin` | xmakeプラグインを管理 |
| `xmake project` | プロジェクトファイルを生成 |
| `xmake repo` | パッケージリポジトリを管理 |
| `xmake show` | プロジェクト情報を表示 |
| `xmake watch` | プロジェクトディレクトリを監視してコマンド実行 |

### arm-embedded パッケージ: Flash プラグイン

| コマンド | 説明 | オプション |
|----------|------|------------|
| `xmake flash` | ARM組み込みターゲットを書き込み | `-t <target>` ターゲット指定<br>`-d <device>` MCU名（例: stm32f407vg）<br>`-b <backend>` バックエンド [pyocd\|openocd\|auto]<br>`-a <address>` ベースアドレス（bin用）<br>`-f <file>` 書き込みファイル指定<br>`--format <fmt>` ファイル形式 [elf\|bin\|hex]<br>`-e <mode>` 消去モード [chip\|sector\|none]<br>`-v` 書き込み後検証<br>`-r <mode>` リセットモード [hw\|sw\|none]<br>`-p <probe>` プローブUID/シリアル<br>`-s <speed>` 通信速度（例: 4M）<br>`--connect <mode>` 接続モード<br>`--unlock` 読み出し保護解除<br>`-y` 確認プロンプト自動承認<br>`--dry-run` コマンド表示のみ実行なし<br>`--interface <cfg>` OpenOCDインターフェース設定 |
| `xmake flash.probes` | 接続されているデバッグプローブを一覧表示 | - |
| `xmake flash.status` | フラッシュツールの状態を表示 | - |

### arm-embedded パッケージ: Debugger プラグイン

| コマンド | 説明 | オプション |
|----------|------|------------|
| `xmake debugger` | 組み込みまたはホストターゲットをデバッグ | `-t <target>` ターゲット指定<br>`-b <backend>` バックエンド [pyocd\|openocd\|jlink\|auto]<br>`-p <port>` GDBサーバーポート（デフォ: 3333）<br>`--server-only` GDBサーバーだけ起動<br>`--attach` 実行中サーバーに接続<br>`--kill` GDBサーバーを停止<br>`--status` サーバー状態を表示<br>`-i <file>` GDB初期化ファイル<br>`--break <sym>` 初期ブレークポイント記号（デフォ: main）<br>`--rtt` RTTを有効化<br>`--rtt-port <port>` RTT TCPポート（デフォ: 19021）<br>`--tui` GDB TUIモード使用<br>`--vscode` VSCode launch.json生成<br>`--kill-on-exit` GDB終了時にサーバーを停止 |
| `xmake debugger.cleanup` | 孤立したGDBサーバープロセスを全て終了 | - |

### arm-embedded パッケージ: Emulator プラグイン

| コマンド | 説明 | オプション |
|----------|------|------------|
| `xmake emulator` | エミュレータのヘルプと状態を表示 | - |
| `xmake emulator.run` | Renodeエミュレータを起動 | `-t <target>` エミュレートするターゲット<br>`-s <file>` Renodeスクリプト（.resc）<br>`-p <file>` プラットフォーム記述（.repl）<br>`--headless` ヘッドレスモード（GUIなし）<br>`--gdb` GDBサーバーを有効化<br>`--gdb-port <port>` GDBポート（デフォ: 3333）<br>`--generate` .rescスクリプトを生成 |
| `xmake emulator.test` | Renode Robot Frameworkテストを実行 | `-r <file>` Robot Frameworkテストファイル<br>`-o <dir>` 結果出力ディレクトリ<br>`-t <sec>` テストタイムアウト（秒） |

### arm-embedded パッケージ: Deploy プラグイン

| コマンド | 説明 | オプション |
|----------|------|------------|
| `xmake deploy` | ビルド成果物を指定ディレクトリにコピー | `-t <target>` デプロイするターゲット<br>`-d <dir>` 出力先ディレクトリ |
| `xmake deploy.webhost` | WASMヘッドレスホストをデプロイ | - |
| `xmake deploy.serve` | デプロイしてローカルサーバーを起動 | - |

### arm-embedded パッケージ: Serve プラグイン

| コマンド | 説明 | オプション |
|----------|------|------------|
| `xmake serve` | Web/WASMコンテンツ用HTTPサーバーを起動 | `-p <port>` サーバーポート（デフォ: 8080）<br>`-h <host>` サーバーホスト/バインド（デフォ: localhost）<br>`-d <dir>` 配信するディレクトリ<br>`-t <target>` ビルドして配信するWASMターゲット<br>`--open` ブラウザを自動で開く<br>`--build` 配信前に強制リビルド<br>`--cors` CORSヘッダーを有効化 |
| `xmake serve.rtt` | RTTログビューアーWebインターフェースを起動 | `-p <port>` HTTPサーバーポート（デフォ: 8081）<br>`--rtt-port <port>` RTT TCPポート（デフォ: 19021）<br>`--target <target>` RTTチャンネル設定用ターゲット |

### プロジェクト定義タスク

| コマンド | 説明 |
|----------|------|
| `xmake test` | プロジェクトテストを実行（ホストテスト13個） |
| `xmake fs-check` | FSテスト、Renodeベンチマーク、ARMサイズ比較を実行 |
| `xmake serve` | HTTPサーバーを起動（web/WASM配信） |
| `xmake serve.rtt` | RTTログビューアーWebインターフェース |
| `xmake info` | プロジェクト情報を表示（無効化されている可能性あり） |

### 非推奨タスク

| コマンド | 説明 | 代替方法 |
|----------|------|----------|
| `xmake flash-h7-app` | H7シンセアプリをQSPIに書き込み | `xmake flash -t daisy_pod_synth_h7 -a 0x90000000` |
| `xmake flash-h7-kernel` | H7カーネルを書き込み | `xmake flash -t daisy_pod_kernel` |
| `xmake flash-kernel` | STM32F4カーネルを書き込み | `xmake flash -t stm32f4_kernel` |
| `xmake flash-synth-app` | シンセアプリをAPP_FLASHに書き込み | `xmake flash -t synth_app -a 0x08060000` |
| `xmake flash-synth-h7` | H7シンセアプリをQSPIに書き込み | `xmake flash -t daisy_pod_synth_h7 -a 0x90000000` |

---

## コマンド実行状況まとめ（調査時点）

### ✅ 完了済み（非対話実行成功）

| コマンド | 結果 |
|----------|------|
| `xmake test` | 13テスト実行 |
| `xmake build` | 成功 |
| `xmake clean` | 完了 |
| `xmake show` | 完了 |
| `xmake build -g firmware` | 成功 |
| `xmake build -g tests/fs\|port\|boot\|dsp\|usb\|umidi\|umimock` | 成功 |
| `xmake build -g wasm` | 成功 |
| `xmake deploy.webhost` | 成功 |

### ⚠️ ヘルプのみ確認（実行未実施）

| コマンド | 状況 |
|----------|------|
| `xmake flash --help` | ヘルプ確認のみ |
| `xmake debugger --help` | ヘルプ確認のみ |
| `xmake emulator` | ヘルプのみ |

### ❌ 未完了/未実施

| カテゴリ | コマンド | 未実施理由 |
|----------|----------|------------|
| **情報** | `xmake info` | このxmakeでは無効 |
| **実行** | `xmake run <target>` | 対話型になるため |
| **エミュレータ** | `xmake emulator.run` | Renode対話/外部依存 |
| **エミュレータ** | `xmake emulator.test` | Robot Framework対話 |
| **デプロイ** | `xmake deploy.serve` | サーバ常駐 |
| **フラッシュ** | `xmake flash` | ハード/プローブ依存 |
| **デバッグ** | `xmake debugger` | ハード/プローブ依存 |

### 🔧 実行可能な追加候補（非対話）

| コマンド | 説明 | 実行可能性 |
|----------|------|------------|
| `xmake check` | プロジェクトチェック | ✅ 非対話 |
| `xmake format` | コードフォーマット | ✅ 非対話 |
| `xmake pack` | パッケージング | ✅ 非対話 |
| `xmake flash.probes` | プローブ一覧 | ⚠️ ハード依存 |
| `xmake flash.status` | フラッシュステータス | ⚠️ ツール依存 |
| `xmake serve` | HTTPサーバー | ❌ 常駐 |
| `xmake debugger.cleanup` | GDBサーバー cleanup | ✅ 非対話 |
| `xmake project -k vsxmake` | VSプロジェクト生成 | ✅ 非対話 |
| `xmake project -k cmake` | CMake生成 | ✅ 非対話 |
| `xmake project -k compile_commands` | compile_commands.json生成 | ✅ 非対話 |

---

*最終更新: 2026-02-05*
*対象リポジトリ: `.refs/arm-embedded-xmake-repo` (packages/a/arm-embedded/plugins/)*
