# umimmio リファクタリング設計書

**作成日:** 2026-03-05
**ステータス:** ✅ 完了 — commit `515eaf4` (2026-03-05)
**テスト結果:** 34/34 pass (59 runtime + 9 compile-fail)

---

## 1. 現状の評価サマリー

register.hh (1020行) + transport 5ファイル + protected.hh + examples 3本を精査した結果、
以下の問題を特定した。機能的に不要なものはなく、設計の方向性は正しい。
問題はすべて「同一ロジックの重複」と「残骸の除去」に分類される。

| カテゴリ | 件数 | 深刻度 |
|---------|------|--------|
| コード重複 (内部ヘルパー不在) | 3 | 中 |
| 未使用メンバー | 3 | 低 |
| バグ (example 残骸) | 1 | 高 |
| ドキュメント不整合 | 1 (COMPARISON 3箇所 + IMPROVEMENTS 11箇所) | 中 |
| 概念定義の冗長性 (`DirectTransportLike` が `RegTransportLike` の部分集合) | 1 | 低 |
| ロジック重複 (is() 二重実装) | 1 | 低 |

---

## 2. 理想状態の定義

### 2.1 原則

1. **DRY (Don't Repeat Yourself)** — 同一ロジックは単一のヘルパーに集約
2. **最小公開 API** — テスト・内部でしか使わないメンバーは公開しない
3. **Example は正規 API のみ使用** — 内部構造の漏洩を禁止
4. **ドキュメントとコードの一致** — 存在しないメソッド名をドキュメントに書かない

### 2.2 理想状態の具体像

#### register.hh

```
現在: 1020行、レンジチェック 4箇所重複、is() 2箇所重複
理想: ~950行、レンジチェック 1箇所、is() 委譲
```

**変更点:**

| 箇所 | 現状 | 理想 |
|------|------|------|
| Register::value() | インラインでレンジチェック + DynamicValue 構築 | `detail::make_checked_dynamic_value<Region>(val)` に委譲 |
| Field::value() | 同上 (コピペ) | 同上に委譲 |
| raw\<F\>() | 同上 (コピペ) | 同上に委譲 |
| apply_field_value() | ErrorPolicy 経由のレンジチェック | 同上のランタイム版に統一 |
| RegOps::is() | self.read() + 手動分岐 + レンジチェック | `self.read(RegionT{}).is(value)` に委譲 |
| Value::get() | 未使用 static メソッド | 削除 |
| Value::ValueType | 未使用 typedef | 削除 |
| DynamicValue::RegionValueType | 未使用 typedef | 削除 |

#### transport ヘッダ群

```
現在: アドレスエンコード 8箇所コピペ (I2C/SPI × HAL/BitBang × read/write)
理想: detail::encode_address<AddrEndian>() で 1箇所に集約
```

**変更点:**

| 箇所 | 現状 | 理想 |
|------|------|------|
| i2c.hh raw_write() | addr エンディアン変換インライン | `detail::encode_address()` 呼び出し |
| i2c.hh raw_read() | 同上 | 同上 |
| spi.hh raw_write() | 同上 + CmdMask/WriteBit 付き | `detail::encode_spi_address()` 呼び出し |
| spi.hh raw_read() | 同上 + CmdMask/ReadBit 付き | 同上 |
| bitbang_i2c.hh raw_write() | 同上 | `detail::encode_address()` 呼び出し |
| bitbang_i2c.hh raw_read() | 同上 | 同上 |
| bitbang_spi.hh raw_write() | 同上 | `detail::encode_spi_address()` 呼び出し |
| bitbang_spi.hh raw_read() | 同上 | 同上 |

#### examples/transport_mock.cc

```
現在: RegOps<MockTransport> — CRTP 時代の残骸。friend も不要。
理想: RegOps<> — 正規の deducing this パス
```

#### bitbang_spi.hh

```
現在: transfer_byte(tx, rx) — rx は常に nullptr で未使用
理想: transfer_byte(tx) — 戻り値のみで受信
```

---

## 3. 設計詳細

### 3.1 レンジチェックヘルパー (`detail::make_checked_dynamic_value`)

```cpp
namespace detail {

/// @brief レンジチェック付き DynamicValue 生成 (compile-time + runtime)
///
/// Register::value(), Field::value(), raw<F>() の 3箇所で
/// 同一のチェック + 構築パターンが繰り返されている。
/// これを単一のヘルパーに統一する。
template <typename Region, std::unsigned_integral T>
constexpr auto make_checked_dynamic_value(T val) {
    if constexpr (Region::bit_width < sizeof(T) * 8) {
        constexpr auto max_value = (1ULL << Region::bit_width) - 1;
        if consteval {
            if (static_cast<std::uint64_t>(val) > max_value) {
                mmio_compile_time_error_value_out_of_range();
            }
        } else {
            assert(static_cast<std::uint64_t>(val) <= max_value
                   && "Value out of range");
        }
    }
    return DynamicValue<Region, T>{val};
}

} // namespace detail
```

**呼び出し側の変形:**

```cpp
// Before (Register::value)
template <std::unsigned_integral T>
static constexpr auto value(T val) {
    if constexpr (Bits < sizeof(T) * bits8) { /* 15行のチェック */ }
    return DynamicValue<Register, T>{val};
}

// After
template <std::unsigned_integral T>
static constexpr auto value(T val) {
    return detail::make_checked_dynamic_value<Register>(val);
}
```

Field::value() と raw\<F\>() も同一パターンで書き換え。

**apply_field_value のランタイムチェック:**

apply_field_value() は ErrorPolicy 経由のチェックを行う。
これは compile-time 不可の DynamicValue 専用パスなので、
make_checked_dynamic_value とは分離する。ただし、
write() / modify() の入口で DynamicValue のレンジが
既にチェック済みであれば apply_field_value 内のチェックは冗長。

**判断:** apply_field_value のチェックをそのまま残す（ダブルチェックは安全側）。
理由: DynamicValue は直接構築可能であり、value() を経由しない場合がある。

**assert vs ErrorPolicy の混在について:**
Register::value() / Field::value() / raw\<F\>() は RegOps の文脈外（レジスタ定義側）で
呼ばれるため、ErrorPolicy テンプレートパラメータにアクセスできない。
そのため assert が唯一の選択肢であり、これは意図的な設計判断である。
RegOps 経由の apply_field_value / is() は ErrorPolicy にアクセス可能なためそちらを使用する。

### 3.2 アドレスエンコードヘルパー (`detail::encode_address`)

**配置場所:** `transport/detail.hh` (新規ファイル)。
register.hh は既に 1020 行あり、transport 固有のヘルパーを入れるのは不適切。
各 transport ヘッダが `#include "detail.hh"` で参照する。

```cpp
// transport/detail.hh
#pragma once
#include <cstdint>
#include <bit>

namespace umi::mmio::detail {

/// @brief マルチバイトアドレスをバッファにエンコード
template <std::endian AddrEndian, typename AddressType>
constexpr std::size_t encode_address(AddressType reg_addr,
                                     std::uint8_t* buf) noexcept {
    constexpr std::size_t addr_size = sizeof(AddressType);
    static_assert(addr_size == 1 || addr_size == 2);

    if constexpr (addr_size == 1) {
        buf[0] = static_cast<std::uint8_t>(reg_addr);
    } else if constexpr (AddrEndian == std::endian::big) {
        buf[0] = static_cast<std::uint8_t>(reg_addr >> 8);
        buf[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
    } else {
        buf[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
        buf[1] = static_cast<std::uint8_t>(reg_addr >> 8);
    }
    return addr_size;
}

/// @brief SPI コマンドバイト付きアドレスエンコード
/// @tparam RwBit  Read/Write 方向を示すビット (ReadBit or WriteBit)
/// @tparam CmdMask  アドレスバイトに適用するマスク
template <std::endian AddrEndian, typename AddressType,
          std::uint8_t RwBit, std::uint8_t CmdMask>
constexpr std::size_t encode_spi_address(AddressType reg_addr,
                                         std::uint8_t* buf) noexcept {
    auto n = encode_address<AddrEndian>(reg_addr, buf);
    buf[0] = (buf[0] & CmdMask) | RwBit;
    return n;
}

} // namespace umi::mmio::detail
```

### 3.3 RegOps::is() の委譲

```cpp
// Before (RegOps::is — 24行)
template <typename Self, ReadableValue V>
[[nodiscard]] bool is(this const Self& self, V&& value) noexcept {
    using VDecay = std::decay_t<V>;
    using RegionT = typename VDecay::RegionType;
    check_transport_allowed<Self, RegionT>();
    if constexpr (requires { VDecay::value; }) {
        if constexpr (RegionT::is_register) {
            return self.read(RegionT{}).bits() == ...;
        } else {
            return self.read(RegionT{}).bits() == ...;
        }
    } else {
        // DynamicValue: range check + 同じ分岐
    }
}

// After (RegOps::is — DynamicValue レンジチェック + 委譲)
template <typename Self, ReadableValue V>
[[nodiscard]] bool is(this const Self& self, V&& value) noexcept {
    using VDecay = std::decay_t<V>;
    using RegionT = typename VDecay::RegionType;
    check_transport_allowed<Self, RegionT>();

    // DynamicValue のレンジチェック (Value は不要)
    if constexpr (!requires { VDecay::value; }) {
        if constexpr (CheckPolicy::value) {
            if constexpr (RegionT::bit_width < sizeof(decltype(value.assigned_value)) * bits8) {
                auto const max_value = (1ULL << RegionT::bit_width) - 1;
                if (static_cast<std::uint64_t>(value.assigned_value) > max_value) {
                    ErrorPolicy::on_range_error("Comparison value out of range");
                }
            }
        }
    }

    // Register/Field 分岐は RegisterReader::is() に委譲
    if constexpr (RegionT::is_register) {
        return self.read(RegionT{}).is(std::forward<V>(value));
    } else {
        return self.read(typename RegionT::ParentRegType{}).is(std::forward<V>(value));
    }
}
```

**注意:** この委譲によりバスアクセスの回数は変わらない（元コードも `self.read()` を呼んでいる）。

**RegisterReader::is() と DynamicValue レンジチェックの関係:**
RegisterReader::is() は DynamicValue のレンジチェックを行わない。これは意図的設計である。
RegisterReader は「既に読み取った値のビュー」であり、ErrorPolicy を保持しない。
レンジチェックの責務は write()/modify()/is() の入口である RegOps 層に集約する。
RegisterReader::is() を直接呼ぶユーザーは、value() 経由で DynamicValue を
生成しているため、その時点で assert チェック済みである。

### 3.4 未使用メンバーの削除

```cpp
// 削除対象 1: Value::get()
// 全箇所 VDecay::value を直接参照 — get() 経由は 0件
static constexpr auto get() noexcept { return value; }  // 削除

// 削除対象 2: Value::ValueType
using ValueType = decltype(value);  // 削除

// 削除対象 3: DynamicValue::RegionValueType
using RegionValueType = typename RegionT::ValueType;  // 削除
```

### 3.5 transport_mock.cc 修正

`RegOps<MockTransport>` は CRTP 残骸だが、単なる古い書き方ではなく **潜在バグ** でもある。
RegOps の第 1 テンプレートパラメータは `CheckPolicy` (デフォルト: `std::true_type`) であり、
`RegOps<MockTransport>` は `CheckPolicy=MockTransport` という型置換になる。
現在は Value パス（compile-time 値）しか通らないため問題が顕在化していないが、
DynamicValue パスを通ると `MockTransport::value` が要求されコンパイルエラーになる。

```cpp
// Before (CheckPolicy=MockTransport という暗黙のバグ)
class MockTransport : private RegOps<MockTransport> {
    friend class RegOps<MockTransport>;
public:
    using RegOps<MockTransport>::write;
    ...

// After (deducing this — friend 不要)
class MockTransport : private RegOps<> {
public:
    using RegOps<>::write;
    ...
```

### 3.6 transfer_byte の rx パラメータ削除

```cpp
// Before
std::uint8_t transfer_byte(std::uint8_t tx,
                           [[maybe_unused]] std::uint8_t* rx) const noexcept;

// After
std::uint8_t transfer_byte(std::uint8_t tx) const noexcept;
```

呼び出し側: `transfer_byte(tx_buf[i], nullptr)` → `transfer_byte(tx_buf[i])`

### 3.7 TransportLike 概念の整理

`DirectTransportLike` は `RegTransportLike` の部分集合（TransportTag が DirectTransportTag に
限定されている以外は同一の requirements）。TransportLike の定義から除去する。

```cpp
// Before
concept TransportLike = DirectTransportLike<T> || ByteTransportLike<T> || RegTransportLike<T>;

// After
concept TransportLike = RegTransportLike<T> || ByteTransportLike<T>;
```

`DirectTransportLike` の定義自体は削除しない（テストで使用される可能性を残す）。

### 3.8 ドキュメント修正

修正対象のファイルは **`lib/docs/design/`** 配下（`lib/umimmio/` ではない）:

| ファイル | `.raw()` 箇所数 | 修正内容 |
|---------|----------------|----------|
| `lib/docs/design/MMIO_TYPE_SAFETY_COMPARISON.md` | 3箇所 | コード例の `.raw()` → `.bits()` |
| `lib/docs/design/UMIMMIO_IMPROVEMENTS.md` | 11箇所 | コード例 + 設計説明文の `.raw()` → `.bits()`。散文中の「`.raw()` 経由」等の表現も修正が必要 |

---

## 4. 影響範囲

### 4.1 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `register.hh` | ヘルパー追加、value()/raw\<\>() 委譲、is() 委譲、未使用メンバー削除、TransportLike 修正 |
| `transport/detail.hh` | **新規作成** — encode_address / encode_spi_address |
| `transport/i2c.hh` | encode_address() 使用 + `#include "detail.hh"` |
| `transport/spi.hh` | encode_spi_address() 使用 + `#include "detail.hh"` |
| `transport/bitbang_i2c.hh` | encode_address() 使用 + `#include "detail.hh"` |
| `transport/bitbang_spi.hh` | encode_spi_address() 使用 + `#include "detail.hh"`、transfer_byte() 簡素化 |
| `examples/transport_mock.cc` | RegOps\<\> + friend 削除 |
| `lib/docs/design/MMIO_TYPE_SAFETY_COMPARISON.md` | `.raw()` → `.bits()` (3箇所) |
| `lib/docs/design/UMIMMIO_IMPROVEMENTS.md` | `.raw()` → `.bits()` (11箇所、散文含む) |

### 4.2 影響を受けないファイル

| ファイル | 理由 |
|---------|------|
| `transport/direct.hh` | アドレスエンコード不要 (volatile pointer) |
| `protected.hh` | 変更不要 |
| `mmio.hh` | 変更不要 |
| 全テストファイル | API 変更なし (内部リファクタのみ) |
| `umidevice/cs43l22.hh` | 変更不要 |
| `umipal/` 生成物 | 変更不要 |

### 4.3 API 互換性

**完全後方互換。** 公開 API の変更は一切ない:
- `value()` の戻り値型は変更なし (`DynamicValue<Region, T>`)
- `is()` の戻り値型と意味は変更なし (`bool`)
- テンプレートパラメータは変更なし

唯一の破壊的変更は `Value::get()` / `Value::ValueType` / `DynamicValue::RegionValueType` の削除だが、
これらは内部でも外部でも使用されていないため影響なし。

---

## 5. 実装順序

| Step | 内容 | 検証 | 状況 |
|------|------|------|:----:|
| 1 | `detail::make_checked_dynamic_value` 追加 + 3箇所委譲 | xmake test | ✅ |
| 2 | `transport/detail.hh` 新規作成 + `encode_address` / `encode_spi_address` 追加 + 4ファイル修正 | xmake test | ✅ |
| 3 | RegOps::is() を RegisterReader::is() に委譲 | xmake test | ✅ |
| 4 | 未使用メンバー 3件削除 | xmake test | ✅ |
| 5 | transport_mock.cc 修正 (RegOps\<\> + friend 削除) | ビルド確認 | ✅ |
| 6 | transfer_byte rx 削除 | xmake test | ✅ |
| 7 | TransportLike 概念から DirectTransportLike 除去 | xmake test | ✅ |
| 8 | ドキュメント修正 (.raw() → .bits(): COMPARISON 3箇所 + IMPROVEMENTS 11箇所) | 目視 | ✅ |

全ステップ完了。1コミットにまとめて適用 (`515eaf4`)。

---

## 6. リスク評価

| リスク | 影響 | 対策 |
|-------|------|------|
| make_checked_dynamic_value の consteval 動作差異 | コンパイルエラー | 既存テスト + compile-fail テストで検証 |
| encode_address のエンディアン間違い | データ化け | 既存 transport テストで検証 |
| is() 委譲によるバスアクセス増加 | なし | 元コードも self.read() を呼んでいる |
| Value::get() 削除で壊れる外部コード | なし | grep 確認済み: 使用箇所 0 |

---

## 7. 行数見積もり

| 項目 | 変化 |
|------|------|
| register.hh | -30行 (3箇所のチェック重複 ~10行×3 削除) + 15行 (ヘルパー + TransportLike 修正) = **-15行** |
| transport/detail.hh | **+25行** (新規ファイル: encode_address + encode_spi_address) |
| transport 4ファイル合計 | -48行 (8箇所のアドレスエンコード削除) + 8行 (ヘルパー呼出 + include) = **-40行** |
| transport_mock.cc | -2行 |
| 合計 | **約 -30行** (新規ファイル含む純減) |
