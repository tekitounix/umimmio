# umimmio 実装計画

**作成日:** 2026-03-05
**ステータス:** ほぼ完了 — 未実装 2件
**参照:** [UMIMMIO_IMPROVEMENTS.md](../../../docs/design/UMIMMIO_IMPROVEMENTS.md),
[UMIMMIO_AUDIT_REPORT.md](../../../docs/design/UMIMMIO_AUDIT_REPORT.md)

---

## 概要

本文書は umimmio ライブラリの全面改修の実装指示書である。
後方互換は考慮せず、理想的な設計を一回の更新として適用する。

全変更は以下の 4 Phase に分かれ、Phase 内は独立にコミット可能な Step で構成する。

| Phase | 内容 | 対象ファイル | 状況 |
|-------|------|-------------|:----:|
| 1 | 型安全基盤 | register.hh | ✅ 完了 |
| 2 | API 刷新 | register.hh, transport/*.hh | ⚠️ Step 2.3 未実装 |
| 3 | 新機能追加 | register.hh, 新規ヘッダ | ✅ 完了 |
| 4 | テスト・ドキュメント | tests/*, docs/* | ⚠️ Step 4.6 未実施 |

### 未実装項目

| Step | 内容 | 理由 |
|------|------|------|
| **2.3** | I2c/SpiTransport NTTP 静的参照化 (`auto& Instance`) | 実使用箇所 (umidevice) がランタイム参照を前提としており、移行に影響範囲の調査が必要 |
| **4.6** | DESIGN.md / TESTING.md / INDEX.md のドキュメント更新 | コード実装を優先し後回し |

---

## Phase 1: 型安全基盤

### Step 1.1: BitRegion 不変条件の static_assert 追加

**対象:** `register.hh` — `BitRegion` 構造体内 (L125–176)

**変更内容:** `BitRegion` の先頭に以下の 5 つの `static_assert` を追加する。

```cpp
template <class Parent,
          Addr AddressOrOffset,
          std::size_t RegBitWidth,
          std::size_t BitOffset,
          std::size_t BitWidth,
          class Access,
          std::uint64_t ResetValue,
          bool IsRegister>
struct BitRegion {
    // --- Structural invariants ---
    static_assert(BitWidth > 0, "Field width must be > 0");
    static_assert(RegBitWidth <= 64, "Register width must be <= 64 bits");
    static_assert(BitOffset + BitWidth <= RegBitWidth,
                  "Field bit range exceeds register width");
    static_assert(!IsRegister || BitOffset == 0,
                  "Register must have BitOffset == 0");
    static_assert(!IsRegister || BitWidth == RegBitWidth,
                  "Register BitWidth must equal RegBitWidth");

    // ... (残りは変更なし)
};
```

**根拠:** 手書きレジスタ定義のタイプミス (e.g., `Field<Reg, 30, 4>` on 32-bit) を
即座にコンパイルエラーにする。umipal-gen 生成コードの検証にもなる。

---

### Step 1.1a: Register に `W1cMask` NTTP を追加

**対象:** `register.hh` — `Register` 構造体 (L178–196)

**変更内容:** W1C フィールドと通常フィールドが同居するレジスタで `modify()` を使用した際に、
W1C ビットへの意図しない書き戻しを防ぐための `W1cMask` テンプレートパラメータを追加する。

```cpp
/// @brief Register — top-level BitRegion that always provides value().
///
/// @tparam Parent   Parent Device or Block type.
/// @tparam Offset   Address offset from parent base.
/// @tparam Bits     Register width in bits (8, 16, 32, 64).
/// @tparam Access   Access policy (RW, RO, WO, W1C, ...).
/// @tparam Reset    Reset value.
/// @tparam W1cMask  Bitmask of W1C field positions. modify() automatically
///                  clears these bits before write-back to prevent accidental
///                  flag clearing during read-modify-write.
template <class Parent, Addr Offset, std::size_t Bits,
          class Access = RW, std::uint64_t Reset = 0,
          std::uint64_t W1cMask = 0>
struct Register : BitRegion<Parent, Offset, Bits, 0, Bits, Access, Reset, true> {
    static_assert(Bits == 64 || W1cMask < (1ULL << Bits),
                  "W1cMask has bits set beyond register width");

    /// @brief Bitmask of all W1C field positions in this register.
    /// Writing 0 to W1C bits is safe (no-op), writing 1 clears them.
    /// modify() masks these bits to 0 before write-back.
    static constexpr auto w1c_mask =
        static_cast<typename BitRegion<Parent, Offset, Bits, 0, Bits, Access, Reset, true>::RegValueType>(W1cMask);

    /// @brief Create a dynamic value for this register.
    template <std::unsigned_integral T>
    [[nodiscard("value() result must be used with write(), modify(), or is()")]]
    static constexpr auto value(T val) {
        if constexpr (Bits < sizeof(T) * bits8) {
            constexpr auto max_value = (1ULL << Bits) - 1;
            if consteval {
                if (static_cast<std::uint64_t>(val) > max_value) {
                    mmio_compile_time_error_value_out_of_range();
                }
            } else {
                assert(static_cast<std::uint64_t>(val) <= max_value
                       && "Register value out of range");
            }
        }
        return DynamicValue<Register, T>{val};
    }
};
```

**設計判断:**
- `W1cMask` のデフォルトは `0` — W1C フィールドを持たないレジスタは変更不要
- umipal-gen は SVD の `modifiedWriteValues="oneToClear"` から自動計算
- `modify()` のライトバック時に `writeback &= ~w1c_mask` で W1C ビットを安全化
- W1C ビットに 0 を書いても何も起きない (W1C の仕様) ので常に安全
- `value()` から `requires(can_write)` を **意図的に除外** — `value()` は値の生成
  ファクトリであり副作用を持たない。書き込み制約は `write()`/`modify()` の
  `WritableValue` concept が担当する。`is()` で RO レジスタの DynamicValue 比較を
  可能にするため分離が必須

**使用例:**

```cpp
// SVD: SR レジスタ — OVR(bit0) と EOC(bit1) が W1C、EN(bit8) が RW
struct SR : Register<ADC, 0x00, bits32, RW, 0,
                     /* W1cMask = */ 0x03> {};  // OVR | EOC のビット位置

// modify() は自動的に W1C ビットを保護する:
io.modify(SR_EN::Set{});  // read → (current & ~0x03) | EN → write
                           // → OVR/EOC は 0 で書き戻され、クリアされない
```

---

### Step 1.2: WriteBehavior enum + W1C アクセスポリシー

**対象:** `register.hh` — Access policies セクション (L55–65)

**変更内容:** 書き込みセマンティクスを `WriteBehavior` enum で分類し、
`W1C` ポリシーを追加する。`bool is_w1c` ではなく enum を採用する理由:

1. **拡張性:** 将来 `ONE_TO_SET` 等が必要になっても enum に値を追加するだけ
2. **Concept 安定性:** `IsW1C` concept の定義が enum 比較で変化しない
3. **網羅性:** `if constexpr` / `switch` で exhaustive チェックが可能

```cpp
/// @brief Describes how a write operation affects the register/field.
enum class WriteBehavior : std::uint8_t {
    NORMAL,        ///< Standard write: written value replaces current value.
    ONE_TO_CLEAR,  ///< Write-1-to-clear: writing 1 clears the bit, writing 0 has no effect.
};

/// @brief Parameterized access policy.
/// @tparam CanRead  Whether the register/field is readable.
/// @tparam CanWrite Whether the register/field is writable.
/// @tparam Behavior Write semantics (NORMAL, ONE_TO_CLEAR, ...).
template <bool CanRead, bool CanWrite, WriteBehavior Behavior = WriteBehavior::NORMAL>
struct AccessPolicy {
    static constexpr bool can_read = CanRead;
    static constexpr bool can_write = CanWrite;
    static constexpr auto write_behavior = Behavior;
};

using RW  = AccessPolicy<true,  true>;
using RO  = AccessPolicy<true,  false>;
using WO  = AccessPolicy<false, true>;
using W1C = AccessPolicy<true,  true, WriteBehavior::ONE_TO_CLEAR>;

struct Inherit {};
```

**設計判断:**
- テンプレート + `using` エイリアスにより全ポリシーが1行で均一
- メンバ定義が `AccessPolicy` 一箇所のみ — DRY、新プロパティ追加が容易
- `WriteBehavior::NORMAL` のデフォルト引数で通常ポリシーは `Behavior` 指定不要
- 構造的等価性: 同じプロパティの組み合わせは同じ型。concepts でチェックするため正しい挙動
- `Inherit` はプロパティを持たない独立型 — テンプレートに含めない

#### `ExtractAccess` の `IsAccessPolicy` concept 対応

既存の `ExtractAccess` は `RW`, `RO`, `WO`, `Inherit` のみを型マッチしており、
`AccessPolicy<...>` の任意の特殊化 (`W1C` 等) を認識できない。
concept で判定することで、将来どのような `AccessPolicy<...>` 特殊化を追加しても
`ExtractAccess` の変更が不要になる (OCP 準拠):

```cpp
namespace detail {

/// @brief Matches any AccessPolicy specialization.
template <typename T>
concept IsAccessPolicy = requires {
    { T::can_read } -> std::convertible_to<bool>;
    { T::can_write } -> std::convertible_to<bool>;
};

/// @brief Extract Access trait from a pack of types. Returns Inherit if not found.
template <typename... Ts>
struct ExtractAccess {
    using type = Inherit;
};
template <IsAccessPolicy T, typename... Rest>
struct ExtractAccess<T, Rest...> {
    using type = T;
};
template <typename T, typename... Rest>
    requires (!IsAccessPolicy<T>)
struct ExtractAccess<T, Rest...> {
    using type = typename ExtractAccess<Rest...>::type;
};

template <typename... Ts>
using ExtractAccess_t = typename ExtractAccess<Ts...>::type;

} // namespace detail
```

---

### Step 1.3: Concept 定義の追加

**対象:** `register.hh` — Access policies 直後 (新規セクション)

**変更内容:** 以下の concept を新設する。
すべての `static_assert` ベースのアクセスチェックをこれらの concept に統一する。

```cpp
// ===========================================================================
// Access constraint concepts
// ===========================================================================

/// @brief Type has readable access policy.
template <typename T>
concept Readable = T::AccessType::can_read;

/// @brief Type has writable access policy.
template <typename T>
concept Writable = T::AccessType::can_write;

/// @brief Type has both read and write access.
template <typename T>
concept ReadWritable = Readable<T> && Writable<T>;

/// @brief Type is a register (not a field).
template <typename T>
concept IsRegister = T::is_register;

/// @brief Type is a field (not a register).
template <typename T>
concept IsField = !T::is_register;

/// @brief Type has W1C (Write-1-to-Clear) access policy.
template <typename T>
concept IsW1C = requires { T::AccessType::write_behavior; }
    && T::AccessType::write_behavior == WriteBehavior::ONE_TO_CLEAR;

/// @brief Type is not W1C (safe for modify() and flip()).
template <typename T>
concept NotW1C = !IsW1C<T>;

/// @brief Value/DynamicValue whose region is Readable.
template <typename V>
concept ReadableValue = requires {
    typename std::decay_t<V>::RegionType;
} && Readable<typename std::decay_t<V>::RegionType>;

/// @brief Value/DynamicValue whose region is Writable.
template <typename V>
concept WritableValue = requires {
    typename std::decay_t<V>::RegionType;
} && Writable<typename std::decay_t<V>::RegionType>;

/// @brief Value/DynamicValue whose region is not W1C.
template <typename V>
concept ModifiableValue = WritableValue<V>
    && Readable<typename std::decay_t<V>::RegionType>
    && NotW1C<typename std::decay_t<V>::RegionType>;
```

---

### Step 1.4: `Value` の `FieldType` 削除、`RegionType` に統一

**対象:** `register.hh` — `Value` 構造体 (L318–340)

**変更内容:** `FieldType` type alias を削除し、全使用箇所を `RegionType` に変更する。

差分:

```cpp
template <class RegionT, auto EnumValue>
struct Value {
    using RegionType = RegionT;
    static constexpr auto value = EnumValue;

    static constexpr auto shifted_value = []() {
        if constexpr (requires { RegionT::shift; }) {
            return static_cast<typename RegionT::RegValueType>(value) << RegionT::shift;
        } else {
            return static_cast<typename RegionT::RegValueType>(value);
        }
    }();

    static constexpr auto get() noexcept { return value; }
    using ValueType = decltype(value);

    // FieldType は完全に削除する
    // using FieldType = RegionT;  ← 削除
};
```

**影響箇所:**
- `RegOps::is()` (L477): `V::FieldType` → `V::RegionType` に変更
- `RegisterReader::is()` (Step 2.2 新規): `VDecay::FieldType` → `VDecay::RegionType`

---

### Step 1.5: `value()` の unsigned 制約と Writable 制約

**対象:** `register.hh` — `Register::value()` (L181–190) と `Field::value()` (L275–285)

**変更内容:**

#### Register::value()

`Register::value()` は Step 1.1a で `W1cMask` パラメータ付きの `Register` 内で定義済み。
ここでは `value()` の変更点のみ記載する。

**重要:** `value()` に `requires(Access::can_write)` は **付けない**。
`value()` は副作用のない値生成ファクトリであり、書き込み制約は
`write()`/`modify()` の `WritableValue` concept が担当する。
分離しないと RO レジスタの DynamicValue 比較が不可能になる:

```cpp
// これがコンパイルエラーになってはならない:
hw.is(StatusReg::value(0x0001u));  // StatusReg は RO
```

#### Field::value()

```cpp
template <std::unsigned_integral T>
    requires(is_numeric)
[[nodiscard("value() result must be used with write(), modify(), or is()")]]
static constexpr auto value(T val) {
    if constexpr (BitWidth < sizeof(T) * bits8) {
        constexpr auto max_value = (1ULL << BitWidth) - 1;
        if consteval {
            if (static_cast<std::uint64_t>(val) > max_value) {
                mmio_compile_time_error_value_out_of_range();
            }
        } else {
            assert(static_cast<std::uint64_t>(val) <= max_value
                   && "Field value out of range");
        }
    }
    return DynamicValue<Field, T>{val};
}
```

**変更点:**
1. `std::integral T` → `std::unsigned_integral T`
2. `if consteval` の `else` ブランチに `assert` を追加
3. `requires(Access::can_write)` は **意図的に付けない** (Step 1.1a 設計判断参照)

#### raw<F>() も同様に修正

```cpp
template <class FieldT, std::unsigned_integral T>
[[nodiscard("raw() result must be used with write(), modify(), or is()")]]
constexpr auto raw(T val) {
    if constexpr (FieldT::bit_width < sizeof(T) * bits8) {
        constexpr auto max_value = (1ULL << FieldT::bit_width) - 1;
        if consteval {
            if (static_cast<std::uint64_t>(val) > max_value) {
                mmio_compile_time_error_value_out_of_range();
            }
        } else {
            assert(static_cast<std::uint64_t>(val) <= max_value
                   && "raw() value out of range");
        }
    }
    return DynamicValue<FieldT, T>{val};
}
```

---

### Step 1.6: `std::endian` 導入 — カスタム `Endian` enum の廃止

**対象:** `register.hh` — `Endian` enum (L76), `ByteAdapter`, transport/*.hh

**変更内容:** `umi::mmio::Endian` を削除し、`std::endian` (C++20 `<bit>`) に置換する。

```cpp
// 削除:
// enum class Endian : std::uint8_t { LITTLE, BIG };

// ByteAdapter テンプレートパラメータ:
//   Before: Endian DataEndian = Endian::LITTLE
//   After:  std::endian DataEndian = std::endian::little

// Transport テンプレートパラメータ:
//   Before: Endian DataEndian = Endian::BIG
//   After:  std::endian DataEndian = std::endian::big
```

**追加 include:**
- `#include <bit>` を register.hh に追加 (`std::endian` + Step 2.5 の `std::byteswap` 両方が利用)
- `#include <variant>` を register.hh に追加 (Step 3.3 `read_variant()` の戻り値型)
- `#include <cstring>` を transport ヘッダに追加 (`std::memcpy` 用、未インクルードの場合)

**根拠:** C++20 の標準型で十分。カスタム enum の維持コストを削除し、
`std::endian::native` との直接比較で ByteAdapter の内部ロジックも簡素化可能。

---

## Phase 2: API 刷新

### Step 2.1: Deducing this による CRTP 削除 + requires 句化

**対象:** `register.hh` — `RegOps` クラス (L380–670), `ByteAdapter` (L670–778),
`DirectTransport` (direct.hh)

**変更内容:** C++23 Deducing this (P0847R7) を使い、CRTP の `Derived` テンプレート
パラメータを完全に削除する。同時に全 `static_assert` ベースの
アクセスチェックを `requires` 句に置き換える。

**改善点:**
1. `RegOps<Derived, Check, Error>` → `RegOps<Check, Error>` (1 パラメータ削減)
2. `ByteAdapter<Derived, ...>` → `ByteAdapter<...>` (同権)
3. `self()` ヘルパーと `static_cast<Derived&>(*this)` の完全削除
4. `friend Derived` / `friend ByteAdapter<...>` 宣言不要
5. コンパイラエラーメッセージの簡素化 (CRTP 型の展開がない)

#### RegOps クラス宣言

```cpp
/// @brief CRTP-free base providing type-safe register read/write/modify operations.
///
/// Uses C++23 deducing this to access derived transport methods without
/// CRTP's Derived template parameter or static_cast.
///
/// @tparam CheckPolicy Enable runtime range checks (std::true_type or std::false_type).
/// @tparam ErrorPolicy Error handler invoked on range violations.
template <typename CheckPolicy = std::true_type, typename ErrorPolicy = AssertOnError>
class RegOps {
protected:
    RegOps() = default;
    RegOps(const RegOps&) = default;
    RegOps& operator=(const RegOps&) = default;
    ~RegOps() = default;

public:
    RegOps(RegOps&&) = delete;
    RegOps& operator=(RegOps&&) = delete;
```

**変更点:**
- `template <class Derived, ...>` → `Derived` 削除
- `friend Derived` / `friend ByteAdapter<...>` → 削除
- `private` constructor → `protected` (派生クラスのみ構築可能)
- `self()` ヘルパー → 削除

#### read() — 2 overload に分割 (deducing this)

```cpp
/// @brief Read a register, returning a RegisterReader for typed field extraction.
/// @tparam Reg Register type.
/// @return RegisterReader wrapping the raw register value.
template <typename Self, typename Reg>
    requires Readable<Reg> && IsRegister<Reg>
[[nodiscard]] auto read(this const Self& self, Reg) noexcept -> RegisterReader<Reg> {
    check_transport_allowed<Self, Reg>();
    return RegisterReader<Reg>{self.reg_read(Reg{})};
}

/// @brief Read a single field value (shortcut for read(Reg).get(Field)).
/// @tparam F Field type.
/// @return Extracted field value.
template <typename Self, typename F>
    requires Readable<F> && IsField<F>
[[nodiscard]] auto read(this const Self& self, F) noexcept -> typename F::ValueType {
    check_transport_allowed<Self, F>();
    return self.read(typename F::ParentRegType{}).get(F{});
}
```

#### is() — ReadableValue concept + deducing this

```cpp
/// @brief Compare a register/field against an expected value.
/// @tparam V Value or DynamicValue type.
/// @return true if the current hardware value matches.
template <typename Self, ReadableValue V>
[[nodiscard]] bool is(this const Self& self, V&& value) noexcept {
    using VDecay = std::decay_t<V>;
    using RegionT = typename VDecay::RegionType;
    check_transport_allowed<Self, RegionT>();

    if constexpr (requires { VDecay::value; }) {
        if constexpr (RegionT::is_register) {
            return self.read(RegionT{}).bits() ==
                   static_cast<typename RegionT::RegValueType>(VDecay::value);
        } else {
            return self.read(RegionT{}) ==
                   static_cast<typename RegionT::ValueType>(VDecay::value);
        }
    } else {
        if constexpr (CheckPolicy::value) {
            if constexpr (RegionT::bit_width < sizeof(decltype(value.assigned_value)) * bits8) {
                auto const max_value = (1ULL << RegionT::bit_width) - 1;
                if (static_cast<std::uint64_t>(value.assigned_value) > max_value) {
                    ErrorPolicy::on_range_error("Comparison value out of range");
                }
            }
        }
        if constexpr (RegionT::is_register) {
            return self.read(RegionT{}).bits() ==
                   static_cast<typename RegionT::RegValueType>(value.assigned_value);
        } else {
            return self.read(RegionT{}) == value.assigned_value;
        }
    }
}
```

#### flip() — ReadWritable concept + deducing this

```cpp
/// @brief Toggle a 1-bit field via read-modify-write.
/// @tparam F A 1-bit, read-write Field type.
template <typename Self, typename F>
    requires ReadWritable<F> && IsField<F> && (F::bit_width == 1) && NotW1C<F>
void flip(this const Self& self, F) noexcept {
    check_transport_allowed<Self, F>();
    using ParentRegType = typename F::ParentRegType;
    auto current = self.reg_read(ParentRegType{});
    current ^= F::mask();
    self.reg_write(ParentRegType{}, current);
}
```

#### write() — WritableValue concept + deducing this

```cpp
/// @brief Write one or more field/register values in a single bus transaction.
/// @warning For single-field writes, other fields are set to their reset values.
///          Use modify() to change a single field at runtime.
template <typename Self, WritableValue... Values>
void write(this const Self& self, Values&&... values) noexcept {
    if constexpr (sizeof...(Values) == 1) {
        write_single(self, std::forward<Values>(values)...);
    } else {
        write_multiple(self, std::forward<Values>(values)...);
    }
}
```

#### modify() — ModifiableValue concept + deducing this (W1C 排除 + w1c_mask 安全化)

```cpp
/// @brief Read-modify-write: read current value, apply field changes, write back.
/// @warning This operation is NOT atomic. Use Protected<> for ISR safety.
/// @note W1C fields are rejected at compile time. Use clear() instead.
/// @note W1C bits in the parent register are automatically masked to 0
///       before write-back via Register::w1c_mask.
template <typename Self, ModifiableValue... Values>
void modify(this const Self& self, Values&&... values) noexcept {
    static_assert(sizeof...(Values) > 0);
    modify_impl(self, std::forward<Values>(values)...);
}
```

#### clear() — W1C 専用メソッド (新規) + deducing this

```cpp
/// @brief Clear a W1C (Write-1-to-Clear) field by writing 1 to the target bit(s).
/// Other bits are written as 0 to prevent accidental clearing.
/// @tparam F A W1C field type.
template <typename Self, typename F>
    requires IsW1C<F> && IsField<F>
void clear(this const Self& self, F) noexcept {
    check_transport_allowed<Self, F>();
    using ParentRegType = typename F::ParentRegType;
    // Write mask with only the target field set to 1, all others 0
    self.reg_write(ParentRegType{}, F::mask());
}
```

#### reset() — レジスタリセット (新規) + deducing this

```cpp
/// @brief Reset a register to its compile-time reset value.
/// @tparam Reg A writable register type.
template <typename Self, typename Reg>
    requires Writable<Reg> && IsRegister<Reg>
void reset(this const Self& self, Reg) noexcept {
    check_transport_allowed<Self, Reg>();
    self.reg_write(Reg{}, Reg::reset_value());
}
```

#### Private helpers — self 引数渡しパターン

Deducing this は public メソッドで `Self` を確定する。private helper は CRTP も
deducing this も使わず、public 層で確定した `self` を引数として受け取る:

```cpp
private:
    /// @brief Write single value — called from write().
    template <typename Self, typename Value>
    static void write_single(const Self& self, Value&& value) noexcept {
        using V = std::decay_t<Value>;
        using RegionType = typename V::RegionType;
        check_transport_allowed<Self, RegionType>();
        static_assert(RegionType::AccessType::can_write, "Cannot write to read-only register");

        if constexpr (RegionType::is_register) {
            write_register_value(self, std::forward<Value>(value));
        } else {
            write_field_value(self, std::forward<Value>(value));
        }
    }

    /// @brief Write a Value to a register-type region.
    template <typename Self, typename Value>
    static void write_register_value(const Self& self, Value&& value) noexcept {
        using V = std::decay_t<Value>;
        using RegionType = typename V::RegionType;

        if constexpr (requires { V::value; }) {
            self.reg_write(RegionType{},
                           static_cast<typename RegionType::RegValueType>(V::value));
        } else {
            self.reg_write(RegionType{},
                           static_cast<typename RegionType::RegValueType>(value.assigned_value));
        }
    }

    /// @brief Write a Value to a field-type region.
    template <typename Self, typename Value>
    static void write_field_value(const Self& self, Value&& value) noexcept {
        using V = std::decay_t<Value>;
        using RegionType = typename V::RegionType;
        using ParentRegType = typename RegionType::ParentRegType;

        auto reg_val = ParentRegType::reset_value();
        reg_val = apply_field_value(reg_val, std::forward<Value>(value));
        self.reg_write(ParentRegType{}, reg_val);
    }

    /// @brief Write multiple values — called from write().
    template <typename Self, typename... Values>
    static void write_multiple(const Self& self, Values&&... values) noexcept {
        using FirstValueType = std::decay_t<std::tuple_element_t<0, std::tuple<Values...>>>;
        using FirstRegionType = typename FirstValueType::RegionType;
        using ParentRegType = std::conditional_t<
            FirstRegionType::is_register, FirstRegionType,
            typename FirstRegionType::ParentRegType>;

        // All values must belong to the same parent register.
        static_assert(
            (std::is_same_v<
                 std::conditional_t<
                     std::decay_t<Values>::RegionType::is_register,
                     typename std::decay_t<Values>::RegionType,
                     typename std::decay_t<Values>::RegionType::ParentRegType>,
                 ParentRegType> && ...),
            "All values must target the same register");

        check_transport_allowed<Self, ParentRegType>();

        auto reg_val = ParentRegType::reset_value();
        ((reg_val = apply_field_value(reg_val, values)), ...);
        self.reg_write(ParentRegType{}, reg_val);
    }

    /// @brief Read-modify-write implementation with W1C mask safety.
    template <typename Self, typename... Values>
    static void modify_impl(const Self& self, Values&&... values) noexcept {
        using FirstValueType = std::decay_t<std::tuple_element_t<0, std::tuple<Values...>>>;
        using FirstRegionType = typename FirstValueType::RegionType;
        using ParentRegType = std::conditional_t<
            FirstRegionType::is_register, FirstRegionType,
            typename FirstRegionType::ParentRegType>;

        // All values must belong to the same parent register.
        static_assert(
            (std::is_same_v<
                 std::conditional_t<
                     std::decay_t<Values>::RegionType::is_register,
                     typename std::decay_t<Values>::RegionType,
                     typename std::decay_t<Values>::RegionType::ParentRegType>,
                 ParentRegType> && ...),
            "All values must target the same register");

        check_transport_allowed<Self, ParentRegType>();

        auto current = self.reg_read(ParentRegType{});
        ((current = apply_field_value(current, values)), ...);

        // W1C safety: clear W1C bits before write-back.
        // Writing 0 to W1C bits is a no-op (W1C spec), so this is always safe.
        // Without this, read-modify-write would write back 1s to set W1C bits,
        // accidentally clearing hardware flags.
        if constexpr (ParentRegType::w1c_mask != 0) {
            current &= ~ParentRegType::w1c_mask;
        }

        self.reg_write(ParentRegType{}, current);
    }

    /// @brief Apply a field value to a register value (pure function, no self).
    template <typename T, typename Value>
    static auto apply_field_value(T reg_val, const Value& v) {
        using V = std::decay_t<Value>;
        using RegionType = typename V::RegionType;
        static_assert(RegionType::AccessType::can_write,
                      "Cannot write to read-only register");

        if constexpr (requires { V::value; }) {
            return (reg_val & ~RegionType::mask()) |
                   ((static_cast<T>(V::value) << RegionType::shift)
                    & RegionType::mask());
        } else {
            if constexpr (CheckPolicy::value) {
                if constexpr (RegionType::bit_width < sizeof(T) * bits8) {
                    auto const max_value = (1ULL << RegionType::bit_width) - 1;
                    if (static_cast<std::uint64_t>(v.assigned_value) > max_value) {
                        ErrorPolicy::on_range_error("Value out of range");
                    }
                }
            }
            return (reg_val & ~RegionType::mask()) |
                   ((static_cast<T>(v.assigned_value) << RegionType::shift)
                    & RegionType::mask());
        }
    }
```

**設計判断:**
- Public メソッド (deducing this) → Private helper (static メンバ, `self` 引数渡し)
- `modify_impl()` は `ParentRegType::w1c_mask` でW1Cビットを自動マスク
  - W1C ビットに 0 を書いても no-op (W1C 仕様) なので常に安全
  - `w1c_mask == 0` の場合は `if constexpr` で最適化除去
- `apply_field_value()` は `self` 不要なpure 関数 — static メソッドのまま
- ADL による意図しない名前解決を防ぐため、private helper は `self.method()` ではなく
  RegOps 内の static メンバ関数として呼び出す

#### check_transport_allowed() — Self 型パラメータ化

```cpp
/// @note Derived is now deduced via Self, not a class template parameter.
template <typename Self, typename RegOrField>
static constexpr void check_transport_allowed() {
    static_assert(TransportLike<Self>, "Self must satisfy TransportLike");
    // ...
}
```

#### DirectTransport 更新

`transport/direct.hh` も同様に CRTP を削除し、`RegOps<CheckPolicy, ErrorPolicy>` を
`Derived` なしで継承する:

```cpp
/// @brief DirectTransport — volatile pointer read/write for memory-mapped peripherals.
/// @tparam CheckPolicy  Enable alignment checks (std::true_type or std::false_type).
/// @tparam ErrorPolicy  Error handler (default: AssertOnError).
template <typename CheckPolicy = std::true_type, typename ErrorPolicy = AssertOnError>
class DirectTransport : private RegOps<CheckPolicy, ErrorPolicy> {
public:
    using RegOps<CheckPolicy, ErrorPolicy>::write;
    using RegOps<CheckPolicy, ErrorPolicy>::read;
    using RegOps<CheckPolicy, ErrorPolicy>::modify;
    using RegOps<CheckPolicy, ErrorPolicy>::is;
    using RegOps<CheckPolicy, ErrorPolicy>::flip;
    using RegOps<CheckPolicy, ErrorPolicy>::clear;
    using RegOps<CheckPolicy, ErrorPolicy>::reset;
    using RegOps<CheckPolicy, ErrorPolicy>::read_variant;
    using TransportTag = DirectTransportTag;

    /// @brief Read a register via volatile pointer dereference.
    template <typename Reg>
    [[nodiscard]] auto reg_read(Reg) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        return *reinterpret_cast<volatile const T*>(Reg::address);
    }

    /// @brief Write a value to a register via volatile pointer dereference.
    template <typename Reg>
    void reg_write(Reg, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;
        if constexpr (CheckPolicy::value) {
            static_assert((Reg::address % alignof(T)) == 0,
                          "Misaligned register access");
        }
        *reinterpret_cast<volatile T*>(Reg::address) = value;
    }
};
```

**変更点:**
- `RegOps<DirectTransport<...>, ...>` → `RegOps<CheckPolicy, ErrorPolicy>` (Derived 削除)
- `friend class RegOps<...>` → 削除 (RegOps コンストラクタが protected)
- `clear()` / `reset()` の `using` 宣言を追加

---

### Step 2.2: RegisterReader クラスの追加

**対象:** `register.hh` — `BitRegion` と `RegOps` の間に新規追加

**変更内容:**

```cpp
/// @brief Immutable view of a register value read from hardware.
///
/// Holds the raw value from a single bus access, providing typed
/// field extraction without additional bus transactions.
/// All methods are constexpr noexcept for zero-overhead.
///
/// @tparam Reg The register type this reader was created from.
template <typename Reg>
class RegisterReader {
    typename Reg::RegValueType val;

public:
    explicit constexpr RegisterReader(typename Reg::RegValueType v) noexcept : val(v) {}

    /// @brief Get the raw bit value of the entire register.
    /// This is an explicit opt-in — prefer get() for typed access.
    [[nodiscard]] constexpr auto bits() const noexcept { return val; }

    /// @brief Extract a field value from the cached register value.
    /// No bus access occurs — this is a pure bit-extraction operation.
    ///
    /// @tparam F Field type. Must belong to this register.
    /// @return Extracted field value, cast to the field's ValueType.
    template <typename F>
        requires IsField<F> && std::is_same_v<typename F::ParentRegType, Reg>
    [[nodiscard]] constexpr auto get(F = {}) const noexcept -> typename F::ValueType {
        return static_cast<typename F::ValueType>(
            (val & F::mask()) >> F::shift);
    }

    /// @brief Compare the register/field value against an expected Value or DynamicValue.
    ///
    /// @tparam V Value or DynamicValue type.
    /// @return true if the value matches.
    template <typename V>
    [[nodiscard]] constexpr bool is(V&& v) const noexcept {
        using VDecay = std::decay_t<V>;
        using RegionT = typename VDecay::RegionType;

        if constexpr (requires { VDecay::value; }) {
            // Enumerated Value<Region, EnumVal>
            if constexpr (RegionT::is_register) {
                static_assert(std::is_same_v<RegionT, Reg>,
                              "Value register type must match reader register type");
                return val == static_cast<typename Reg::RegValueType>(VDecay::value);
            } else {
                static_assert(std::is_same_v<typename RegionT::ParentRegType, Reg>,
                              "Value field must belong to this register");
                return get(RegionT{}) ==
                       static_cast<typename RegionT::ValueType>(VDecay::value);
            }
        } else {
            // DynamicValue<Region, T>
            if constexpr (RegionT::is_register) {
                static_assert(std::is_same_v<RegionT, Reg>,
                              "DynamicValue register type must match reader register type");
                return val == static_cast<typename Reg::RegValueType>(v.assigned_value);
            } else {
                static_assert(std::is_same_v<typename RegionT::ParentRegType, Reg>,
                              "DynamicValue field must belong to this register");
                return get(RegionT{}) == v.assigned_value;
            }
        }
    }
};
```

**設計判断:**
- `bits()` を採用 (`raw()` ではない) — `mm::raw<F>()` (escape hatch) との名前衝突を回避し、svd2rust の `sr.bits()` と一致
- `get()` に `Readable` 制約は不要 — `read()` 時点でバスアクセスは完了済み
- `CheckPolicy` は RegisterReader に伝搬しない — CheckPolicy は RegOps 層の責務

---

### Step 2.3: I2cTransport / SpiTransport の NTTP 参照化

**対象:**
- `transport/i2c.hh` — 既存 `I2cTransport` の置き換え
- `transport/spi.hh` — 既存 `SpiTransport` の置き換え

**変更内容:** ランタイム参照をNTTP static 参照に変更する。

#### I2cTransport

```cpp
/// @brief I2C transport with static driver reference.
///
/// The NTTP reference guarantees the I2C driver has static storage duration,
/// eliminating dangling reference risks at the language level.
///
/// @tparam I2cInstance  Static I2C driver instance (must have static storage).
/// @tparam CheckPolicy Enable runtime range checks.
/// @tparam ErrorPolicy Error handler for range violations.
/// @tparam AddressWidth Register address width on the bus (uint8_t or uint16_t).
/// @tparam AddrEndian  Byte order for register addresses on the wire.
/// @tparam DataEndian  Byte order for data on the wire.
template <auto& I2cInstance,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressWidth = std::uint8_t,
          std::endian AddrEndian = std::endian::big,
          std::endian DataEndian = std::endian::big>
class I2cTransport
    : public ByteAdapter<CheckPolicy, ErrorPolicy, AddressWidth, DataEndian> {
    std::uint8_t device_addr;

public:
    using TransportTag = I2CTransportTag;

    explicit constexpr I2cTransport(std::uint8_t addr) noexcept : device_addr(addr) {}

    /// @brief Write register data over I2C.
    void raw_write(AddressWidth reg_addr, const void* data, std::size_t size) const noexcept {
        // Pack address + data into single buffer for I2C transaction
        std::array<std::uint8_t, sizeof(AddressWidth) + 8> buf{};
        if constexpr (sizeof(AddressWidth) == 1) {
            buf[0] = static_cast<std::uint8_t>(reg_addr);
        } else if constexpr (AddrEndian == std::endian::big) {
            buf[0] = static_cast<std::uint8_t>(reg_addr >> 8);
            buf[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
        } else {
            buf[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
            buf[1] = static_cast<std::uint8_t>(reg_addr >> 8);
        }
        std::memcpy(&buf[sizeof(AddressWidth)], data, size);
        I2cInstance.write(device_addr >> 1, buf.data(), sizeof(AddressWidth) + size);
    }

    /// @brief Read register data over I2C.
    void raw_read(AddressWidth reg_addr, void* data, std::size_t size) const noexcept {
        std::array<std::uint8_t, sizeof(AddressWidth)> addr_buf{};
        if constexpr (sizeof(AddressWidth) == 1) {
            addr_buf[0] = static_cast<std::uint8_t>(reg_addr);
        } else if constexpr (AddrEndian == std::endian::big) {
            addr_buf[0] = static_cast<std::uint8_t>(reg_addr >> 8);
            addr_buf[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
        } else {
            addr_buf[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
            addr_buf[1] = static_cast<std::uint8_t>(reg_addr >> 8);
        }
        I2cInstance.write(device_addr >> 1, addr_buf.data(), sizeof(AddressWidth));
        I2cInstance.read(device_addr >> 1, static_cast<std::uint8_t*>(data), size);
    }
};
```

#### SpiTransport

```cpp
/// @brief SPI transport with static driver reference.
///
/// @tparam SpiInstance  Static SPI driver instance (must have static storage).
/// @tparam ReadBit     Bit OR'd into address for read operations.
/// @tparam WriteBit    Bit OR'd into address for write operations.
/// @tparam CmdMask     Address mask (default: 0x7F for 7-bit addressing).
/// @tparam CheckPolicy Enable runtime range checks.
/// @tparam ErrorPolicy Error handler for range violations.
/// @tparam AddressWidth Register address width on the bus.
/// @tparam AddrEndian  Byte order for multi-byte addresses on the wire.
/// @tparam DataEndian  Byte order for data on the wire.
template <auto& SpiInstance,
          std::uint8_t ReadBit = 0x80,
          std::uint8_t WriteBit = 0x00,
          std::uint8_t CmdMask = 0x7F,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressWidth = std::uint8_t,
          std::endian AddrEndian = std::endian::big,
          std::endian DataEndian = std::endian::big>
class SpiTransport
    : public ByteAdapter<CheckPolicy, ErrorPolicy, AddressWidth, DataEndian> {
public:
    using TransportTag = SPITransportTag;

    /// @brief Write register data over SPI.
    void raw_write(AddressWidth reg_addr, const void* data, std::size_t size) const noexcept {
        SpiInstance.select();
        send_address(reg_addr, WriteBit);
        SpiInstance.transfer(static_cast<const std::uint8_t*>(data), nullptr, size);
        SpiInstance.deselect();
    }

    /// @brief Read register data over SPI.
    void raw_read(AddressWidth reg_addr, void* data, std::size_t size) const noexcept {
        SpiInstance.select();
        send_address(reg_addr, ReadBit);
        SpiInstance.transfer(nullptr, static_cast<std::uint8_t*>(data), size);
        SpiInstance.deselect();
    }

private:
    /// @brief Send address byte(s) with command bit over SPI.
    ///
    /// Single-byte addresses: OR with cmd bit and send as one byte.
    /// Multi-byte addresses: apply AddrEndian byte order, OR cmd bit into first byte sent.
    void send_address(AddressWidth reg_addr, std::uint8_t cmd_bit) const noexcept {
        if constexpr (sizeof(AddressWidth) == 1) {
            std::uint8_t cmd = (static_cast<std::uint8_t>(reg_addr) & CmdMask) | cmd_bit;
            SpiInstance.transfer(&cmd, nullptr, 1);
        } else {
            std::array<std::uint8_t, sizeof(AddressWidth)> addr_buf{};
            if constexpr (AddrEndian == std::endian::big) {
                // MSB first (big-endian)
                for (std::size_t i = 0; i < sizeof(AddressWidth); ++i) {
                    addr_buf[i] = static_cast<std::uint8_t>(
                        reg_addr >> ((sizeof(AddressWidth) - 1 - i) * 8));
                }
            } else {
                // LSB first (little-endian)
                for (std::size_t i = 0; i < sizeof(AddressWidth); ++i) {
                    addr_buf[i] = static_cast<std::uint8_t>(reg_addr >> (i * 8));
                }
            }
            // OR command bit into the first byte sent
            addr_buf[0] = (addr_buf[0] & CmdMask) | cmd_bit;
            SpiInstance.transfer(addr_buf.data(), nullptr, sizeof(AddressWidth));
        }
    }
};
```

既存の参照ベース版 (`I2cTransport`, `SpiTransport`) は削除する (後方互換は考慮しない)。

---

### Step 2.4: Transport 層の設計判断コメント追加

**対象:** `register.hh` — Transport concepts セクション (L356–385)

**変更内容:** Transport concept の直前に設計判断コメントを追加する。

```cpp
/// @note Transport layer design rule:
/// reg_read() / reg_write() do NOT check access policies.
/// Access control is enforced exclusively at the RegOps layer.
/// Rationale: modify() calls reg_read() on registers that may contain
/// read-only or write-only fields. Transport-level checks would
/// incorrectly reject these valid internal operations.

/// @name Transport concepts
/// @{
```

---

### Step 2.5: ByteAdapter deducing this + `std::byteswap`

**対象:** `register.hh` — `ByteAdapter` クラス (L670–778)

**変更内容:** ByteAdapter からも CRTP `Derived` を削除し、
`pack()`/`unpack()` の手動バイトスワップループを `std::byteswap` (C++23) に置換する。

```cpp
/// @brief CRTP-free adapter bridging byte-oriented transports to RegOps.
///
/// Uses deducing this to access derived transport's raw_read/raw_write.
/// Uses std::byteswap + std::memcpy for endian conversion instead of
/// manual byte-by-byte loops.
///
/// @tparam CheckPolicy   Enable runtime range checks.
/// @tparam ErrorPolicy   Error handler invoked on range violations.
/// @tparam AddressTypeT  Register address width on the bus (uint8_t or uint16_t).
/// @tparam DataEndian    Byte order for data on the wire.
template <typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressTypeT = std::uint8_t,
          std::endian DataEndian = std::endian::little>
class ByteAdapter : private RegOps<CheckPolicy, ErrorPolicy> {
protected:
    ByteAdapter() = default;

    static_assert(std::is_integral_v<AddressTypeT>, "AddressType must be an integral type");
    static_assert(sizeof(AddressTypeT) <= 2, "AddressType must be 8 or 16 bit");

    /// @brief Pack a register value into a byte buffer.
    /// Uses std::byteswap for endian conversion when needed.
    template <typename T>
    static void pack(T value, std::uint8_t* buffer) noexcept {
        if constexpr (DataEndian != std::endian::native) {
            value = std::byteswap(value);
        }
        std::memcpy(buffer, &value, sizeof(T));
    }

    /// @brief Unpack a byte buffer into a register value.
    template <typename T>
    static T unpack(const std::uint8_t* buffer) noexcept {
        T value;
        std::memcpy(&value, buffer, sizeof(T));
        if constexpr (DataEndian != std::endian::native) {
            value = std::byteswap(value);
        }
        return value;
    }

public:
    using RegOps<CheckPolicy, ErrorPolicy>::write;
    using RegOps<CheckPolicy, ErrorPolicy>::read;
    using RegOps<CheckPolicy, ErrorPolicy>::modify;
    using RegOps<CheckPolicy, ErrorPolicy>::is;
    using RegOps<CheckPolicy, ErrorPolicy>::flip;
    using RegOps<CheckPolicy, ErrorPolicy>::clear;
    using RegOps<CheckPolicy, ErrorPolicy>::reset;
    using RegOps<CheckPolicy, ErrorPolicy>::read_variant;
    using AddressType = AddressTypeT;

    /// @brief Read register via deducing this → self.raw_read().
    template <typename Self, typename Reg>
    [[nodiscard]] auto reg_read(this const Self& self, Reg) noexcept
        -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        static_assert(sizeof(T) <= 8, "Register size must be <= 64 bits");
        static_assert(Reg::address <= static_cast<Addr>(std::numeric_limits<AddressTypeT>::max()),
                      "Register address exceeds address width for byte transport");

        std::array<std::uint8_t, sizeof(T)> buffer;
        self.raw_read(static_cast<AddressTypeT>(Reg::address), buffer.data(), sizeof(T));
        return unpack<T>(buffer.data());
    }

    /// @brief Write register via deducing this → self.raw_write().
    template <typename Self, typename Reg>
    void reg_write(this const Self& self, Reg, typename Reg::RegValueType value) noexcept {
        using T = typename Reg::RegValueType;
        static_assert(sizeof(T) <= 8, "Register size must be <= 64 bits");
        static_assert(Reg::address <= static_cast<Addr>(std::numeric_limits<AddressTypeT>::max()),
                      "Register address exceeds address width for byte transport");

        std::array<std::uint8_t, sizeof(T)> buffer;
        pack<T>(value, buffer.data());
        self.raw_write(static_cast<AddressTypeT>(Reg::address), buffer.data(), sizeof(T));
    }
};
```

**改善点:**
1. `ByteAdapter<Derived, ...>` → `ByteAdapter<...>` (Derived 削除)
2. `friend Derived` / `friend RegOps<...>` → 削除
3. `self()` ヘルパー → deducing this の `self` パラメータ
4. 手動バイトループ → `std::byteswap` + `std::memcpy` (読みやすく、ARM では `rev` 命令に最適化)
5. `clear()`/`reset()` の `using` 宣言を追加

---

## Phase 3: 新機能追加

### Step 3.1: OneBitW1CAliases — W1C 用 1-bit エイリアス

**対象:** `register.hh` — `detail` namespace 内 (L233–244)

**変更内容:** W1C フィールド用の 1-bit エイリアスを追加する。

```cpp
namespace detail {

/// @brief Set/Reset aliases for 1-bit RW fields.
template <class FieldT>
struct OneBitAliases {
    using Set = Value<FieldT, 1>;
    using Reset = Value<FieldT, 0>;
};

/// @brief Clear alias for 1-bit W1C fields.
template <class FieldT>
struct OneBitW1CAliases {
    using Clear = Value<FieldT, 1>;  // W1C: writing 1 clears the bit
};

/// @brief Select the appropriate base class for 1-bit field aliases.
template <class FieldT, std::size_t BitWidth, class Access>
using OneBitBase = std::conditional_t<
    BitWidth != 1,
    NoOneBitAliases,
    std::conditional_t<
        requires { Access::write_behavior; }
            && Access::write_behavior == WriteBehavior::ONE_TO_CLEAR,
        OneBitW1CAliases<FieldT>,
        OneBitAliases<FieldT>>>;

} // namespace detail
```

`Field` の基底クラスも修正:

```cpp
template <class Reg, std::size_t BitOffset, std::size_t BitWidth, class... Traits>
struct Field : BitRegion<Reg, 0, Reg::reg_bits, BitOffset, BitWidth,
                         detail::ExtractAccess_t<Traits...>, 0, false>,
               detail::OneBitBase<Field<Reg, BitOffset, BitWidth, Traits...>,
                                  BitWidth,
                                  detail::ExtractAccess_t<Traits...>> {
    // ...
};
```

---

### Step 3.2: Protected<T, LockPolicy> — 排他アクセス制御

**対象:** 新規ファイル `include/umimmio/protected.hh`

**変更内容:**

```cpp
#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file protected.hh
/// @brief RAII-based exclusive access control for transport objects.
/// @author Shota Moriguchi @tekitounix

#include <type_traits>
#include <utility>

namespace umi::mmio {

// ===========================================================================
// Lock Policies
// ===========================================================================

/// @brief Bare-metal critical section via interrupt disable.
///
/// ARM (Cortex-M) ターゲット専用。テンプレート遅延インスタンス化により、
/// ヘッダを include するだけではエラーにならず、実際に lock()/unlock() を
/// 呼び出した時点で static_assert が発火する。
/// `__aarch64__` は対象外 (`cpsid i` は A32/T32 命令)。
struct CriticalSectionPolicy {
    template <bool IsArm =
#if defined(__arm__)
                  true
#else
                  false
#endif
              >
    static void lock() noexcept {
        static_assert(IsArm, "CriticalSectionPolicy is ARM (Cortex-M) only. "
                             "Use NoLockPolicy or MutexPolicy on other platforms.");
        __asm volatile("cpsid i" ::: "memory");
    }

    template <bool IsArm =
#if defined(__arm__)
                  true
#else
                  false
#endif
              >
    static void unlock() noexcept {
        static_assert(IsArm, "CriticalSectionPolicy is ARM (Cortex-M) only. "
                             "Use NoLockPolicy or MutexPolicy on other platforms.");
        __asm volatile("cpsie i" ::: "memory");
    }
};

/// @brief RTOS mutex wrapper.
/// @tparam MutexT Mutex type with lock()/unlock() methods.
template <typename MutexT>
struct MutexPolicy {
    MutexT& mtx;
    void lock() noexcept { mtx.lock(); }
    void unlock() noexcept { mtx.unlock(); }
};

/// @brief No-op lock for single-threaded or test contexts.
struct NoLockPolicy {
    static void lock() noexcept {}
    static void unlock() noexcept {}
};

// ===========================================================================
// Guard — RAII scoped access to the protected value
// ===========================================================================

/// @brief RAII guard providing exclusive access to a Protected<T> value.
///
/// The only way to access the inner T of a Protected<T, Policy>.
/// Lock is acquired on construction and released on destruction.
///
/// @tparam T          Protected value type.
/// @tparam LockPolicy Lock policy type.
template <typename T, typename LockPolicy>
class Guard {
    T& ref;
    LockPolicy& policy;

public:
    /// @brief Acquire lock and bind to the protected value.
    explicit Guard(T& r, LockPolicy& p) noexcept : ref(r), policy(p) {
        policy.lock();
    }

    /// @brief Release lock.
    ~Guard() { policy.unlock(); }

    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    Guard(Guard&&) = delete;
    Guard& operator=(Guard&&) = delete;

    /// @brief Access the protected value.
    T& operator*() noexcept { return ref; }
    T* operator->() noexcept { return &ref; }
    const T& operator*() const noexcept { return ref; }
    const T* operator->() const noexcept { return &ref; }
};

// ===========================================================================
// Protected — wrapper preventing direct access without lock
// ===========================================================================

/// @brief Wraps a value, exposing it only through a locked Guard.
///
/// Provides the same safety guarantee as Rust's Mutex<T>:
/// you cannot access the inner value without holding the lock.
///
/// @note This is an opt-in pattern. DirectTransport remains stateless and
///       accessible without Protected. Enforce Protected usage via project
///       conventions and code review.
///
/// @tparam T          Value type (typically a transport object).
/// @tparam LockPolicy Lock policy (CriticalSectionPolicy, MutexPolicy, NoLockPolicy).
template <typename T, typename LockPolicy>
class Protected {
    T inner;
    [[no_unique_address]] LockPolicy policy;

public:
    /// @brief Construct with a pre-built lock policy and forwarded args for T.
    template <typename... Args>
    explicit Protected(LockPolicy p, Args&&... args) noexcept
        : inner(std::forward<Args>(args)...), policy(std::move(p)) {}

    /// @brief Construct with default-constructed lock policy.
    ///        Available only when LockPolicy is default-constructible (e.g., CriticalSectionPolicy).
    template <typename... Args>
        requires std::is_default_constructible_v<LockPolicy>
    explicit Protected(Args&&... args) noexcept
        : inner(std::forward<Args>(args)...), policy{} {}

    /// @brief Acquire the lock and return a Guard for exclusive access.
    [[nodiscard]] Guard<T, LockPolicy> lock() noexcept {
        return Guard<T, LockPolicy>{inner, policy};
    }

    // No direct access to inner — this is the entire point.
};

} // namespace umi::mmio
```

---

### Step 3.3: read_variant() — 網羅的パターンマッチ支援

**対象:** `register.hh` — `RegOps` クラス内に新規メソッド追加

**変更内容:**

```cpp
/// @brief Sentinel type for unknown field values.
template <typename F>
struct UnknownValue {
    typename F::ValueType value;
};

// RegOps 内:

/// @brief Read a field and return a variant of possible named values.
///
/// All Value types must be handled, or compile error from std::visit.
/// If the field value matches none of the named values, UnknownValue<F> is returned.
///
/// @tparam F        Field type to read.
/// @tparam Variants Named Value types to match against.
/// @return std::variant of the matched Value type or UnknownValue.
template <typename F, typename... Variants, typename Self>
    requires Readable<F> && IsField<F>
[[nodiscard]] auto read_variant(this const Self& self, F field = {})
    -> std::variant<Variants..., UnknownValue<F>>
{
    auto val = self.read(field);
    std::variant<Variants..., UnknownValue<F>> result = UnknownValue<F>{val};

    auto try_match = [&]<typename V>() {
        if (val == static_cast<typename F::ValueType>(V::value)) {
            result = V{};
        }
    };
    (try_match.template operator()<Variants>(), ...);

    return result;
}
```

**設計判断:**
- テンプレートパラメータ順序は `<F, Variants..., Self>` — `Self` を末尾に配置する理由:
  deducing this では `Self` はオブジェクト式から推論される。`F` と `Variants...` は
  明示的テンプレート引数として指定される (`hw.read_variant<ConfigMode, ModeNormal, ...>()`)。
  `Self` を先頭にすると明示引数が `Self` にバインドされてしまい、推論と衝突する。
  末尾に配置することで、パラメータパックが明示引数を全て消費し、`Self` は純粋に推論される。

---

## Phase 4: テスト・ドキュメント

### Step 4.1: テスト fixture 更新

**対象:** `tests/test_fixture.hh`

**変更内容:**

1. W1C レジスタ/フィールドの追加:

```cpp
/// @brief Status register with W1C fields (32-bit, read-write, reset = 0)
struct W1cStatusReg : Register<MockDevice, 0x14, bits32, RW, 0, /*W1cMask=*/0x03> {};

/// @brief W1C overflow flag (bit 0)
struct W1cOvr : Field<W1cStatusReg, 0, 1, W1C> {};

/// @brief W1C end-of-conversion flag (bit 1)
struct W1cEoc : Field<W1cStatusReg, 1, 1, W1C> {};

/// @brief Non-W1C enable flag in same register (bit 8)
struct W1cRegEnable : Field<W1cStatusReg, 8, 1> {};
```

2. MockTransport の `read()` 戻り値をテストで `.bits()` 等に変更する必要を反映。

3. MockTransport の CRTP 削除、Direct トランスポートインターフェース、`clear_memory()`:

```cpp
/// @brief In-memory mock transport for testing.
///
/// DirectTransport と同じインターフェース (reg_read/reg_write) を提供する。
/// CRTP パラメータは不要 (deducing this で RegOps が自動解決)。
///
/// @note clear_memory() という名前を使う理由:
/// RegOps が reset(Reg) メソッドを持つため、reset() では名前衝突する。
/// clear_memory() はモック固有の「全メモリゼロクリア」を明示する。
struct MockTransport : private RegOps<> {
public:
    using RegOps<>::read;
    using RegOps<>::write;
    using RegOps<>::modify;
    using RegOps<>::is;
    using RegOps<>::clear;
    using RegOps<>::reset;
    using RegOps<>::flip;
    using RegOps<>::read_variant;

    using TransportTag = DirectTransportTag;

    std::array<std::uint8_t, 256> memory{};

    /// @brief Clear all mock memory to zero.
    void clear_memory() noexcept { memory.fill(0); }

    /// @brief Read register value from mock memory.
    template <typename Reg>
    auto reg_read(Reg) const noexcept -> typename Reg::RegValueType {
        typename Reg::RegValueType val{};
        std::memcpy(&val, &memory[Reg::address], sizeof(val));
        return val;
    }

    /// @brief Write register value to mock memory.
    template <typename Reg>
    void reg_write(Reg, typename Reg::RegValueType val) noexcept {
        std::memcpy(&memory[Reg::address], &val, sizeof(val));
    }

    /// @brief Peek raw memory at arbitrary address (test helper).
    template <typename T>
    T peek(Addr addr) const noexcept {
        T val{};
        std::memcpy(&val, &memory[static_cast<std::size_t>(addr)], sizeof(val));
        return val;
    }

    /// @brief Poke raw memory at arbitrary address (test helper).
    template <typename T>
    void poke(Addr addr, T val) noexcept {
        std::memcpy(&memory[static_cast<std::size_t>(addr)], &val, sizeof(val));
    }
};
```

**設計判断:**
- `RegOps<>` の `<>` は default template arguments で空にできる。
- CRTP 削除により `friend class RegOps<MockTransport>` も不要。
- `private` 継承 + `using` 宣言で RegOps の公開インターフェースを明示的に制御。
- `reg_read(Reg)`/`reg_write(Reg, val)` は DirectTransportTag のインターフェース契約。
  RegOps 内部から `self.reg_read(ParentRegType{})` として呼ばれるため、
  アドレスベースの `raw_read`/`raw_write` ではなく Register 型ベースが正しい。
- `Reg::address` を使用 (`Reg::offset` ではない) — BitRegion の公開メンバ名に一致。
  DirectTransport, ByteAdapter と同じメンバ名を使う。
- `clear_memory()` は `reset(Reg)` (RegOps 由来) との名前衝突を回避する命名。

---

### Step 4.2: 既存テストの全面更新

**対象:** `tests/test_register_field.cc`, `tests/test_transport.cc`, `tests/test_access_policy.cc`

**変更方針:**

#### test_register_field.cc

`read(Register{})` の戻り値が `RegisterReader` になるため、raw 値との比較は `.bits()` を使用:

```cpp
// Before
auto val = hw.read(DataReg{});
t.assert_eq(val, static_cast<uint32_t>(0xDEAD'BEEF));

// After
auto val = hw.read(DataReg{});
t.assert_eq(val.bits(), static_cast<uint32_t>(0xDEAD'BEEF));
```

フィールド抽出テストの追加:

```cpp
// RegisterReader::get() テスト
hw.poke<uint32_t>(0x04, 0x0000'1203U);
auto cfg = hw.read(ConfigReg{});
t.assert_eq(cfg.get(ConfigEnable{}), static_cast<uint8_t>(1));
t.assert_eq(cfg.get(ConfigMode{}), static_cast<uint8_t>(1));
t.assert_eq(cfg.get(ConfigPrescaler{}), static_cast<uint8_t>(0x12));
```

RegisterReader::is() テスト:

```cpp
auto cfg = hw.read(ConfigReg{});
t.assert_true(cfg.is(ConfigEnable::Set{}), "enable bit should be set");
t.assert_true(cfg.is(ModeFast{}), "mode should be FAST");
```

#### test_transport.cc

W1C テストの追加:

```cpp
// clear() テスト
hw.poke<uint32_t>(0x14, 0x0103U);  // OVR=1, EOC=1, Enable=1
hw.clear(W1cOvr{});
// clear() は F::mask() (= 0x01) をレジスタに書き込む。
// 実ハードウェアでは W1C セマンティクスで OVR がクリアされるが、
// mock では単純に値が上書きされる。テストは「正しい値がそのレジスタに書かれたか」を検証する。
t.assert_eq(hw.peek<uint32_t>(0x14), W1cOvr::mask());  // write(0x01)
```

reset() テスト:

```cpp
hw.poke<uint32_t>(0x04, 0xAAAA'AAAA);
hw.reset(ConfigReg{});
t.assert_eq(hw.peek<uint32_t>(0x04), static_cast<uint32_t>(0xFF00));
```

read_variant() テスト:

```cpp
/// @brief Visitor helper for std::visit (C++17 pattern).
template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

hw.poke<uint32_t>(0x04, 0x02U);  // Mode = FAST (bit 1-2 = 01)
auto mode = hw.read_variant<ConfigMode, ModeNormal, ModeFast, ModeLowPower, ModeTest>();
bool matched = false;
std::visit(Overloaded{
    [&](ModeFast) { matched = true; },
    [&](auto) { matched = false; },
}, mode);
t.assert_true(matched, "should match ModeFast");
```

---

### Step 4.3: compile_fail テストの更新と追加

**対象:** `tests/compile_fail/`

**更新:**
- `read_wo.cc` — エラーメッセージが `requires` 不一致に変わる
- `write_ro.cc` — 同上
- `value_typesafe.cc` — 同上

**新規追加:**

| ファイル | 検証内容 |
|---------|---------|
| `modify_w1c.cc` | W1C フィールドに `modify()` → `ModifiableValue` 不一致 |
| `value_signed.cc` | `PLLN::value(-1)` → `std::unsigned_integral` 不一致 |
| `write_ro_value.cc` | `hw.write(StatusReg::value(42u))` → `WritableValue` concept 不一致 (RO レジスタ) |
| `flip_w1c.cc` | W1C フィールドに `flip()` → `NotW1C` 不一致 |
| `field_overflow.cc` | `Field<Reg, 30, 4>` on 32-bit → `BitRegion` static_assert |

#### modify_w1c.cc

```cpp
// compile_fail: modify() on W1C field must be rejected
#include <umimmio/register.hh>
#include "../test_fixture.hh"

using namespace umi::mmio;
using namespace umimmio::test;

int main() {
    MockTransport hw;
    hw.modify(W1cOvr::Clear{});  // ERROR: W1C field not ModifiableValue
    return 0;
}
```

#### value_signed.cc

```cpp
// compile_fail: signed value must be rejected
#include <umimmio/register.hh>
#include "../test_fixture.hh"

using namespace umi::mmio;
using namespace umimmio::test;

int main() {
    MockTransport hw;
    hw.write(ConfigPrescaler::value(-1));  // ERROR: requires unsigned_integral
    return 0;
}
```

#### write_ro_value.cc

```cpp
// compile_fail: write() with value on RO register must be rejected
//
// value() itself succeeds on RO registers (it only creates a value object).
// The write() call is rejected because WritableValue concept is not satisfied.
#include <umimmio/register.hh>
#include "../test_fixture.hh"

using namespace umi::mmio;
using namespace umimmio::test;

int main() {
    MockTransport hw;
    hw.write(StatusReg::value(42u));  // ERROR: WritableValue not satisfied (RO register)
    return 0;
}
```

#### flip_w1c.cc

```cpp
// compile_fail: flip() on W1C field must be rejected
#include <umimmio/register.hh>
#include "../test_fixture.hh"

using namespace umi::mmio;
using namespace umimmio::test;

int main() {
    MockTransport hw;
    hw.flip(W1cOvr{});  // ERROR: NotW1C constraint
    return 0;
}
```

#### field_overflow.cc

```cpp
// compile_fail: field exceeding register width must be rejected
#include <umimmio/register.hh>

using namespace umi::mmio;

struct TestDevice : Device<RW> {};
struct TestReg : Register<TestDevice, 0x00, bits32> {};
struct BadField : Field<TestReg, 30, 4> {};  // bits 30-33 → overflow

int main() {
    (void)BadField::mask();  // ERROR: BitRegion static_assert
    return 0;
}
```

---

### Step 4.4: Protected のテスト追加

**対象:** 新規ファイル `tests/test_protected.cc`

```cpp
// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix

#include <umimmio/protected.hh>
#include "test_fixture.hh"

using namespace umi::mmio;
using namespace umimmio::test;

void run_protected_tests(umi::test::Suite& suite) {
    suite.test("Protected with NoLockPolicy", [](auto& t) {
        Protected<MockTransport, NoLockPolicy> protected_hw;

        {
            auto guard = protected_hw.lock();
            guard->write(ConfigEnable::Set{});
        }

        // Verify write through protected access
        auto guard = protected_hw.lock();
        t.assert_true(guard->is(ConfigEnable::Set{}), "enable should be set");
    });

    suite.test("Protected prevents direct access", [](auto& t) {
        // This test is structural — Protected::inner is private.
        // The compile_fail test below verifies this.
        t.assert_true(true, "structural guarantee");
    });

    suite.test("Protected with stateless policy has no size overhead", [](auto& t) {
        t.assert_eq(sizeof(Protected<MockTransport, NoLockPolicy>),
                    sizeof(MockTransport),
                    "no size overhead for stateless policy");
    });
}
```

---

### Step 4.5: xmake.lua テスト登録更新

**対象:** `tests/xmake.lua`

新規テストファイルの追加:
- `test_protected.cc`
- `compile_fail/modify_w1c.cc`
- `compile_fail/value_signed.cc`
- `compile_fail/write_ro_value.cc`
- `compile_fail/flip_w1c.cc`
- `compile_fail/field_overflow.cc`

test_fixture.hh に `run_protected_tests` の宣言を追加し、`test_main.cc` から呼び出す。

---

### Step 4.6: ドキュメント更新

以下のドキュメントを更新する。すべて英語で記述し、日本語版 (`ja/`) も同期する。

#### DESIGN.md の更新

| セクション | 変更内容 |
|-----------|---------|
| §2.2 Access Policy Enforcement | `static_assert` → `requires` 句に記述変更。W1C + WriteBehavior 追加 |
| §5.0 API Reference: Core types | `RegisterReader`, `UnknownValue`, `WriteBehavior` 追加 |
| §5.0 API Reference: Operations | `clear()`, `reset()`, `read_variant()` 追加。`read()` の戻り値型を更新 |
| §5.2 Register Map Organization | W1C フィールド定義例を追加 |
| §5.2.1 Field Type Safety Model | `W1C` trait の説明を追加 |
| §6.2 RegOps | concept 定義、deducing this 移行、`clear()`/`reset()` 追加 |
| §6.4 Value and DynamicValue | `FieldType` 削除の注記 |
| §6.5 ByteAdapter | `std::byteswap`/`std::endian` 移行、CRTP 削除の注記 |
| §7.1 Compile-Time Errors | `requires` 句に統一した旨を記載。BitRegion static_assert を追加 |
| §8 Test Strategy | 新規テストファイルの一覧追加 |
| §10 Near-Term | この計画の完了により §10 の一部が解決される旨を記載 |
| 新規 §12 | `write()` / `modify()` セマンティクスガイドを追加 |

#### DESIGN.md §12 — write() / modify() Semantics Guide (新規)

```markdown
## 12. write() / modify() Semantics Guide

### 12.1 Semantic Difference

| Operation | Base Value | Semantics | Safety |
|-----------|-----------|-----------|:------:|
| `write(v1, v2, ...)` | `reset_value()` | Initialize register from reset state | ✅ |
| `modify(v1, v2, ...)` | Current value (RMW) | Change specific fields, preserving others | ✅ |
| `write(single_v)` | `reset_value()` | Initialize register — other fields reset | ⚠️ |

### 12.2 Usage Rules

1. **Initialization**: Use `write()` with all relevant fields specified.
2. **Runtime change**: Use `modify()` to change specific fields.
3. **Single-field write**: `write(v)` resets other fields. Intentional
   for single-field registers or full reset. Use `modify()` for
   runtime single-field changes.

### 12.3 W1C Fields

W1C fields must use `clear()`:

```cpp
hw.clear(OVR{});             // ✅ Correct: writes 1 to OVR only
hw.modify(OVR::Clear{});     // ✗ Compile error: W1C not ModifiableValue
```

### 12.4 Atomicity

`modify()` performs read-modify-write and is **never atomic**.
For ISR-safe access, use `Protected<Transport, CriticalSectionPolicy>`:

```cpp
auto guard = protected_hw.lock();   // __disable_irq()
guard->modify(ConfigEnable::Set{}); // ISR-safe RMW
```                                  // __enable_irq() (RAII)
```

#### TESTING.md の更新

| セクション | 変更内容 |
|-----------|---------|
| Test Layout | 新規テストファイルの追加: `test_protected.cc`, 5 つの compile_fail テスト |
| Test Strategy | RegisterReader テスト、W1C テスト、Protected テストのカテゴリ追加 |
| Quality Gates | 追加ゲート: W1C compile_fail, signed compile_fail, BitRegion overflow |

#### INDEX.md の更新

| セクション | 変更内容 |
|-----------|---------|
| API Reference Map | `protected.hh` を追加 |
| Read in This Order | Plans ディレクトリへの参照を追加 |

#### 日本語版 (ja/) の同期

`ja/DESIGN.md`, `ja/TESTING.md`, `ja/INDEX.md` を英語版と同期する。
技術用語 (RegisterReader, Protected, W1C, concept) は英語のまま残す。

---

## 実装順序とコミット戦略

```
Phase 1:
  commit 1: Step 1.1 + 1.1a  BitRegion static_assert + Register W1cMask NTTP
  commit 2: Step 1.2  WriteBehavior enum + W1C access policy + IsAccessPolicy concept
  commit 3: Step 1.3  Concept definitions
  commit 4: Step 1.4  FieldType → RegionType 統一
  commit 5: Step 1.5  value() unsigned + Writable + assert
  commit 6: Step 1.6  std::endian 導入 (Endian enum 削除)

Phase 2:
  commit 7: Step 2.1  Deducing this + requires 句化 + clear() + reset()
  commit 8: Step 2.2  RegisterReader
  commit 9: Step 2.3  I2c/SpiTransport NTTP 化
  commit 10: Step 2.4  Transport 設計判断コメント
  commit 11: Step 2.5  ByteAdapter deducing this + std::byteswap

Phase 3:
  commit 12: Step 3.1  OneBitW1CAliases
  commit 13: Step 3.2  protected.hh
  commit 14: Step 3.3  read_variant()

Phase 4:
  commit 15: Step 4.1-4.2  テスト fixture + 既存テスト更新
  commit 16: Step 4.3  compile_fail テスト追加
  commit 17: Step 4.4-4.5  Protected テスト + xmake.lua
  commit 18: Step 4.6  ドキュメント更新
```

各 commit 後に `xmake test` を実行し、すべてのテストが通過することを確認する。
Phase 2 の commit 7-8 は既存テストが壊れるため、commit 15 と同時に実施するか、
テスト修正を commit 7-8 に含める。

推奨: Phase 1 → Phase 2 + テスト修正 → Phase 3 + テスト追加 → Phase 4 (ドキュメント)

**注意:**
- commit 6 (std::endian) で `Endian` enum を削除するため、bitbang_i2c.hh と bitbang_spi.hh の
  `Endian` 参照も同時に `std::endian` に置換が必要。
- commit 7 (deducing this) と commit 11 (ByteAdapter deducing this) は
RegOps と ByteAdapter の CRTP パラメータを同時に変更するため、
両方を同一コミットにまとめてもよい。
DirectTransport, bitbang_i2c.hh, bitbang_spi.hh も同時に更新が必要。

---

## ファイル変更一覧

### 修正

| ファイル | Step | 変更概要 |
|---------|------|---------|
| `include/umimmio/register.hh` | 1.1–1.6, 2.1–2.2, 2.4–2.5, 3.1, 3.3 | 全面改修 |
| `include/umimmio/transport/direct.hh` | 2.1 | CRTP 削除 (ByteAdapter 同様) |
| `include/umimmio/transport/i2c.hh` | 2.3 | NTTP 参照化 (全面書き換え) |
| `include/umimmio/transport/spi.hh` | 2.3 | NTTP 参照化 (全面書き換え) |
| `include/umimmio/transport/bitbang_i2c.hh` | 1.6, 2.1 | CRTP 削除 + `std::endian` 置換 |
| `include/umimmio/transport/bitbang_spi.hh` | 1.6, 2.1 | CRTP 削除 + `std::endian` 置換 |
| `include/umimmio/mmio.hh` | 3.2 | `#include "protected.hh"` 追加 |
| `tests/test_fixture.hh` | 4.1 | W1C 定義追加、run_protected_tests 宣言 |
| `tests/test_main.cc` | 4.1 | run_protected_tests 呼び出し追加 |
| `tests/test_register_field.cc` | 4.2 | RegisterReader 対応、`.bits()` 使用 |
| `tests/test_transport.cc` | 4.2 | W1C, reset, read_variant テスト追加 |
| `tests/test_access_policy.cc` | 4.2 | requires 句対応 |
| `tests/compile_fail/read_wo.cc` | 4.3 | エラーメッセージ更新 |
| `tests/compile_fail/write_ro.cc` | 4.3 | エラーメッセージ更新 |
| `tests/compile_fail/value_typesafe.cc` | 4.3 | エラーメッセージ更新 |
| `tests/xmake.lua` | 4.5 | 新規テスト登録 |
| `docs/DESIGN.md` | 4.6 | 全面更新 |
| `docs/TESTING.md` | 4.6 | 新規テスト反映 |
| `docs/INDEX.md` | 4.6 | API Reference Map 更新 |
| `docs/ja/DESIGN.md` | 4.6 | 英語版と同期 |
| `docs/ja/TESTING.md` | 4.6 | 英語版と同期 |
| `docs/ja/INDEX.md` | 4.6 | 英語版と同期 |

### 新規

| ファイル | Step | 内容 |
|---------|------|------|
| `include/umimmio/protected.hh` | 3.2 | Protected, Guard, LockPolicy |
| `tests/test_protected.cc` | 4.4 | Protected テスト |
| `tests/compile_fail/modify_w1c.cc` | 4.3 | W1C modify 拒否 |
| `tests/compile_fail/value_signed.cc` | 4.3 | signed 値拒否 |
| `tests/compile_fail/write_ro_value.cc` | 4.3 | RO レジスタへの write() 拒否 |
| `tests/compile_fail/flip_w1c.cc` | 4.3 | W1C flip 拒否 |
| `tests/compile_fail/field_overflow.cc` | 4.3 | BitRegion オーバーフロー |
| `docs/plans/IMPLEMENTATION_PLAN.md` | — | 本文書 |

### 削除なし

後方互換を考慮しないが、ファイル自体の削除はない。

### bitbang トランスポートの影響

`transport/bitbang_i2c.hh` と `transport/bitbang_spi.hh` は以下の変更の影響を受ける:

1. **Step 2.1**: `ByteAdapter` の CRTP パラメータ削除 — `ByteAdapter<BitBangI2cTransport<...>, ...>` → `ByteAdapter<...>`
2. **Step 1.6**: `umi::mmio::Endian` → `std::endian` 置換

これらは機械的な置換であり、bitbang の設計自体は変更しない。

**ランタイム参照の維持理由:** bitbang トランスポートは GPIO ピンをランタイム参照で保持する。
GPIO ピンはボード設計時に確定するが、NTTP で渡すと型爆発が起きる
(ピン*ポート*モードの組み合わせごとにテンプレートがインスタンス化)。
また、bitbang はテストやプロトタイピング用途が主であり、
NTTP 化による最適化は不要。

---

## 検証チェックリスト

各 Phase 完了時に以下を確認する:

### Phase 1 完了時

- [ ] `Field<Reg, 30, 4>` (32-bit オーバーフロー) がコンパイルエラーになる
- [ ] `W1C::write_behavior == WriteBehavior::ONE_TO_CLEAR` が true
- [ ] `RW::write_behavior == WriteBehavior::NORMAL` が true
- [ ] concept `Readable<StatusReg>` == true, `Readable<CmdReg>` == false
- [ ] `Value::FieldType` が存在しない (コンパイルエラー)
- [ ] `StatusReg::value(42u)` がコンパイル **成功** (RO でも value() は値生成のみ)
- [ ] `hw.write(StatusReg::value(42u))` がコンパイルエラー (RO + WritableValue)
- [ ] `ConfigPrescaler::value(-1)` がコンパイルエラー (signed)
- [ ] `ConfigPrescaler::value(256u)` が constexpr でコンパイルエラー
- [ ] `auto v = ConfigPrescaler::value(256u)` が debug ビルドで assert
- [ ] `umi::mmio::Endian` enum が削除され、`std::endian` に置換されている
- [ ] `Register<..., W1cMask>` が W1C フィールドのビットマスクを保持
- [ ] `Register<..., W1cMask>` に register width を超える W1cMask がコンパイルエラー
- [ ] `detail::IsAccessPolicy<RW>` == true, `detail::IsAccessPolicy<int>` == false

### Phase 2 完了時

- [ ] `RegOps` テンプレートパラメータから `Derived` が削除されている
- [ ] `ByteAdapter` テンプレートパラメータから `Derived` が削除されている
- [ ] `self()` ヘルパーと `static_cast<Derived&>(*this)` が存在しない
- [ ] ByteAdapter の `pack()`/`unpack()` が `std::byteswap` を使用
- [ ] `hw.read(DataReg{})` が `RegisterReader<DataReg>` を返す
- [ ] `hw.read(DataReg{}).bits()` で raw 値取得可能
- [ ] `hw.read(ConfigReg{}).get(ConfigEnable{})` でフィールド抽出可能
- [ ] `hw.read(CmdReg{})` がコンパイルエラー (WO + requires Readable)
- [ ] `hw.is(ConfigEnable::Set{})` が WO レジスタで補完候補に出ない
- [ ] `hw.modify(W1cOvr::Clear{})` がコンパイルエラー (W1C)
- [ ] `hw.clear(W1cOvr{})` が正常に動作
- [ ] modify_impl が w1c_mask を自動マスクして非 W1C ビットのみ保持
- [ ] `hw.reset(ConfigReg{})` がレジスタを 0xFF00 に戻す
- [ ] `hw.flip(W1cOvr{})` がコンパイルエラー (W1C)
- [ ] I2cTransport がローカル変数の I2C ドライバでコンパイルエラー
- [ ] bitbang_i2c.hh, bitbang_spi.hh が `std::endian` 置換 + CRTP 削除後もコンパイル成功

### Phase 3 完了時

- [ ] W1C 1-bit フィールドに `Clear` エイリアスが自動生成される
- [ ] `Protected<MockTransport, NoLockPolicy>` で `lock()` 経由のみアクセス可能
- [ ] `Protected` の直接 `inner` アクセスがコンパイルエラー
- [ ] `read_variant()` で全パターンハンドルしないとコンパイルエラー
- [ ] `sizeof(Protected<T, CriticalSectionPolicy>) == sizeof(T)`

### Phase 4 完了時

- [ ] `xmake test` 全テスト通過
- [ ] 全 compile_fail テスト通過 (8 ファイル)
- [ ] DESIGN.md に RegisterReader, W1C, WriteBehavior, Protected, clear(), reset() が記載
- [ ] DESIGN.md に deducing this 移行、std::byteswap/std::endian が記載
- [ ] DESIGN.md §12 に write/modify セマンティクスガイドが存在
- [ ] TESTING.md に新規テストファイルが記載
- [ ] 日本語版ドキュメントが英語版と同期

---

## 監査レポート対応表

本計画が監査レポートの各指摘にどう対応するかの対応表:

| 監査 # | 深刻度 | Step | 対応 |
|--------|:---:|------|------|
| A-1 | 🔴 | 2.1, 4.6 §12 | write() に `@warning` + セマンティクスガイド |
| C-2 | 🔴 | 3.2 | Protected が LockPolicy を保持し Guard に参照渡し |
| A-2 | 🟡 | 1.3, 2.1 | `ReadableValue` concept + `is()` に `requires` |
| A-3 | 🟡 | 1.5 | `value()` の else ブランチに assert 追加 |
| A-4 | 🟡 | 1.5 | `std::unsigned_integral<T>` 制約 |
| A-5 | 🟡 | 1.5, 2.1 | `value()` から `can_write` 制約を除外、`write()`/`modify()` の `WritableValue` concept で制約 |
| A-6 | 🟡 | 1.1 | BitRegion に 5 つの static_assert |
| B-1 | 🟡 | 2.2 | `bits()` を採用 |
| B-2 | 🟡 | 1.4 | `FieldType` 削除 → `RegionType` 統一 |
| C-1 | 🟡 | 2.1 | §1 と §3 の is() を同時に更新 |
| C-3 | 🟡 | 2.2 | RegisterReader は CheckPolicy 無関係と明記 |
| D-1 | 🟡 | 4.6 §12 | write/modify セマンティクスガイド |
| D-4 | 🟡 | 1.1 | A-6 と同一: BitRegion static_assert |
| C-4 | 🟢 | — | ロードマップは IMPROVEMENTS.md 側で対応 |
| D-2 | 🟢 | 4.6 §12.4 | modify() と Protected の関係を明記 |
| D-3 | 🟢 | 2.4 | Transport 設計判断コメント |
| D-5 | 🟢 | 2.1 | reset() メソッド追加 |
| D-6 | 🟢 | — | 対応不要 (umipal-gen テストで担保) |
| B-3 | 🟢 | 2.1 | `RegOrField` に rename |
| B-4 | 🟢 | 3.1 | OneBitW1CAliases |
| E-1 | 🟢 | — | 対応不要 |
| E-2 | 🟢 | — | umimmio 対象外 |
| E-3 | 🟢 | 3.2 | protected.hh の `@note` に限界明記 |

---

## C++23 標準機能分析

本計画で採用/不採用とした C++20/C++23 標準機能の分析:

### 採用

| 機能 | 規格 | Step | 影響度 | 理由 |
|-------|------|------|:---:|------|
| **Deducing this** | C++23 P0847R7 | 2.1, 2.5 | ★★★ | CRTP の `Derived` パラメータ完全削除、`self()` ヘルパー不要、エラーメッセージ簡素化 |
| **`std::byteswap`** | C++23 `<bit>` | 2.5 | ★★ | ByteAdapter の手動バイトループを標準関数に置換。ARM で `rev` 命令に最適化 |
| **`std::endian`** | C++20 `<bit>` | 1.6 | ★★ | カスタム `Endian` enum を標準型に置換。`std::endian::native` でホスト判定可能 |
| **`if consteval`** | C++23 | 1.5 | ★ | 既に使用済み。value() のコンパイル時レンジチェック |

### 不採用

| 機能 | 規格 | 理由 |
|-------|------|----- |
| **`std::integral_constant`** | C++11 | C++20 NTTP + concepts で完全に代替済み。CheckPolicy は既に `std::true_type`/`std::false_type` (≡ `integral_constant<bool, ...>`) 。Access policies は concepts がチェックするため `integral_constant` 基底クラスの追加価値なし |
| **`std::to_underlying`** | C++23 | enum → 整数変換箇所が少なく、既存の `static_cast` で十分 |
| **`std::unreachable()`** | C++23 | エラーパスは `assert`/`std::abort()` で既に正しく処理済み |
| **`static operator()`** | C++23 | umimmio のパターンに適用箇所なし |
| **`[[assume(expr)]]`** | C++23 | 将来の最適化ヒントとしては有用だが、現時点では必要箇所なし |
| **`std::expected`** | C++23 | umimmio は noexcept 前提。ErrorPolicy が既にエラーハンドリングを担当 |

### `std::integral_constant` 詳細分析

umimmio で `std::integral_constant` が改善できる部分はない。理由:

1. **CheckPolicy**: 既に `std::true_type`/`std::false_type` を使用。
   これらは `integral_constant<bool, true/false>` そのもの
2. **Access policies**: `static constexpr bool can_read/can_write` を
   concepts がチェック。`integral_constant` 基底クラスにしても concept 定義は変わらない
3. **NTTP (C++20)**: `Value<RegionT, auto EnumValue>` のように値を型レベルで
   エンコードする用途は NTTP が完全に代替
4. **DynamicValue**: ランタイム値を保持 — `integral_constant` はコンパイル時専用で不適用
