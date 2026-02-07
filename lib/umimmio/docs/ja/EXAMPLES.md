# サンプル

[ドキュメント一覧](INDEX.md) | [English](../EXAMPLES.md)

## サンプル一覧

- `examples/minimal.cc`: 基本的なRegionとFieldの定義（コンパイル時チェック付き）。
- `examples/register_map.cc`: 現実的なSPIペリフェラルのレジスタマップレイアウト。
- `examples/transport_mock.cc`: ホストテスト用のRAMバックドモックトランスポート。

## 学習順序の推奨

1. `minimal.cc`
2. `register_map.cc`
3. `transport_mock.cc`

## 運用ガイド

- レジスタマップは通常デバイス固有のヘッダ（例: `spi_regs.hh`）に定義する。
- マスクとオフセット値のコンパイル時検証に `static_assert` を使用。
- ホストテストでは実際のMMIOアクセスの代わりにRAMバックドモックトランスポートを作成。
- `DirectTransport` はステートレス。I2C/SPIトランスポートはドライバ参照とデバイスアドレスを保持。
