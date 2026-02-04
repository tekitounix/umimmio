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
xmake show
```

## テスト自動検出

`xmake test` は `set_group("tests/<lib>")` で登録されたテストターゲットを自動検出します。
各テストターゲットの `xmake.lua` に以下のように追加してください。

```lua
    add_rules("host.test")
    set_group("tests/example")
    add_files("test_example.cc")
```

### グループ指定実行

```bash
# umidi のテストのみ実行
xmake test -g tests/umidi

# tests/* 配下をまとめて実行
xmake test -g "tests/*"
```
