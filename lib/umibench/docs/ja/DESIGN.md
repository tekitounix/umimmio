# umibench 設計

[ドキュメント一覧](INDEX.md) | [English](../DESIGN.md)

## 1. ビジョン

`umibench` は、同一のユーザー記述スタイルで複数ターゲットを扱うベンチマークライブラリです。

1. ベンチマークは通常の C++ `main` として書く
2. 同じソースを host / wasm / embedded で再利用する
3. ターゲット切替はビルド設定で行い、ユーザーコード側の分岐を最小化する
4. ブートストラップ初期化は `main` の前に隠蔽する
5. 人間可読出力と機械可読分析の両立を目指す

## 2. 主要要件

### 2.1 共通 `main` 形式

```cpp
#include <umibench/bench.hh>
#include <umibench/platform.hh>

int main() {
    using Timer = umi::bench::Platform::Timer;

    umi::bench::Runner<Timer> runner;
    runner.calibrate<64>();

    auto stats = runner.run<64>(100, [] {
        // benchmark body
    });

    umi::bench::report<umi::bench::Platform>("sample", stats);
    return 0;
}
```

規約:

1. ベンチマークソースにターゲット専用 include を入れない
2. ターゲット専用型名をユーザーコードに露出しない
3. ユーザー `main` で明示的な `Platform::init()` を必須にしない

### 2.2 ヘッダ解決戦略

`<umibench/platform.hh>` は論理パスとして固定し、実体は include path の順序で解決する。

1. host ビルドは host 実装へ解決
2. wasm ビルドは wasm 実装へ解決
3. embedded ビルドは board/SoC 実装へ解決

### 2.3 依存境界

1. `platforms/*` は低レベルアクセスに `umimmio` を利用
2. `tests/*` は `umitest` を利用
3. `include/umibench/*`（公開API）は `platforms/*` に依存しない

### 2.4 初期化境界

1. Cortex-M/A は startup/reset で必要初期化を実行
2. host/wasm はランタイムフック側で初期化
3. `Platform::init()` は内部的に存在しても、ユーザー必須APIにはしない

## 3. プログラミングモデル

### 3.1 最小フロー

1. `Runner<Timer>` を構築
2. 必要に応じて `calibrate`
3. `run`
4. `report`
5. `main` 終了

### 3.2 関数 / ラムダ等価性

```cpp
void process();
runner.run<64>(100, process);
runner.run<64>(100, [] { process(); });
```

### 3.3 実行セマンティクス

`run<N>(iters, fn)` では:

1. サンプル数は厳密に `N`
2. 呼び出し回数は厳密に `N * iters`
3. 計測ループ内で隠れた追加呼び出しをしない

## 4. ベースラインと統計

### 4.1 ベースライン推定

1. 空処理（または近似空処理）を複数回計測
2. 代表値は median を既定採用
3. 補正式は `corrected = max(raw - baseline, 0)`

### 4.2 統計

`Stats` 主要項目:

- `min`, `max`, `median`
- `mean`, `stddev`, `cv`
- `samples`, `iterations`

数値方針:

1. 可能な範囲でオーバーフロー回避
2. 偶数中央値は安定した式で算出
3. ターゲット差を考慮し、絶対時間の固定値断定を避ける

## 5. 出力モデル

### 5.1 人間可読出力

最低限含める情報:

1. ベンチ名
2. ターゲット名
3. タイマ単位
4. サンプル数 / 反復数
5. `min/max/median/mean/stddev/cv`

### 5.2 機械可読出力

将来の分析に向けて、CSV/JSONL 等の構造化出力へ拡張しやすい設計を維持する。

## 6. テスト戦略

1. `tests/test_*.cc` に責務ごとに分割
2. `test_main.cc` から一括実行
3. compile-fail テストで API 契約を検証（例: `calibrate<0>`）
4. host/wasm/embedded の差を踏まえ、意味論中心の検証を優先

## 7. 設計原則

1. ユーザー `main` は常にターゲット非依存
2. バックエンド選択はビルドシステムで行う
3. 公開APIと実装詳細の境界を守る
4. 計測セマンティクスと統計定義を明示する
5. 人間向け/機械向け出力を同時に成立させる
