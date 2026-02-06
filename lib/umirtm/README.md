# umirtm - SEGGER RTT互換モニター

`/Users/tekitou/work/rtm` から移植した、SEGGER RTT互換のヘッダオンリー実装です。

## 特徴

- RTT互換コントロールブロック/リングバッファ
- `printf`/`snprintf` 相当の軽量フォーマッタ
- `{}` 記法の `print` ヘルパ
- ホスト向け補助ヘッダ (`rtm_host.hh`)

## 公開ヘッダ

- `#include <umirtm/rtm.hh>`
- `#include <umirtm/printf.hh>`
- `#include <umirtm/print.hh>`
- `#include <umirtm/rtm_host.hh>`

## ビルド・テスト

```bash
xmake build umirtm
xmake run test_umirtm
```
