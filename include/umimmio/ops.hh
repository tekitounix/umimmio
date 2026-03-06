#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file ops.hh
/// @brief RegOps and ByteAdapter — type-safe register operation layer.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <tuple>
#include <utility>
#include <variant>

#include "region.hh"

namespace umi::mmio {

// ===========================================================================
// RegOps — CRTP-free base using deducing this (C++23)
// ===========================================================================

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

    /// @brief Read a register, returning a RegionValue for typed field extraction.
    /// @tparam Reg Register type.
    /// @return RegionValue wrapping the raw register value.
    template <typename Self, typename Reg>
        requires Readable<Reg> && IsRegister<Reg>
    [[nodiscard]] auto read(this const Self& self, Reg /*reg*/) noexcept -> RegionValue<Reg> {
        check_transport_allowed<Self, Reg>();
        return RegionValue<Reg>{self.reg_read(Reg{})};
    }

    /// @brief Read a single field value (shortcut for read(Reg).get(Field)).
    /// @tparam F Field type.
    /// @return RegionValue<F> wrapping the extracted value.
    template <typename Self, typename F>
        requires Readable<F> && IsField<F>
    [[nodiscard]] auto read(this const Self& self, F /*field*/) noexcept -> RegionValue<F> {
        check_transport_allowed<Self, F>();
        return self.read(typename F::ParentRegType{}).get(F{});
    }

    /// @brief Compare a register/field against an expected value.
    /// @tparam V Value or DynamicValue type.
    /// @return true if the current hardware value matches.
    ///
    /// DynamicValue range check is performed here (RegOps layer) since
    /// RegionValue intentionally omits it — it holds no ErrorPolicy.
    template <typename Self, ReadableValue V>
    [[nodiscard]] bool is(this const Self& self, V&& value) noexcept {
        using VDecay = std::decay_t<V>;
        using RegionT = typename VDecay::RegionType;
        check_transport_allowed<Self, RegionT>();

        // DynamicValue range check (Value has compile-time guarantee, skip)
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

        // Delegate to RegionValue::is() which handles both Value/DynamicValue
        if constexpr (RegionT::is_register) {
            return self.read(RegionT{}).is(std::forward<V>(value));
        } else {
            return self.read(typename RegionT::ParentRegType{}).is(std::forward<V>(value));
        }
    }

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

    /// @brief Toggle a 1-bit field via read-modify-write.
    /// @tparam F A 1-bit, read-write Field type.
    template <typename Self, typename F>
        requires ReadWritable<F> && IsField<F> && (F::bit_width == 1) && NotW1C<F>
    void flip(this const Self& self, F /*field*/) noexcept {
        check_transport_allowed<Self, F>();
        using ParentRegType = typename F::ParentRegType;
        auto current = self.reg_read(ParentRegType{});
        current ^= F::mask();
        self.reg_write(ParentRegType{}, current);
    }

    /// @brief Clear a W1C (Write-1-to-Clear) field by writing 1 to the target bit(s).
    /// Other bits are written as 0 to prevent accidental clearing.
    /// @tparam F A W1C field type.
    template <typename Self, typename F>
        requires IsW1C<F> && IsField<F>
    void clear(this const Self& self, F /*field*/) noexcept {
        check_transport_allowed<Self, F>();
        using ParentRegType = typename F::ParentRegType;
        self.reg_write(ParentRegType{}, F::mask());
    }

    /// @brief Reset a register to its compile-time reset value.
    /// @tparam Reg A writable register type.
    template <typename Self, typename Reg>
        requires Writable<Reg> && IsRegister<Reg>
    void reset(this const Self& self, Reg /*reg*/) noexcept {
        check_transport_allowed<Self, Reg>();
        self.reg_write(Reg{}, Reg::reset_value());
    }

    /// @brief Read a field and return a variant of possible named values.
    ///
    /// Uses `||` fold expression for first-match-wins semantics:
    /// stops evaluation as soon as a match is found.
    ///
    /// @tparam F        Field type to read.
    /// @tparam Variants Named Value types to match against.
    /// @return std::variant of the matched Value type or UnknownValue.
    template <typename F, typename... Variants, typename Self>
        requires Readable<F> && IsField<F>
    [[nodiscard]] auto read_variant(this const Self& self, F field = {}) -> std::variant<Variants..., UnknownValue<F>> {
        auto val = self.read(field);
        std::variant<Variants..., UnknownValue<F>> result = UnknownValue<F>{val.bits()};

        auto try_match = [&]<typename V>() -> bool {
            if (val.bits() == static_cast<typename F::ValueType>(V::value)) {
                result = V{};
                return true;
            }
            return false;
        };
        (try_match.template operator()<Variants>() || ...);

        return result;
    }

  private:
    /// @brief Transport constraint check.
    /// @note Self is deduced via deducing this, not a class template parameter.
    template <typename Self, typename RegOrField>
    static constexpr void check_transport_allowed() {
        static_assert(TransportLike<Self>, "Self must satisfy TransportLike");
        using AllowedType = typename RegOrField::AllowedTransportsType;
        using TransportTagType = typename Self::TransportTag;

        constexpr bool is_allowed = []<typename... Ts>(std::tuple<Ts...>) {
            return (std::is_same_v<TransportTagType, Ts> || ...);
        }(AllowedType{});

        static_assert(is_allowed, "This transport is not allowed for this device");
    }

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
            self.reg_write(RegionType{}, static_cast<typename RegionType::RegValueType>(V::value));
        } else {
            self.reg_write(RegionType{}, static_cast<typename RegionType::RegValueType>(value.assigned_value));
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
        using ParentRegType =
            std::conditional_t<FirstRegionType::is_register, FirstRegionType, typename FirstRegionType::ParentRegType>;

        static_assert((std::is_same_v<std::conditional_t<std::decay_t<Values>::RegionType::is_register,
                                                         typename std::decay_t<Values>::RegionType,
                                                         typename std::decay_t<Values>::RegionType::ParentRegType>,
                                      ParentRegType> &&
                       ...),
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
        using ParentRegType =
            std::conditional_t<FirstRegionType::is_register, FirstRegionType, typename FirstRegionType::ParentRegType>;

        static_assert((std::is_same_v<std::conditional_t<std::decay_t<Values>::RegionType::is_register,
                                                         typename std::decay_t<Values>::RegionType,
                                                         typename std::decay_t<Values>::RegionType::ParentRegType>,
                                      ParentRegType> &&
                       ...),
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
        static_assert(RegionType::AccessType::can_write, "Cannot write to read-only register");

        if constexpr (requires { V::value; }) {
            return (reg_val & ~RegionType::mask()) |
                   ((static_cast<T>(V::value) << RegionType::shift) & RegionType::mask());
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
                   ((static_cast<T>(v.assigned_value) << RegionType::shift) & RegionType::mask());
        }
    }
};

// ===========================================================================
// ByteAdapter — CRTP-free adapter using deducing this + std::byteswap
// ===========================================================================

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
        if constexpr (sizeof(T) == 1) {
            buffer[0] = static_cast<std::uint8_t>(value);
        } else {
            if constexpr (DataEndian != std::endian::native) {
                value = std::byteswap(value);
            }
            std::memcpy(buffer, &value, sizeof(T));
        }
    }

    /// @brief Unpack a byte buffer into a register value.
    template <typename T>
    static T unpack(const std::uint8_t* buffer) noexcept {
        if constexpr (sizeof(T) == 1) {
            return static_cast<T>(buffer[0]);
        } else {
            T value;
            std::memcpy(&value, buffer, sizeof(T));
            if constexpr (DataEndian != std::endian::native) {
                value = std::byteswap(value);
            }
            return value;
        }
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

    /// @brief Read register via deducing this -> self.raw_read().
    template <typename Self, typename Reg>
    [[nodiscard]] auto reg_read(this const Self& self, Reg /*reg*/) noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        static_assert(sizeof(T) <= max_reg_bytes, "Register size must be <= 64 bits");
        static_assert(Reg::address <= static_cast<Addr>(std::numeric_limits<AddressTypeT>::max()),
                      "Register address exceeds address width for byte transport");

        std::array<std::uint8_t, sizeof(T)> buffer;
        self.raw_read(static_cast<AddressTypeT>(Reg::address), buffer.data(), sizeof(T));
        return unpack<T>(buffer.data());
    }

    /// @brief Write register via deducing this -> self.raw_write().
    template <typename Self, typename Reg>
    void reg_write(this const Self& self, Reg /*reg*/, typename Reg::RegValueType value) noexcept {
        using T = typename Reg::RegValueType;
        static_assert(sizeof(T) <= max_reg_bytes, "Register size must be <= 64 bits");
        static_assert(Reg::address <= static_cast<Addr>(std::numeric_limits<AddressTypeT>::max()),
                      "Register address exceeds address width for byte transport");

        std::array<std::uint8_t, sizeof(T)> buffer;
        pack<T>(value, buffer.data());
        self.raw_write(static_cast<AddressTypeT>(Reg::address), buffer.data(), sizeof(T));
    }
};

} // namespace umi::mmio
