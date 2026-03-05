#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file register.hh
/// @brief UMI Memory-mapped I/O register abstractions.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

/// @namespace umi::mmio
/// @brief UMI Memory-mapped I/O abstractions
namespace umi::mmio {

/// @typedef Addr
/// @brief Memory address type
using Addr = std::uintptr_t;

// Forward declarations
template <class RegionT, typename T>
struct DynamicValue;
template <class FieldT, auto EnumValue>
struct Value;

/// @name Bit width constants
/// @{
constexpr std::size_t bits8 = 8U;   ///< 8-bit width
constexpr std::size_t bits16 = 16U; ///< 16-bit width
constexpr std::size_t bits32 = 32U; ///< 32-bit width
constexpr std::size_t bits64 = 64U; ///< 64-bit width
/// @}

/// @brief Select the smallest unsigned integer type that can hold Bits
template <std::size_t Bits>
using UintFit =
    std::conditional_t<(Bits <= bits8),
                       std::uint8_t,
                       std::conditional_t<(Bits <= bits16),
                                          std::uint16_t,
                                          std::conditional_t<(Bits <= bits32), std::uint32_t, std::uint64_t>>>;

// ===========================================================================
// Access policies
// ===========================================================================

/// @brief Describes how a write operation affects the register/field.
enum class WriteBehavior : std::uint8_t {
    NORMAL,       ///< Standard write: written value replaces current value.
    ONE_TO_CLEAR, ///< Write-1-to-clear: writing 1 clears the bit, writing 0 has no effect.
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

using RW = AccessPolicy<true, true>;
using RO = AccessPolicy<true, false>;
using WO = AccessPolicy<false, true>;
using W1C = AccessPolicy<true, true, WriteBehavior::ONE_TO_CLEAR>;

struct Inherit {};

/// @name Transport tags
/// @{
struct DirectTransportTag {};
struct I2CTransportTag {};
struct SPITransportTag {};
/// @}

/// @name Error policies
/// @{
struct AssertOnError {
    static void on_range_error([[maybe_unused]] const char* msg) noexcept { assert(false && msg); }
};

struct TrapOnError {
    [[noreturn]] static void on_range_error([[maybe_unused]] const char* msg) noexcept { std::abort(); }
};

struct IgnoreError {
    static void on_range_error([[maybe_unused]] const char* msg) noexcept {}
};

template <void (*Handler)(const char*)>
struct CustomErrorHandler {
    static void on_range_error(const char* msg) noexcept { Handler(msg); }
};
/// @}

/// @brief Compile-time error trigger (non-constexpr function)
/// @note Called in `if consteval` block to trigger compile error
void mmio_compile_time_error_value_out_of_range();

/// @brief Helper type for transport concepts
struct TransportConceptReg {
    using RegValueType = std::uint32_t;
    static constexpr Addr address = 0;
};

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
///   // MMIO: override base_address
///   struct SPI1 : Device<RW, DirectTransportTag> {
///       static constexpr Addr base_address = 0x4001'3000;
///   };
///
///   // I2C: base_address = 0 is correct (register addrs are offsets)
///   struct CS43L22 : Device<RW, I2CTransportTag> {
///       static constexpr std::uint8_t i2c_address = 0x4A;
///   };
/// @endcode
template <class Access = RW, typename... AllowedTransports>
struct Device {
    using AccessType = Access;
    using AllowedTransportsType = std::conditional_t<sizeof...(AllowedTransports) == 0,
                                                     std::tuple<DirectTransportTag>,
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
        if constexpr (Bits < sizeof(T) * bits8) {
            constexpr auto max_value = (1ULL << Bits) - 1;
            if consteval {
                if (static_cast<std::uint64_t>(val) > max_value) {
                    mmio_compile_time_error_value_out_of_range();
                }
            } else {
                assert(static_cast<std::uint64_t>(val) <= max_value && "Register value out of range");
            }
        }
        return DynamicValue<Register, T>{val};
    }
};

/// @brief Forward declaration for Value
template <class FieldT, auto EnumValue>
struct Value;

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
        if constexpr (BitWidth < sizeof(T) * bits8) {
            constexpr auto max_value = (1ULL << BitWidth) - 1;
            if consteval {
                if (static_cast<std::uint64_t>(val) > max_value) {
                    mmio_compile_time_error_value_out_of_range();
                }
            } else {
                assert(static_cast<std::uint64_t>(val) <= max_value && "Field value out of range");
            }
        }
        return DynamicValue<Field, T>{val};
    }
};

/// @brief Explicit raw value for any field — escape hatch.
///
/// Use when you intentionally need to write a numeric value to a field
/// that does not have Numeric trait. The name `raw` signals deliberate
/// bypassing of type safety, similar to const_cast.
///
/// @tparam FieldT  The Field type to target.
/// @param  val     The raw numeric value.
/// @return DynamicValue usable with write(), modify(), is().
///
/// @code
///   io.write(mm::raw<HPRE>(5u));    // deliberate escape
///   io.write(PLLN::value(336u));    // normal for Numeric fields
/// @endcode
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
            assert(static_cast<std::uint64_t>(val) <= max_value && "raw() value out of range");
        }
    }
    return DynamicValue<FieldT, T>{val};
}

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

    static constexpr auto get() noexcept { return value; }
    using ValueType = decltype(value);
};

template <class RegionT, typename T>
struct [[nodiscard("DynamicValue must be used with write(), modify(), or is()")]] DynamicValue {
    using RegionType = RegionT;
    using RegionValueType = typename RegionT::ValueType;
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
concept DirectTransportLike = requires(T& t) {
    typename T::TransportTag;
    requires std::same_as<typename T::TransportTag, DirectTransportTag>;
} && requires(T& t, std::uint64_t val) {
    { t.template reg_read<TransportConceptReg>(TransportConceptReg{}) } -> std::convertible_to<std::uint64_t>;
    { t.template reg_write<TransportConceptReg>(TransportConceptReg{}, val) } -> std::same_as<void>;
};

template <typename T>
concept RegTransportLike = requires(T& t) { typename T::TransportTag; } && requires(T& t, std::uint64_t val) {
    { t.template reg_read<TransportConceptReg>(TransportConceptReg{}) } -> std::convertible_to<std::uint64_t>;
    { t.template reg_write<TransportConceptReg>(TransportConceptReg{}, val) } -> std::same_as<void>;
};

template <typename T>
concept ByteTransportLike = requires(T& t) {
    typename T::TransportTag;
    typename T::AddressType;
    requires(std::same_as<typename T::TransportTag, I2CTransportTag> ||
             std::same_as<typename T::TransportTag, SPITransportTag>);
    requires std::is_integral_v<typename T::AddressType>;
} && requires(T& t, typename T::AddressType addr, void* data, std::size_t size) {
    { t.raw_read(addr, data, size) } -> std::same_as<void>;
    { t.raw_write(addr, data, size) } -> std::same_as<void>;
};

template <typename T>
concept TransportLike = DirectTransportLike<T> || ByteTransportLike<T> || RegTransportLike<T>;
/// @}

/// @brief Sentinel type for unknown field values.
template <typename F>
struct UnknownValue {
    typename F::ValueType value;
};

// ===========================================================================
// RegisterReader — immutable view of a register value
// ===========================================================================

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
    [[nodiscard]] constexpr auto get(F /*field*/ = {}) const noexcept -> typename F::ValueType {
        return static_cast<typename F::ValueType>((val & F::mask()) >> F::shift);
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
            if constexpr (RegionT::is_register) {
                static_assert(std::is_same_v<RegionT, Reg>, "Value register type must match reader register type");
                return val == static_cast<typename Reg::RegValueType>(VDecay::value);
            } else {
                static_assert(std::is_same_v<typename RegionT::ParentRegType, Reg>,
                              "Value field must belong to this register");
                return get(RegionT{}) == static_cast<typename RegionT::ValueType>(VDecay::value);
            }
        } else {
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

    /// @brief Read a register, returning a RegisterReader for typed field extraction.
    /// @tparam Reg Register type.
    /// @return RegisterReader wrapping the raw register value.
    template <typename Self, typename Reg>
        requires Readable<Reg> && IsRegister<Reg>
    [[nodiscard]] auto read(this const Self& self, Reg /*reg*/) noexcept -> RegisterReader<Reg> {
        check_transport_allowed<Self, Reg>();
        return RegisterReader<Reg>{self.reg_read(Reg{})};
    }

    /// @brief Read a single field value (shortcut for read(Reg).get(Field)).
    /// @tparam F Field type.
    /// @return Extracted field value.
    template <typename Self, typename F>
        requires Readable<F> && IsField<F>
    [[nodiscard]] auto read(this const Self& self, F /*field*/) noexcept -> typename F::ValueType {
        check_transport_allowed<Self, F>();
        return self.read(typename F::ParentRegType{}).get(F{});
    }

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
                return self.read(RegionT{}).bits() == static_cast<typename RegionT::RegValueType>(VDecay::value);
            } else {
                return self.read(RegionT{}) == static_cast<typename RegionT::ValueType>(VDecay::value);
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
                return self.read(RegionT{}).bits() == static_cast<typename RegionT::RegValueType>(value.assigned_value);
            } else {
                return self.read(RegionT{}) == value.assigned_value;
            }
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
    /// All Value types must be handled, or compile error from std::visit.
    /// If the field value matches none of the named values, UnknownValue<F> is returned.
    ///
    /// @tparam F        Field type to read.
    /// @tparam Variants Named Value types to match against.
    /// @return std::variant of the matched Value type or UnknownValue.
    template <typename F, typename... Variants, typename Self>
        requires Readable<F> && IsField<F>
    [[nodiscard]] auto read_variant(this const Self& self, F field = {}) -> std::variant<Variants..., UnknownValue<F>> {
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
        static_assert(sizeof(T) <= 8, "Register size must be <= 64 bits");
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
        static_assert(sizeof(T) <= 8, "Register size must be <= 64 bits");
        static_assert(Reg::address <= static_cast<Addr>(std::numeric_limits<AddressTypeT>::max()),
                      "Register address exceeds address width for byte transport");

        std::array<std::uint8_t, sizeof(T)> buffer;
        pack<T>(value, buffer.data());
        self.raw_write(static_cast<AddressTypeT>(Reg::address), buffer.data(), sizeof(T));
    }
};

} // namespace umi::mmio
