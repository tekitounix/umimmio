<!-- SPDX-License-Identifier: MIT -->
# bench ライブラリ - 既知の問題点と改善計画

このドキュメントは `lib/umi/bench` の徹底的な評価結果を記録したものです。
開発者向けの既知の問題、潜在的リスク、将来の改善ロードマップを含みます。

**最終更新**: 2026-02-04
**評価範囲**: 全ソースファイル、テスト、ビルドシステム、ドキュメント

---

## 1. 重要度別問題一覧

### 🔴 Critical (即座の修正が必要)

#### 1.1 UART無限ブロックリスク
**場所**: `include/bench/output/uart.hh`

```cpp
static void putc(char c) {
    while (!(*reinterpret_cast<volatile std::uint32_t*>(usart2_base) & 0x80)) {
        // ← UART未接続/未初期化で永久ハング
    }
    // ...
}
```

**問題**:
- UART未接続時に無限ループ
- タイムアウト機構なし
- デバッガ必須

**影響**: 製品環境でのブート失敗、デバッグ不能化

#### 1.2 `halt()`のデバッガ依存によるHardFault
**場所**: `include/bench/platform/stm32f4.hh`

```cpp
static void halt() {
    asm volatile("bkpt #0");  // ← デバッガ未接続でHardFault
    while (true) {
        asm volatile("wfi");  // ← 割り込み未設定で永久停止
    }
}
```

**問題**:
- `bkpt`命令はデバッガ未接続でHardFault例外を発生
- WFIは有効な割り込みソースがない場合永久停止
- `noreturn`属性欠如

**影響**: 製品環境でシステムクラッシュ

---

### 🟠 High (次期リリースまでに修正)

#### 2.1 baseline計算の統計的信頼性欠如
**場所**: `include/bench/core/runner.hh:calibrate()`

```cpp
template <std::size_t N = DefaultSamples>
Runner& calibrate() {
    std::array<Counter, N> samples{};
    for (std::size_t i = 0; i < N; ++i) {
        samples[i] = measure<Timer>([] {});  // 空ラムダの最適化問題
    }
    baseline = *std::min_element(samples.begin(), samples.end());  // minのみ使用
    return *this;
}
```

**問題**:
- 空ラムダ`[] {}`はコンパイラによって完全に削除される可能性
- 最小値のみ使用 = アウトライアに弱い
- warmupラウンドなし（キャッシュコールド状態）
- 異常値チェックなし

**改善案**:
- `volatile`ワークアラウンド追加
- medianまたはmean使用への変更
- warmupイテレーション追加
- 複数回calibrateの移動平均対応

#### 2.2 `measure()`の最適化削除リスク
**場所**: `include/bench/core/measure.hh`

```cpp
template <TimerLike Timer, typename Func>
typename Timer::Counter measure(Func&& func) {
    std::atomic_signal_fence(std::memory_order_seq_cst);  // 過剰同期
    const auto start = Timer::now();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    std::forward<Func>(func)();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const auto end = Timer::now();
    return end - start;
}
```

**問題**:
- `seq_cst`は過剰（`acquire`/`release`で十分なケースが多い）
- ハードウェアフェンス欠如（`__asm__ volatile`等）
- コンパイラ最適化で測定対象が削除されるリスク
- 関数ポインタのインライン化保証なし

**改善案**:
- `volatile`ワークアラウンドの使用推奨をドキュメント化
- プラットフォーム別ハードウェアフェンス追加
- `__attribute__((always_inline))`検討

#### 2.3 `measure_corrected`の情報損失
**場所**: `include/bench/core/measure.hh`

```cpp
return (measured > baseline) ? (measured - baseline) : 0;  // ゼロ飽和
```

**問題**:
- measured ≦ baseline時に0返却（真の負のオーバーヘッドが不明）
- 符号なし減算のアンダーフローリスク
- 異なるイテレーション数で比例性崩れ

---

### 🟡 Medium (計画中)

#### 3.1 Stats構造体の不完全性
**場所**: `include/bench/core/stats.hh`

```cpp
struct Stats {
    std::uint64_t min = 0;
    std::uint64_t max = 0;
    std::uint64_t mean = 0;  // ← 整数除算で精度損失
    std::uint64_t median = 0;
    // 標準偏差、パーセンタイル、分散欠如
};
```

**問題**:
- meanが整数（真の平均計算不可、例：(1+2)/2=1）
- 標準偏差欠如（ばらつき定量化不可）
- パーセンタイル欠如（p50/p90/p95/p99計測不可）
- 変動係数（CV）欠如（異なる単位間の比較不可）
- sumフィールド欠如

**改善案**:
- `double mean`への変更
- `double stddev`追加
- `percentile(p)`メソッド追加
- `cv()`メソッド追加

#### 3.2 `insertion_sort`の非効率性
**場所**: `include/bench/core/stats.hh`

```cpp
template <typename T, std::size_t N>
void insertion_sort(std::array<T, N>& arr) {
    for (std::size_t i = 1; i < N; ++i) {
        T key = arr[i];  // 値コピー（大きな型で高コスト）
        // ...
    }
}
```

**問題**:
- 値コピー（`std::swap`ベース実装が効率的）
- O(n²)アルゴリズム（Nが大きくなると破滅）
- constexpr非対応

#### 3.3 統計計算の非効率
**場所**: `include/bench/core/stats.hh:compute_stats()`

**問題**:
- 2回の配列走査（ソート前後）
- `sorted = samples`で全コピー（in-placeソート可能）
- Cacheラインパディング未考慮

---

### 🟢 Low (将来検討)

#### 4.1 出力フォーマットの単一性
**場所**: `include/bench/bench.hh:report()`

**問題**:
- スペース区切りテキストのみ
- JSON/CSV/TSV選択不可
- プラットフォーム依存の改行（`\n` vs `\r\n`）

**改善案**:
- `JsonOutput`、`CsvOutput`追加
- 改行の自動検出/設定

#### 4.2 Renodeスクリプトの環境依存
**場所**: `target/stm32f4/renode/bench_stm32f4.resc`

**問題**:
```bash
local renode = "/Applications/Renode.app/Contents/MacOS/Renode"  # macOS専用
```
- macOS専用パス
- ユーザー名`tekitou`ハードコード
- Linux/Windows未対応

---

## 2. API設計の問題

### 2.1 `report()`関数の命名
```cpp
template <typename Output>
void report(const char* name, const Stats& stats, std::uint32_t expected = 0);
```

- `report`は一般的すぎる名前（`report_result`等に変更推奨）
- `expected`パラメータは検証ロジックなし（単なる表示用）
- 改行文字混合（`\n`と`\n\n`の不統一）

### 2.2 `Runner`クラスの柔軟性欠如
```cpp
template <TimerLike Timer, std::size_t DefaultSamples = 64>
class Runner
```

- 実行時サンプル数変更不可（テンプレート固定）
- Stats型固定（カスタム統計処理の拡張ポイントなし）
- 複数タイマー同時計測不可
- 非同期計測パターン未対応

### 2.3 テンプレートパラメータの制約
- `DefaultSamples`変更時の文法が複雑
- デフォルト値（64）の根拠不明
- 大規模ベンチマーク（N=1000+）時のメモリ使用量（`std::array`スタック消費）

---

## 3. セキュリティ・安全性問題

### 3.1 レジスタアクセスのstrict aliasing
**場所**: `include/bench/timer/dwt.hh`, `include/bench/output/uart.hh`

```cpp
return *reinterpret_cast<volatile std::uint32_t*>(dwt_base);
```

- strict aliasing違反リスク（`volatile`で許容されるが型違反）
- アドレスハードコードによる移植性低下
- 複数コア未対応（DWTはコア固有）

### 3.2 ポインタの整合性
- `reinterpret_cast`多用（`uintptr_t`経由の変換が安全）
- アライメントチェックなし

---

## 4. テストの不完全性

### 4.1 未カバー領域
| コンポーネント | カバレッジ |
|---------------|-----------|
| `DwtTimer` | ❌ 概念的テストもなし（ARM組み込み専用） |
| `UartOutput` | ❌ 未テスト |
| `NullOutput` | ❌ 未テスト |
| `Stm32f4` Platform | ❌ 未テスト |

### 4.2 欠如しているテストケース
- 0サンプル（エッジケース）
- 極大値（uint64_t境界）
- アウトライア混在時の統計検証
- 並行Runner実行時の独立性
- 時間経過によるdrift検出
- 異常値（負値、オーバーフロー）

### 4.3 テストの脆弱性
```cpp
bool test_chrono_timer_concept(TestContext& t) {
    static_assert(TimerLike<ChronoTimer>);  // コンパイル時のみ
    return t.assert_true(true);  // ランタイム検証なし
}
```

- 概念テストのみ（実際の動作検証不足）
- モック/スタブ実装なし
- リグレッションベースラインなし

---

## 5. ドキュメントの問題

### 5.1 README.mdの不完全性
- Timer実装一覧なし（`ChronoTimer`/`DwtTimer`の違い不明）
- Output実装一覧なし（`Stdout`/`Uart`/`Null`の違い不明）
- Platform切り替え方法未文書化
- リアルタイム安全性の制約事項未記載
- ベストプラクティス（warmup推奨、cache効果考慮）欠如

### 5.2 コメント不足
- `Stats::mean`が整数除算である旨の注記なし
- `iterations`フィールドの説明なし
- `atomic_signal_fence`の使用理由なし

### 5.3 使用例の不足
- 複数ベンチマーク比較例
- カスタムOutput実装例
- カスタムTimer実装例（RDTSC等）
- テンプレート引数指定例

---

## 6. 移植性・互換性問題

### 6.1 アーキテクチャ依存
- x86用RDTSC実装なし
- `asm volatile`構文はMSVC非対応
- 32bit/64bit混合の型使い分け不統一
- big-endian未検証

### 6.2 コンパイラ依存
- GCC/Clang固有の拡張使用
- `__builtin`関数のラップなし

---

## 7. パフォーマンスの問題

### 7.1 測定オーバーヘッド
- ループアンロールなし
- ラムダ捕捉の間接参照コスト
- 分岐予測効果（確定的測定困難）

### 7.2 統計計算
- 2回の配列走査
- 余分なコピー
- Cache非効率

---

## 8. 欠如している機能

### 8.1 高度な計測機能
| 機能 | 必要性 | 備考 |
|------|--------|------|
| ITM/ETMトレース統合 | 高 | プロファイラ連携 |
| エネルギー計測 | 中 | 消費電力同時計測 |
| 温度補正 | 中 | サーマルスロットリング考慮 |
| DVFS検出 | 中 | 周波数変動正規化 |
| Cache miss count | 低 | メモリアクセス分析 |
| Branch miss prediction | 低 | 分岐予測統計 |

### 8.2 出力・分析機能
| 機能 | 必要性 |
|------|--------|
| JSON出力 | 高（CI連携） |
| CSV出力 | 高（スプレッドシート分析） |
| Google Benchmark互換 | 中 |
| ヒストグラム出力 | 低 |
| 回帰検出 | 中 |
| 統計的有意差検定 | 中 |

### 8.3 ユーティリティ機能
| 機能 | 必要性 |
|------|--------|
| Fixture/Setup | 高 |
| Teardown | 高 |
| パラメータ化ベンチマーク | 中 |
| マルチスレッド計測 | 低 |
| 継続的ベンチマーク | 低 |

---

## 9. 改善ロードマップ

### Phase 1: 緊急修正 (Security/Stability)
**目標**: 製品環境での安全な動作

1. **UART無限ブロック修正**
   - タイムアウト機構追加（最大10000サイクル）
   - 非ブロッキングモード追加
   - エラーフラグ返却

2. **`halt()`安全化**
   - `bkpt`前にデバッガ検出（`CoreDebug->DHCSR & 1`）
   - WDT（ウォッチドッグタイマー）設定推奨
   - `noreturn`属性追加

3. **`measure()`最適化対策**
   - `volatile`ワークアラウンド使用例をドキュメント化
   - ハードウェアフェンス追加（x86: `rdtscp`, ARM: `dmb`）

**期待効果**: 製品環境でのブート失敗ゼロ

### Phase 2: 統計改善 (Quality)
**目標**: 信頼性の高い計測結果

1. **Stats構造体強化**
   ```cpp
   struct Stats {
       double mean;        // ← uint64_tから変更
       double stddev;      // ← 追加
       double cv;          // ← 追加（変動係数）
       uint64_t percentile(int p);  // ← メソッド追加
   };
   ```

2. **baseline計算robust化**
   - warmupイテレーション（10回）追加
   - median使用への変更
   - 異常値検出（3σ外れ）と除去

3. **アウトライア処理**
   - Tukey's fences（四分位範囲法）
   - 自動アウトライア除去オプション

**期待効果**: 計測値の信頼区間±5%以内

### Phase 3: 機能拡張 (Features)
**目標**: 実用的なベンチマークツールへ

1. **出力フォーマット追加**
   - `JsonOutput`、`CsvOutput`実装
   - 構造化出力（ベンチマーク名、タイムスタンプ、環境情報含む）

2. **Fixture機構**
   ```cpp
   runner.setup([] { /* 測定前準備 */ })
         .teardown([] { /* 後処理 */ })
         .run<64>(func);
   ```

3. **パラメータ化ベンチマーク**
   ```cpp
   runner.run_with_params<64>({1, 10, 100, 1000}, [](int n) {
       // nを変化させて計測
   });
   ```

**期待効果**: CI統合、自動回帰検出

### Phase 4: 高度機能 (Advanced)
**目標**: プロフェッショナルツールへ

1. **ハードウェアパフォーマンスカウンタ統合**
   - ARM PMU（Performance Monitoring Unit）
   - x86 RDPMC
   - Cache統計

2. **エネルギー計測**
   - ARM EML（Energy Management Library）
   - 外部ADC連携

3. **リグレッションデータベース**
   - 過去計測値の保存・比較
   - 統計的有意差検定（t検定）
   - 閾値超過アラート

**期待効果**: 継続的パフォーマンス監視

---

## 10. 推奨される使用方法（現在の制約下）

### 安全なベンチマーク記述パターン

```cpp
#include <bench/bench.hh>
#include <bench/platform/host.hh>

using Platform = umi::bench::Host;

int main() {
    Platform::init();
    
    // ✅ 推奨: volatileワークアラウンドで最適化防止
    static volatile int workaround = 0;
    
    umi::bench::Runner<Platform::Timer> runner;
    
    // ✅ 推奨: 十分なwarmupとサンプル数
    runner.calibrate<256>();  // デフォルト64より多めに
    
    // ✅ 推奨: 測定対象に副作用を持たせる
    static volatile int result = 0;
    auto stats = runner.run<128>(1000, [&] { 
        result = compute_something();
    });
    
    // ✅ 推奨: meanの整数除算を認識した上での使用
    // 真の平均が必要な場合、生データを取得して自前計算
    
    (void)result;  // 使用しない場合でも最適化防止
    
    umi::bench::report<Platform::Output>("Compute", stats);
    return 0;
}
```

### 避けるべきパターン

```cpp
// ❌ 非推奨: 最適化で削除される可能性
auto stats = runner.run<64>([] {});

// ❌ 非推奨: 整数meanの精度損失を無視
assert(stats.mean == expected);  // (1+2)/2 = 1 になりうる

// ❌ 非推奨: 組み込み環境でデバッグなしhalt()
Platform::halt();  // HardFaultリスク
```

---

## 11. 関連ドキュメント

- [README.md](README.md) - 基本的な使用方法
- [ARCHITECTURE.md](../../../docs/refs/specs/ARCHITECTURE.md) - UMI全体アーキテクチャ
- [CODING_STYLE.md](../../../lib/docs/CODING_STYLE.md) - コーディング規約
- [TESTING.md](../../../lib/docs/TESTING.md) - テスト戦略

---

## 12. 貢献ガイドライン

問題の修正に貢献する場合：

1. **Critical問題**: 即座にPRを作成。セキュリティ/安全性重視。
2. **統計改善**: `test_bench.cc`にテストケースを追加してから実装。
3. **API変更**: 後方互換性を維持。非推奨マークを経て削除。
4. **新機能**: Phase 2以降の項目は設計ドキュメント（DESIGN.md）を先に作成。

---

## 13. 変更履歴

| 日付 | 変更内容 |
|------|----------|
| 2026-02-04 | 初版作成 - 全コンポーネントの徹底評価 |

---

**免責事項**: このドキュメントは現状分析であり、将来の改善計画を示すものです。記載の問題が即座に修正されることを保証するものではありません。
