# umimmio ドキュメント

[English](../INDEX.md)

このページは、GitHub と Doxygen の両方で使うドキュメントの入口です。

## 推奨の読む順序

1. [はじめに](GETTING_STARTED.md)
2. [使い方](USAGE.md)
3. [サンプル](EXAMPLES.md)
4. [テスト](TESTING.md)
5. [設計](DESIGN.md)

## APIリファレンスマップ

- 公開エントリポイント: `include/umimmio/mmio.hh`
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

生成先:

- `build/doxygen/html/index.html`

## リリース情報

- バージョンファイル: `VERSION`
- 変更履歴: `CHANGELOG.md`
- リリース方針: `RELEASE.md`

GitHub自動化:

- ワークフローファイル: `.github/workflows/umimmio-ci.yml`
- Pull Request: ホストテスト + compile-fail 実行
- `main` ブランチpush: CI検証
