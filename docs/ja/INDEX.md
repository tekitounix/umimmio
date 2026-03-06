# umimmio ドキュメント

[English](../INDEX.md)

umimmio ライブラリのドキュメントエントリです。

## 読む順序

1. [README](README.md) — 概要、クイックスタート、比較
2. [テスト](TESTING.md) — テスト戦略、ランタイム & compile-fail テスト
3. [設計](DESIGN.md) — アーキテクチャ、API 設計、エラーハンドリング

## API リファレンスマップ

- パブリックエントリポイント: `include/umimmio/mmio.hh`
- コアレジスタ抽象化 (厳密なレイヤリング: policy → region → ops):
  - `include/umimmio/policy.hh` — Addr, AccessPolicy, トランスポートタグ, エラーポリシー
  - `include/umimmio/region.hh` — Device, Register, Field, Value, RegionValue, concepts
  - `include/umimmio/ops.hh` — RegOps, ByteAdapter
- 並行性:
  - `include/umimmio/protected.hh` — Protected, Guard, MutexPolicy, NoLockPolicy
- トランスポート実装:
  - `include/umimmio/transport/direct.hh` — DirectTransport (volatile ポインタ)
  - `include/umimmio/transport/i2c.hh` — I2cTransport (HAL ベース)
  - `include/umimmio/transport/spi.hh` — SpiTransport (HAL ベース)
  - `include/umimmio/transport/bitbang_i2c.hh` — BitBangI2cTransport (GPIO)
  - `include/umimmio/transport/bitbang_spi.hh` — BitBangSpiTransport (GPIO)


