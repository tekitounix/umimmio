# Clang-Tidy セットアップガイド

## 問題: arm-embedded ツールチェーンとの互換性

clang-arm 21.1.0/21.1.1 の multilib.yaml が clang-tidy 20.x と互換性がありません。

## 解決策（選択肢）

### 方法1: clang-tidy をアップデート（推奨）

```bash
# macOS
brew upgrade llvm

# Ubuntu/Debian
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 20
```

LLVM 21.x 以降では multilib.yaml の IncludeDirs がサポートされています。

### 方法2: ホストコンパイラでチェック

組み込みコードを `--target=arm-none-eabi` なしでチェック（完全ではないが有用）：

```bash
clang-tidy lib/umibench/**/*.cc -- -Ilib -std=c++23
```

### 方法3: CI でのみ実行

ローカルではスキップし、CI 環境（Docker）で実行：

```yaml
# .github/workflows/ci.yml
- name: Static Analysis
  uses: docker://llvm:21
  with:
    args: xmake check clang.tidy
```

### 方法4: 別の静的解析ツールを使用

cppcheck などの代替ツール：

```bash
xmake check  # デフォルトチェッカーのみ
```

## 推奨

- **開発時**: 方法4（xmake check のみ）
- **PR前**: 方法1（clang-tidy アップデート）
- **CI**: 方法3（Docker 環境）
