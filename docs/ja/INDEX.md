# umimmio ドキュメント

[English](../INDEX.md)

このページは GitHub と Doxygen の両方で使用される正規ドキュメントエントリです。

## 読む順序

1. [テスト](TESTING.md)
2. [設計](DESIGN.md)

## API リファレンスマップ

- パブリックエントリポイント: `include/umimmio/mmio.hh`
- コアレジスタ抽象化:
  - `include/umimmio/register.hh` — BitRegion, Register, Field, Value, RegOps, ByteAdapter
- トランスポート実装:
  - `include/umimmio/transport/direct.hh` — DirectTransport (volatile ポインタ)
  - `include/umimmio/transport/i2c.hh` — I2cTransport (HAL ベース)
  - `include/umimmio/transport/spi.hh` — SpiTransport (HAL ベース)
  - `include/umimmio/transport/bitbang_i2c.hh` — BitBangI2cTransport (GPIO)
  - `include/umimmio/transport/bitbang_spi.hh` — BitBangSpiTransport (GPIO)

## ローカル生成

```bash
xmake doxygen -P . -o build/doxygen .
```

生成エントリポイント:

- `build/doxygen/html/index.html`

## リリースメタデータ

- バージョンファイル: `VERSION`
- 変更履歴: `CHANGELOG.md`
- リリースポリシー: `RELEASE.md`

GitHub 自動化:

- ワークフローファイル: `.github/workflows/umimmio-ci.yml`
- プルリクエスト: ホストテスト + compile-fail 実行
- `main` ブランチプッシュ: CI 検証
