# clang-arm multilib.yaml Workaround

## 問題

clang-arm 21.1.0/21.1.1 の multilib.yaml に含まれる `IncludeDirs` キーが、clang-tidy 20.x で認識されない。

## 解決策

パッケージインストール時に 2 つのバージョンが作成される：
- `multilib.yaml` - ビルド用（元のまま）
- `multilib.yaml.tidy` - clang-tidy 用（patched）

## 一時的な切り替え手順

### clang-tidy チェック前

```bash
# multilib.yaml を patched バージョンに切り替え
MV_FILE="$HOME/.xmake/packages/c/clang-arm/21.1.1/*/lib/clang-runtimes/multilib.yaml"
cp "${MV_FILE}.tidy" "$MV_FILE"
```

### clang-tidy 実行

```bash
xmake check clang.tidy
```

### 元に戻す（ビルド前に必ず実行）

```bash
# 元の multilib.yaml を復元
MV_FILE="$HOME/.xmake/packages/c/clang-arm/21.1.1/*/lib/clang-runtimes/multilib.yaml"
cp "${MV_FILE}.orig" "$MV_FILE" 2>/dev/null || xmake require --force clang-arm@21.1.1
```

## 恒久対策

- arm-embedded パッケージに自動切り替え機能を追加予定
- または clang-arm 22.x での修正を待つ
