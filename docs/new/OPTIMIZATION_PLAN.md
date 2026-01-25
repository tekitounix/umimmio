# UMI-OS コンパクト化・最適化計画

**バージョン**: 1.1
**作成日**: 2026-01-25
**更新日**: 2026-01-26
**対象**: カーネル、USBスタック、シェルライブラリ

## 概要

本計画は、UMI-OSの以下のコンポーネントについて、機能を維持しながらコードサイズとメモリ使用量を削減し、実行効率を向上させることを目的とする。

- **カーネル** (`lib/umios/kernel/`)
- **USBスタック** (`lib/umiusb/`)
- **シェルライブラリ** (`lib/umios/shell/`)

## 現状分析

### ビルドサイズ (stm32f4_kernel)

| 指標 | 値 | 備考 |
|------|-----|------|
| text (Flash) | 41,692 B | コード |
| data | 84 B | 初期化済みデータ |
| bss (RAM) | 77,736 B | 未初期化データ |

---

## 実測検証結果

### ✅ LTO (Link Time Optimization) - **検証済み・有効**

```
$ xmake build stm32f4_kernel  # LTO=thin
```

| 指標 | LTOなし | LTO (thin) | 削減 |
|------|---------|------------|------|
| text (Flash) | 41,692 B | 40,344 B | **-1,348 B (3.2%)** |
| data | 84 B | 20 B | -64 B |
| bss (RAM) | 77,736 B | 72,616 B | **-5,120 B (6.6%)** |

**注意点**: アセンブリから参照されるシンボルには `[[gnu::used]]` 属性が必要

```cpp
// LTO対応のためのシンボル保護
extern "C" [[gnu::used]] void umi_cm4_switch_context();
extern "C" [[gnu::used]] void svc_handler_c(uint32_t* sp);
extern "C" [[gnu::used]] umi::port::cm4::TaskContext* umi_cm4_current_tcb;
```

**判定**: ✅ 有効 - 即座に適用可能

---

### ✅ コマンドハッシュテーブル - **検証済み・有効**

ホストベンチマーク結果 (16コマンド、100万回反復):

| 方式 | 所要時間 | 1回あたり |
|------|----------|----------|
| if-else連鎖 (現状) | 115.7 ms | 19.3 ns |
| FNV-1aハッシュテーブル | 45.0 ms | 7.5 ns |

**高速化率: 2.57倍**

```cpp
// FNV-1a ハッシュ (コンパイル時計算可能)
constexpr uint32_t fnv1a(const char* str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= static_cast<uint8_t>(*str++);
        hash *= 16777619u;
    }
    return hash;
}
```

**判定**: ✅ 有効 - コマンド数10個以上で効果大

---

### ❌ キャッシュラインアライメント削除 - **検証済み・削除不可**

```cpp
// umi_kernel.hh:350-351 - SpscQueueのみで使用
alignas(64) std::atomic<std::size_t> write_pos_ {0};
alignas(64) std::atomic<std::size_t> read_pos_ {0};
```

**検証結果**: ISR-Task間のfalse sharing防止に**必須**。TCB配列には未使用。

**判定**: ❌ 削除不可

---

### ❌ USB StringDescriptor変換削除 - **検証済み・既に実装済み**

```cpp
// lib/umiusb/include/descriptor.hh:88-100
// コンパイル時UTF-16変換が既に実装済み
template<std::size_t N>
constexpr auto String(const char (&s)[N]) {
    // コンパイル時にUTF-16LE変換
    for (std::size_t i = 0; i < len; ++i) {
        result[2 + i * 2] = static_cast<uint8_t>(s[i]);
        result[2 + i * 2 + 1] = 0;
    }
    return result;
}
```

**判定**: ❌ 不要 - 既に最適化済み

---

### ⚠️ TimerQueue O(1) - **効果限定的**

**現状**: デルタエンコード方式で O(n) 挿入

**検証結果**: MaxTimers は通常8-16程度のため実用上問題なし。
デルタエンコードはメモリ効率が良く、現状維持が適切。

**判定**: ⚠️ 効果限定的 - 将来タイマー数が増加した場合のみ検討

---

### ⚠️ Hw<Impl> テンプレート共通化 - **効果薄**

```cpp
// 現状: 薄いラッパー (各メソッド1行転送)
template <class Impl>
class Hw {
    static void set_timer_absolute(usec target) { Impl::set_timer_absolute(target); }
    static usec monotonic_time_usecs() { return Impl::monotonic_time_usecs(); }
    // ...
};
```

**検証結果**: 各メソッドが1行の転送のみで、共通化の余地なし。

**判定**: ⚠️ 効果薄

---

## 有効な最適化項目 (優先度順)

| 優先度 | 項目 | 効果 | 難易度 | 状態 |
|-------|------|------|--------|------|
| **1** | **LTO有効化** | Flash -3.2%, RAM -6.6% | 低 | ✅ 検証済み |
| **2** | **コマンドハッシュ** | CPU 2.57x高速化 | 中 | ✅ 検証済み |
| 3 | 出力バッファ削減 | RAM 1.5-2KB | 低 | 未検証 |
| 4 | セクションGC | Flash 3-8% | 低 | 未検証 |
| 5 | CRTP最適化 | ISR CPU 10% | 高 | 未検証 |
| 6 | DMAリングバッファ | Audio CPU 20-30% | 高 | 未検証 |

## 削除/不要な項目

| 項目 | 理由 |
|------|------|
| キャッシュライン削除 | false sharing防止に必須 |
| StringDesc変換削除 | 既にコンパイル時変換実装済み |
| TimerQueue O(1) | MaxTimers小で効果限定的 |
| Hw<Impl>共通化 | 薄いラッパーで効果薄 |

---

## 実装済み変更

1. **LTO対応シンボル保護** (`examples/stm32f4_kernel/src/main.cc`)
   - `[[gnu::used]]` 属性追加

2. **LTO設定** (`examples/stm32f4_kernel/xmake.lua`)
   - `embedded.lto = "thin"` オプション（コメントアウト状態）

---

## 期待される最終効果

| 指標 | 現状 | LTO適用後 | 全最適化後 |
|------|------|----------|----------|
| Flash | 41,692 B | 40,344 B (-3.2%) | ~38,000 B (-9%) |
| RAM | 77,736 B | 72,616 B (-6.6%) | ~70,000 B (-10%) |
| コマンド検索 | O(n) | O(n) | O(1) 平均 |

---

## 検証方法

1. **サイズ検証**: `arm-none-eabi-size` での継続的監視
2. **パフォーマンス検証**: ホストベンチマーク + DWT サイクルカウンタ
3. **回帰テスト**: 既存機能の動作確認
4. **ハードウェアテスト**: STM32F4 Discovery での実機確認

---

## 参考資料

- ARM Cortex-M4 Technical Reference Manual
- STM32F4 Reference Manual (RM0090)
- USB 2.0 Specification
- "Making Embedded Systems" by Elecia White
