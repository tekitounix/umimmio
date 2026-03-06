#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file region.hh
/// @brief MMIO data model: Device, Register, Field, Value types with access concepts.
/// @author Shota Moriguchi @tekitounix

#include <cassert>
#include <concepts>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "policy.hh"

namespace umi::mmio {

// ===========================================================================
// Forward declarations
// ===========================================================================

template <class RegionT, typename T>
struct DynamicValue;
template <class FieldT, auto EnumValue>
struct Value;

namespace detail {

/// @brief Compile-time error trigger (non-constexpr function)
/// @note Called in `if consteval` block to trigger compile error
void mmio_compile_time_error_value_out_of_range();

/// @brief Range-checked DynamicValue construction (compile-time + runtime).
///
/// Consolidates the identical range-check + construct pattern used by
/// Register::value() and Field::value().
template <typename Region, std::unsigned_integral T>
constexpr auto make_checked_dynamic_value(T val) {
    if constexpr (Region::bit_width < sizeof(T) * bits8) {
        constexpr auto max_value = (1ULL << Region::bit_width) - 1;
        if consteval {
            if (static_cast<std::uint64_t>(val) > max_value) {
                mmio_compile_time_error_value_out_of_range();
            }
        } else {
            assert(static_cast<std::uint64_t>(val) <= max_value && "Value out of range for region");
        }
    }
    return DynamicValue<Region, T>{val};
}

/// @brief Helper type for transport concepts
struct TransportConceptReg {
    using RegValueType = std::uint32_t;
    static constexpr Addr address = 0;
};

} // namespace detail

// ===========================================================================
// Device, Block — peripheral and memory region descriptors
// ===========================================================================

/// @brief Device — top-level peripheral with transport constraints.
///
/// Provides shared access policy and transport type for all child registers.
/// MMIO peripherals override `base_address` in the derived struct.
/// I2C/SPI devices use the default `base_address = 0` (register offsets are
/// relative to the device, and bus address is handled at transport layer).
///
/// @tparam Access           Default access policy (RW, RO, WO, W1C).
/// @tparam AllowedTransports Transport tags permitted for this device.
///
/// @code
///   // MMIO: override base_address (Direct is the default transport)
///   struct SPI1 : Device<> {
///       static constexpr Addr base_address = 0x4001'3000;
///   };
///
///   // I2C: base_address = 0 is correct (register addrs are offsets)
///   struct CS43L22 : Device<RW, I2c> {
///       static constexpr std::uint8_t i2c_address = 0x4A;
///   };
/// @endcode
template <class Access = RW, typename... AllowedTransports>
struct Device {
    using AccessType = Access;
    using AllowedTransportsType = std::conditional_t<sizeof...(AllowedTransports) == 0,
                                                     std::tuple<Direct>,
                                                     std::tuple<AllowedTransports...>>;
    static constexpr Addr base_address = 0;
};

/// @brief Block - memory region within device
template <class Parent, Addr BaseAddr, class Access = Inherit>
struct Block {
    using AccessType = std::conditional_t<std::is_same_v<Access, Inherit>, typename Parent::AccessType, Access>;
    using AllowedTransportsType = typename Parent::AllowedTransportsType;
    static constexpr Addr base_address = Parent::base_address + BaseAddr;
};

// ===========================================================================
// BitRegion, Register, Field — bit-level descriptors
// ===========================================================================

/// @brief Bit region base - unified implementation for registers and fields
template <class Parent,
          Addr AddressOrOffset,
          std::size_t RegBitWidth,
          std::size_t BitOffset,
          std::size_t BitWidth,
          class Access,
          std::uint64_t ResetValue,
          bool IsRegister>
struct BitRegion {
    // --- Structural invariants (Step 1.1) ---
    static_assert(BitWidth > 0, "Field width must be > 0");
    static_assert(RegBitWidth <= 64, "Register width must be <= 64 bits");
    static_assert(BitOffset + BitWidth <= RegBitWidth, "Field bit range exceeds register width");
    static_assert(!IsRegister || BitOffset == 0, "Register must have BitOffset == 0");
    static_assert(!IsRegister || BitWidth == RegBitWidth, "Register BitWidth must equal RegBitWidth");

    using ParentAccessType = typename Parent::AccessType;
    using AccessType = std::conditional_t<std::is_same_v<Access, Inherit>, ParentAccessType, Access>;
    using AllowedTransportsType = typename Parent::AllowedTransportsType;

    // For fields, provide ParentRegType
    using ParentRegType = std::conditional_t<IsRegister, void, Parent>;

    static constexpr bool is_register = IsRegister;
    static constexpr std::size_t bit_width = BitWidth;
    static constexpr std::size_t shift = BitOffset;
    static constexpr std::size_t reg_bits = RegBitWidth;

    using RegValueType = UintFit<reg_bits>;
    using ValueType = UintFit<bit_width>;

    static constexpr Addr address = []() {
        if constexpr (IsRegister) {
            return Parent::base_address + AddressOrOffset;
        } else {
            return Parent::address;
        }
    }();

    static consteval RegValueType mask() {
        if constexpr (bit_width >= sizeof(RegValueType) * bits8) {
            return ~RegValueType{0};
        } else {
            return static_cast<RegValueType>((RegValueType{1} << bit_width) - 1) << shift;
        }
    }

    static consteval RegValueType reset_value() {
        if constexpr (IsRegister) {
            return static_cast<RegValueType>(ResetValue);
        } else {
            return Parent::reset_value();
        }
    }
};

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
template <class Parent,
          Addr Offset,
          std::size_t Bits,
          class Access = RW,
          std::uint64_t Reset = 0,
          std::uint64_t W1cMask = 0>
struct Register : BitRegion<Parent, Offset, Bits, 0, Bits, Access, Reset, true> {
    static_assert(Bits == 64 || W1cMask < (1ULL << Bits), "W1cMask has bits set beyond register width");

    /// @brief Bitmask of all W1C field positions in this register.
    /// Writing 0 to W1C bits is safe (no-op), writing 1 clears them.
    /// modify() masks these bits to 0 before write-back.
    static constexpr auto w1c_mask =
        static_cast<typename BitRegion<Parent, Offset, Bits, 0, Bits, Access, Reset, true>::RegValueType>(W1cMask);

    /// @brief Create a dynamic value for this register.
    template <std::unsigned_integral T>
    [[nodiscard("value() result must be used with write(), modify(), or is()")]]
    static constexpr auto value(T val) {
        return detail::make_checked_dynamic_value<Register>(val);
    }
};

/// @brief Numeric domain tag.
/// When specified as a trait of Field, raw value() is enabled.
/// Without this tag, Field only accepts named Value<> types (safe by default).
struct Numeric {};

// ---------------------------------------------------------------------------
// Trait extraction helpers (internal)
// ---------------------------------------------------------------------------
namespace detail {

/// @brief Check if a pack of types contains a specific type.
template <typename Target, typename... Ts>
constexpr bool contains_v = (std::is_same_v<Target, Ts> || ...);

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
    requires(!IsAccessPolicy<T>)
struct ExtractAccess<T, Rest...> {
    using type = typename ExtractAccess<Rest...>::type;
};

template <typename... Ts>
using ExtractAccess_t = typename ExtractAccess<Ts...>::type;

/// @brief Empty base — no 1-bit aliases.
struct NoOneBitAliases {};

/// @brief Set/Reset aliases for 1-bit RW fields.
template <class FieldT>
struct OneBitAliases {
    using Set = Value<FieldT, 1>;
    using Reset = Value<FieldT, 0>;
};

/// @brief Clear alias for 1-bit W1C fields.
template <class FieldT>
struct OneBitW1CAliases {
    using Clear = Value<FieldT, 1>;
};

/// @brief Check if an access policy is W1C.
template <class Access>
consteval bool is_w1c_access() {
    if constexpr (requires { Access::write_behavior; }) {
        return Access::write_behavior == WriteBehavior::ONE_TO_CLEAR;
    } else {
        return false;
    }
}

/// @brief Select the appropriate base class for 1-bit field aliases.
template <class FieldT, std::size_t BitWidth, class Access>
using OneBitBase =
    std::conditional_t<BitWidth != 1,
                       NoOneBitAliases,
                       std::conditional_t<is_w1c_access<Access>(), OneBitW1CAliases<FieldT>, OneBitAliases<FieldT>>>;

} // namespace detail

/// @brief Field — bit-range within a register.
///
/// By default, raw value() is **not** available (safe by default).
/// Add `mm::Numeric` to traits to enable raw value().
/// 1-bit fields automatically provide Set/Reset type aliases.
/// 1-bit W1C fields provide Clear alias instead.
///
/// @tparam Reg       Parent register type.
/// @tparam BitOffset Bit position within the register.
/// @tparam BitWidth  Width in bits.
/// @tparam Traits    Access policy (RW, RO, WO, W1C, Inherit) and/or Numeric, in any order.
///
/// Examples:
/// @code
///   struct EN   : mm::Field<CR, 0, 1> {};                      // 1-bit: Set/Reset auto
///   struct HPRE : mm::Field<CFGR, 4, 4> {};                   // safe (no raw value())
///   struct PLLN : mm::Field<PLLCFGR, 6, 9, mm::Numeric> {};   // raw value() enabled
///   struct DR   : mm::Field<ADC_DR, 0, 16, mm::RO, mm::Numeric> {};  // read-only + numeric
///   struct OVR  : mm::Field<SR, 0, 1, mm::W1C> {};            // W1C: Clear alias
/// @endcode
template <class Reg, std::size_t BitOffset, std::size_t BitWidth, class... Traits>
struct Field
    : BitRegion<Reg, 0, Reg::reg_bits, BitOffset, BitWidth, detail::ExtractAccess_t<Traits...>, 0, false>,
      detail::OneBitBase<Field<Reg, BitOffset, BitWidth, Traits...>, BitWidth, detail::ExtractAccess_t<Traits...>> {
  private:
    static constexpr bool is_numeric = detail::contains_v<Numeric, Traits...>;

  public:
    /// @brief Create a dynamic value. Only available for Numeric fields.
    template <std::unsigned_integral T>
        requires(is_numeric)
    [[nodiscard("value() result must be used with write(), modify(), or is()")]]
    static constexpr auto value(T val) {
        return detail::make_checked_dynamic_value<Field>(val);
    }
};

// ===========================================================================
// Value, DynamicValue — typed value carriers
// ===========================================================================

/// @brief Value representation for Fields and Registers
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
};

template <class RegionT, typename T>
struct [[nodiscard("DynamicValue must be used with write(), modify(), or is()")]] DynamicValue {
    using RegionType = RegionT;
    T assigned_value;
};

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
concept IsW1C =
    requires { T::AccessType::write_behavior; } && T::AccessType::write_behavior == WriteBehavior::ONE_TO_CLEAR;

/// @brief Type is not W1C (safe for modify() and flip()).
template <typename T>
concept NotW1C = !IsW1C<T>;

/// @brief Value/DynamicValue whose region is Readable.
template <typename V>
concept ReadableValue =
    requires { typename std::decay_t<V>::RegionType; } && Readable<typename std::decay_t<V>::RegionType>;

/// @brief Value/DynamicValue whose region is Writable.
template <typename V>
concept WritableValue =
    requires { typename std::decay_t<V>::RegionType; } && Writable<typename std::decay_t<V>::RegionType>;

/// @brief Value/DynamicValue whose region is not W1C.
template <typename V>
concept ModifiableValue =
    WritableValue<V> && Readable<typename std::decay_t<V>::RegionType> && NotW1C<typename std::decay_t<V>::RegionType>;

/// @name Transport concepts
/// @{

/// @note Transport layer design rule:
/// reg_read() / reg_write() do NOT check access policies.
/// Access control is enforced exclusively at the RegOps layer.
/// Rationale: modify() calls reg_read() on registers that may contain
/// read-only or write-only fields. Transport-level checks would
/// incorrectly reject these valid internal operations.

template <typename T>
concept RegTransportLike = requires(T& t) { typename T::TransportTag; } && requires(T& t, std::uint64_t val) {
    { t.template reg_read<detail::TransportConceptReg>(detail::TransportConceptReg{}) } -> std::convertible_to<std::uint64_t>;
    { t.template reg_write<detail::TransportConceptReg>(detail::TransportConceptReg{}, val) } -> std::same_as<void>;
};

template <typename T>
concept ByteTransportLike = requires(T& t) {
    typename T::TransportTag;
    typename T::AddressType;
    requires(std::same_as<typename T::TransportTag, I2c> ||
             std::same_as<typename T::TransportTag, Spi>);
    requires std::is_integral_v<typename T::AddressType>;
} && requires(T& t, typename T::AddressType addr, void* data, std::size_t size) {
    { t.raw_read(addr, data, size) } -> std::same_as<void>;
    { t.raw_write(addr, data, size) } -> std::same_as<void>;
};

template <typename T>
concept TransportLike = RegTransportLike<T> || ByteTransportLike<T>;
/// @}

// ===========================================================================
// UnknownValue, RegionValue — read result types
// ===========================================================================

/// @brief Sentinel type for unknown field values.
template <typename F>
struct UnknownValue {
    typename F::ValueType value;
};

/// @brief Immutable snapshot of a register or field value read from hardware.
///
/// Unified read-result type — replaces the former RegisterReader/FieldValue pair.
/// For registers: provides `bits()`, `get(Field)`, `is(Value/DynamicValue)`.
/// For fields: provides `bits()`, `operator==(Value/DynamicValue)`.
/// Raw integer comparison is always blocked — use `.bits()` for explicit escape.
///
/// @tparam R  The register or field type this value was read from.
template <typename R>
class RegionValue {
    using StoredType = std::conditional_t<R::is_register, typename R::RegValueType, typename R::ValueType>;
    StoredType val;

  public:
    using RegionType = R;

    explicit constexpr RegionValue(StoredType v) noexcept : val(v) {}

    /// @brief Get the raw bit value (escape hatch).
    /// Symmetric with write-side `value()` — explicit opt-in for raw access.
    [[nodiscard]] constexpr auto bits() const noexcept { return val; }

    /// @brief Compare two RegionValues of the same region.
    [[nodiscard]] friend constexpr bool operator==(RegionValue, RegionValue) noexcept = default;

    // --- Register-only: field extraction and value matching ---

    /// @brief Extract a field value from the cached register value.
    /// No bus access occurs — this is a pure bit-extraction operation.
    ///
    /// @tparam F Field type. Must belong to this register.
    /// @return RegionValue<F> wrapping the extracted value.
    template <typename F>
        requires(R::is_register) && IsField<F> && std::is_same_v<typename F::ParentRegType, R>
    [[nodiscard]] constexpr auto get(F /*field*/ = {}) const noexcept -> RegionValue<F> {
        return RegionValue<F>{static_cast<typename F::ValueType>((val & F::mask()) >> F::shift)};
    }

    /// @brief Compare the register/field value against an expected Value or DynamicValue.
    ///
    /// @tparam V Value or DynamicValue type.
    /// @return true if the value matches.
    template <typename V>
        requires(R::is_register)
    [[nodiscard]] constexpr bool is(V&& v) const noexcept {
        using VDecay = std::decay_t<V>;
        using RegionT = typename VDecay::RegionType;

        if constexpr (requires { VDecay::value; }) {
            if constexpr (RegionT::is_register) {
                static_assert(std::is_same_v<RegionT, R>, "Value register type must match reader register type");
                return val == static_cast<typename R::RegValueType>(VDecay::value);
            } else {
                static_assert(std::is_same_v<typename RegionT::ParentRegType, R>,
                              "Value field must belong to this register");
                return get(RegionT{}).bits() == static_cast<typename RegionT::ValueType>(VDecay::value);
            }
        } else {
            if constexpr (RegionT::is_register) {
                static_assert(std::is_same_v<RegionT, R>,
                              "DynamicValue register type must match reader register type");
                return val == static_cast<typename R::RegValueType>(v.assigned_value);
            } else {
                static_assert(std::is_same_v<typename RegionT::ParentRegType, R>,
                              "DynamicValue field must belong to this register");
                return get(RegionT{}).bits() == static_cast<typename RegionT::ValueType>(v.assigned_value);
            }
        }
    }

    // --- Field-only: typed comparison ---

    /// @brief Compare with a compile-time named Value.
    template <auto EnumValue>
        requires(!R::is_register)
    [[nodiscard]] constexpr bool operator==(Value<R, EnumValue> /*unused*/) const noexcept {
        return val == static_cast<typename R::ValueType>(EnumValue);
    }

    /// @brief Compare with a DynamicValue.
    template <typename T>
        requires(!R::is_register)
    [[nodiscard]] constexpr bool operator==(DynamicValue<R, T> const& dv) const noexcept {
        return val == static_cast<typename R::ValueType>(dv.assigned_value);
    }
};

} // namespace umi::mmio
