# arm-embedded-xmake-repo 改善 実行計画

## TL;DR

xmake標準機能（`format`, `check`, `test`, `show`）をそのまま活用し、arm-embedded パッケージは組み込み固有の機能（`flash`, `debug`, `emulator`）に集中する。重複実装を削減し、標準ワークフローに統合する。

---

## コンテキスト

- 既存計画書: `docs/ARM_EMBEDDED_XMAKE_REPO_IMPROVEMENT_PLAN.md`
- 標準フロー基準: `docs/XMAKE.md`
- arm-embedded repo: `.refs/arm-embedded-xmake-repo`
- 現行xmake設定: `xmake.lua`

---

## 重要な発見（2024年実装時に判明）

### xmake標準機能として既に存在するもの

| コマンド | xmake標準 | 備考 |
|----------|-----------|------|
| `xmake format` | ✅ xmake v2.7+ 標準 | clang-format対応 |
| `xmake check` | ✅ xmake v2.7+ 標準 | clang.tidyチェッカー含む |
| `xmake test` | ✅ xmake標準 | `set_group("test")`対応 |
| `xmake show` | ✅ xmake標準 | プロジェクト情報表示 |

**結論**: Task 1, 2, 10 はカスタム実装不要。xmake標準機能をそのまま使う。

---

## スコープ

### IN
- `xmake format` のパス重複バグ修正（プロジェクト側）
- 既存テストターゲットの `set_group("test")` 確認
- Flash/Debug/Emulator プラグインの動作確認
- ドキュメント更新

### OUT
- `xmake format` / `xmake check` のカスタム実装（xmake標準を使用）
- `xmake show` のカスタム拡張（xmake標準と衝突）
- 実際のビルド成果物やARMボードへの書き込み実行

---

## 依存関係

- 変更対象: `.refs/arm-embedded-xmake-repo` と プロジェクト `xmake.lua`
- 既存プラグイン/ルールを尊重し、後方互換を維持する

---

## 実行フェーズとタスク

### Wave 1 (Phase 1: 標準機能の活用)

- [x] **1) `xmake format` の利用確認**

**目的**: xmake標準の `format` プラグインを使用。

**状況**:
- xmake v2.7+ に `xmake format` が標準搭載
- clang-format を使用し、`.clang-format` ファイルを参照
- **プロジェクト側にパス重複バグあり** → Task 1b で修正

**受け入れ条件**:
- `xmake format` が正常に動作

**QA (コマンド)**:
```bash
xmake format --dry-run
```

---

- [x] **1b) プロジェクト側のパス重複バグ修正**

**目的**: `xmake format` のパス重複エラーを解消。

**問題**:
- `lib/umi/port/xmake.lua` で `add_headerfiles(path.join(port_dir, ...))` を使用
- `port_dir = os.scriptdir()` が絶対パスを返す
- `xmake format` がプロジェクトディレクトリを追加し、パスが重複

**作業**:
- `add_headerfiles()` に相対パスを使用するよう修正

**受け入れ条件**:
- `xmake format --dry-run` でエラーなし

---

- [x] **2) `xmake check` の利用確認**

**目的**: xmake標準の `check` プラグインを使用。

**状況**:
- xmake v2.7+ に `xmake check` が標準搭載
- `xmake check clang.tidy` で clang-tidy チェックが可能

**受け入れ条件**:
- `xmake check clang.tidy` が正常に動作

**QA (コマンド)**:
```bash
xmake check --list
xmake check clang.tidy
```

---

- [x] **3) `xmake test` 自動検出確認**

**目的**: `set_group("test")` で標準テスト検出を確認。

**状況**:
- xmake標準の `test` 機能で動作中
- テストターゲットは `set_group("test")` または命名規則で検出

**受け入れ条件**:
- `xmake test` で全テストが自動検出される

**QA (コマンド)**:
```bash
xmake test --list
xmake test
```

---

- [x] **4) テスト標準化のドキュメント更新**

**目的**: 新しいテスト追加ルールを周知。

**作業**:
- `set_group("test")` での自動検出ルールを明記
- `xmake test -g / -p` の使い方を追記

**受け入れ条件**:
- 新規テスト追加手順が明確に記載される

---

### Wave 2 (Phase 2: arm-embedded プラグイン)

- [x] **5) Flash プラグイン**

**目的**: pyOCD を使用した ARM フラッシュ書き込み。

**状況**:
- `.refs/arm-embedded-xmake-repo/packages/a/arm-embedded/plugins/flash/xmake.lua` に実装済み
- `xmake flash -t <target>` で使用可能

**受け入れ条件**:
- `xmake flash --help` で使い方が表示される
- ターゲット指定でフラッシュ書き込みが可能

**QA (コマンド)**:
```bash
xmake flash --help
```

---

- [x] **6) Debug / Debugger プラグイン**

**目的**: GDB/pyOCD を使用したデバッグ。

**状況**:
- `xmake debug` と `xmake debugger` が実装済み

**受け入れ条件**:
- `xmake debugger --help` で使い方が表示される

**QA (コマンド)**:
```bash
xmake debugger --help
```

---

- [x] **7) Emulator プラグイン族**

**目的**: Renode エミュレータの起動・テスト。

**状況**:
- `xmake emulator.run`, `xmake emulator.test`, `xmake emulator.robot` が実装済み
- プロジェクトの `renode`, `renode-test`, `robot` タスクに委譲

**受け入れ条件**:
- `xmake emulator --help` でサブコマンドが表示される

**QA (コマンド)**:
```bash
xmake emulator --help
```

---

### Wave 3 (Phase 3: クリーンアップ)

- [x] **8) カスタム show プラグイン削除**

**目的**: xmake標準の `show` と衝突するカスタムプラグインを削除。

**問題**:
- `.refs/arm-embedded-xmake-repo/packages/a/arm-embedded/plugins/show/` が存在
- xmake標準の `xmake show` と衝突

**作業**:
- カスタム `show` プラグインを削除
- 代わりに `xmake info` タスク（プロジェクト定義）を使用

**受け入れ条件**:
- `xmake show` がxmake標準の動作
- 組み込み情報は `xmake info` で表示

---

- [x] **9) Deploy プラグインの動作確認**

**目的**: WASM/Python モジュールのデプロイ。

**状況**:
- `xmake deploy -t <target>` が実装済み
- プロジェクトの `webhost`, `waveshaper-py` タスクに委譲

**受け入れ条件**:
- `xmake deploy --help` で使い方が表示される

**QA (コマンド)**:
```bash
xmake deploy --help
```

---

- [x] **10) ドキュメント統合**

**目的**: 全ドキュメントに新コマンド体系を反映。

**作業**:
- xmake標準コマンドとarm-embeddedプラグインの関係を明記
- 旧タスクは非推奨と明記

**受け入れ条件**:
- 主要ドキュメントが新コマンド表記に統一

---

## コマンド対応表

| 機能 | 新コマンド | 提供元 | 旧コマンド/代替 |
|------|------------|--------|-----------------|
| コードフォーマット | `xmake format` | xmake標準 | `xmake coding format` |
| 静的解析 | `xmake check clang.tidy` | xmake標準 | `xmake coding check` |
| テスト実行 | `xmake test` | xmake標準 | - |
| プロジェクト情報 | `xmake show` | xmake標準 | - |
| 組み込み情報 | `xmake info` | プロジェクト定義 | - |
| フラッシュ書き込み | `xmake flash -t <target>` | arm-embedded | `xmake flash-kernel` 等 |
| デバッグ | `xmake debugger` | arm-embedded | - |
| エミュレータ | `xmake emulator.run` | arm-embedded | `xmake renode` |
| エミュレータテスト | `xmake emulator.test` | arm-embedded | `xmake renode-test` |
| Robot テスト | `xmake emulator.robot` | arm-embedded | `xmake robot` |
| WASM デプロイ | `xmake deploy.webhost` | arm-embedded | `xmake webhost` |

---

## 進行ルール

- 後方互換性を保つ（旧コマンドはエイリアスとして残す）
- Phaseごとに確認・段階的に移行
- 実行前に `xmake --version` と環境依存ツールの存在を確認

---

## 最終確認（Success Criteria）

- xmake標準の `format/check/test/show` が正常動作
- arm-embedded の `flash/debug/emulator` が正常動作
- 旧タスクは非推奨として残り、移行方法がドキュメント化されている
- ドキュメントと実装が一致している
