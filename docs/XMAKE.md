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

