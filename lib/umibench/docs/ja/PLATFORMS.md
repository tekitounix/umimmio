# プラットフォーム

[ドキュメント一覧](INDEX.md) | [English](../PLATFORMS.md)

## ヘッダ解決モデル

ユーザーコードは常に次を include します。

- `<umibench/bench.hh>`
- `<umibench/platform.hh>`

実際に使われる `platform.hh` はビルド設定で切り替わります。

## 組み込み済みターゲット

- Host: `platforms/host/umibench/platform.hh`
- WASM: `platforms/wasm/umibench/platform.hh`
- STM32F4 (Cortex-M): `platforms/arm/cortex-m/stm32f4/umibench/platform.hh`

## 共有Platformベース

`platforms/common/platform_base.hh` に共通実装を集約しています。

- `PlatformBase<Timer, Output>`
- `PlatformAutoInit<Platform>`

## 新規ターゲット追加手順

1. timer/output バックエンド実装
2. `platforms/<your-target>/umibench/platform.hh` 追加
3. `xmake.lua` に include path / target rule を追加
4. 既存共通テストを再利用しつつ検証
