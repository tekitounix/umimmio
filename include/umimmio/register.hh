#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file register.hh
/// @brief UMI Memory-mapped I/O register abstractions.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>

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

/// @name Access policies
/// @{
struct RW {
    static constexpr bool can_read = true, can_write = true;
};
struct RO {
    static constexpr bool can_read = true, can_write = false;
};
struct WO {
    static constexpr bool can_read = false, can_write = true;
};
struct Inherit {};
/// @}

/// @name Transport tags
/// @{
struct DirectTransportTag {};
struct I2CTransportTag {};
struct SPITransportTag {};
/// @}

/// @brief Byte order for byte-oriented transports
enum class Endian : std::uint8_t { LITTLE, BIG };

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

/// @brief Device - top level with transport constraints
template <class Access = RW, typename... AllowedTransports>
struct Device {
    using AccessType = Access;
    using AllowedTransportsType = std::conditional_t<sizeof...(AllowedTransports) == 0,
                                                     std::tuple<DirectTransportTag>, // Default to direct transport only
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

    // Dynamic value creation
    template <std::integral T>
    [[nodiscard("value() result must be used with write(), modify(), or is()")]]
    static constexpr auto value(T val) {
        if constexpr (bit_width < sizeof(T) * bits8) {
            constexpr auto max_value = (1ULL << bit_width) - 1;
            if consteval {
                if (static_cast<std::uint64_t>(val) > max_value) {
                    mmio_compile_time_error_value_out_of_range();
                }
            }
        }
        // Runtime check deferred to write()/modify() via CheckPolicy + ErrorPolicy
        return DynamicValue<BitRegion, T>{val};
    }
};

/// @brief Register = BitRegion with IsRegister=true
template <class Parent, Addr Offset, std::size_t Bits, class Access = RW, std::uint64_t Reset = 0>
using Register = BitRegion<Parent, Offset, Bits, 0, Bits, Access, Reset, true>;

/// @brief Forward declaration for Value
template <class FieldT, auto EnumValue>
struct Value;

/// @brief Field = BitRegion with IsRegister=false
template <class Reg, std::size_t BitOffset, std::size_t BitWidth, class Access = Inherit>
struct Field : BitRegion<Reg, 0, Reg::reg_bits, BitOffset, BitWidth, Access, 0, false> {};

/// @brief 1-bit field specialization (auto Set/Reset)
template <class Reg, std::size_t BitOffset, class Access>
struct Field<Reg, BitOffset, 1, Access> : BitRegion<Reg, 0, Reg::reg_bits, BitOffset, 1, Access, 0, false> {
    using Set = Value<Field, 1>;
    using Reset = Value<Field, 0>;
};

/// @brief Value representation for Fields and Registers
template <class RegionT, auto EnumValue>
struct Value {
    using RegionType = RegionT;
    static constexpr auto value = EnumValue;

    // For Fields, provide shifted value
    static constexpr auto shifted_value = []() {
        if constexpr (requires { RegionT::shift; }) {
            return static_cast<typename RegionT::RegValueType>(value) << RegionT::shift;
        } else {
            return static_cast<typename RegionT::RegValueType>(value);
        }
    }();

    // Get the raw value (unshifted)
    static constexpr auto get() noexcept { return value; }

    // Get the value type
    using ValueType = decltype(value);

    // For backward compatibility
    using FieldType = RegionT;
};

template <class RegionT, typename T>
struct [[nodiscard("DynamicValue must be used with write(), modify(), or is()")]] DynamicValue {
    using RegionType = RegionT;
    using RegionValueType = typename RegionT::ValueType;
    T assigned_value;
};

/// @name Transport concepts
/// @{
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

// Forward declaration for friend
template <class Derived, typename CheckPolicy, typename ErrorPolicy, typename AddressType, Endian DataEndian>
class ByteAdapter;

/// @brief CRTP base providing type-safe register read/write/modify operations.
/// @tparam Derived     Concrete transport (DirectTransport, I2cTransport, etc.).
/// @tparam CheckPolicy Enable runtime range checks (std::true_type or std::false_type).
/// @tparam ErrorPolicy Error handler invoked on range violations.
template <class Derived, typename CheckPolicy = std::true_type, typename ErrorPolicy = AssertOnError>
class RegOps {
  private:
    friend Derived;
    template <class D, typename C, typename E, typename A, Endian DE>
    friend class ByteAdapter;
    RegOps() = default;
    RegOps(const RegOps&) = default;
    RegOps& operator=(const RegOps&) = default;

  public:
    RegOps(RegOps&&) = delete;
    RegOps& operator=(RegOps&&) = delete;

  protected:
    ~RegOps() = default;
    auto& self() { return static_cast<Derived&>(*this); }
    [[nodiscard]] const auto& self() const { return static_cast<const Derived&>(*this); }

    // Transport constraint check (at device level)
    template <typename Reg>
    static constexpr void check_transport_allowed() {
        static_assert(TransportLike<Derived>, "Derived must satisfy TransportLike");
        using AllowedType = typename Reg::AllowedTransportsType;
        using TransportTagType = typename Derived::TransportTag;

        constexpr bool is_allowed = []<typename... Ts>(std::tuple<Ts...>) {
            return (std::is_same_v<TransportTagType, Ts> || ...);
        }(AllowedType{});

        static_assert(is_allowed, "This transport is not allowed for this device");
    }

  public:
    // Derived classes must implement:
    // template <typename Reg>
    // auto reg_read(Reg reg) const noexcept -> typename Reg::RegValueType;
    //
    // template <typename Reg>
    // void reg_write(Reg reg, typename Reg::RegValueType value) const noexcept;

    /// @brief Write one or more field/register values in a single bus transaction.
    /// @tparam Values Value or DynamicValue types carrying the data.
    template <typename... Values>
    void write(Values&&... values) const noexcept {
        if constexpr (sizeof...(Values) == 1) {
            write_single(std::forward<Values>(values)...);
        } else {
            write_multiple(std::forward<Values>(values)...);
        }
    }

    /// @brief Read-modify-write: read current value, apply field changes, write back.
    /// @tparam Values Value or DynamicValue types carrying the data.
    /// @warning This operation is NOT atomic. Protect with a critical section if needed.
    template <typename... Values>
    void modify(Values&&... values) const noexcept {
        static_assert(sizeof...(Values) > 0);
        modify_impl(std::forward<Values>(values)...);
    }

    /// @brief Read a register or field value.
    /// @tparam RegOrField Register or Field type to read.
    /// @return For registers: the full register value. For fields: the extracted field value.
    template <typename RegOrField>
    [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] auto
    read(RegOrField /*reg_or_field*/) const noexcept {
        check_transport_allowed<RegOrField>();
        static_assert(RegOrField::AccessType::can_read, "Cannot read from write-only register");

        if constexpr (RegOrField::is_register) {
            return self().reg_read(RegOrField{});
        } else {
            auto const reg_val = self().reg_read(typename RegOrField::ParentRegType{});
            return static_cast<typename RegOrField::ValueType>((reg_val & RegOrField::mask()) >> RegOrField::shift);
        }
    }

    /// @brief Compare a register/field against an expected value.
    /// @tparam Value Enumerated Value or DynamicValue to compare against.
    /// @return true if the current hardware value matches.
    template <typename Value>
    bool is(Value&& value) const noexcept {
        using V = std::decay_t<Value>;

        if constexpr (requires { V::value; }) {
            // Enumerated value
            return read(typename V::FieldType{}) == V::value;
        } else {
            // Dynamic value with runtime check
            using RegionType = typename V::RegionType;
            if constexpr (CheckPolicy::value) {
                if constexpr (RegionType::bit_width < sizeof(decltype(value.assigned_value)) * bits8) {
                    auto const max_value = (1ULL << RegionType::bit_width) - 1;
                    if (static_cast<std::uint64_t>(value.assigned_value) > max_value) {
                        ErrorPolicy::on_range_error("Comparison value out of range");
                    }
                }
            }
            return read(RegionType{}) == value.assigned_value;
        }
    }

    /// @brief Toggle a 1-bit field via read-modify-write.
    /// @tparam Field A 1-bit Field type (static_assert enforced).
    /// @pre Field must have both read and write access.
    template <typename Field>
    void flip(Field /*field*/) const noexcept {
        check_transport_allowed<Field>();
        static_assert(Field::bit_width == 1, "flip() only for 1-bit fields");
        static_assert(Field::AccessType::can_read && Field::AccessType::can_write, "flip() requires read-write access");

        using ParentRegType = typename Field::ParentRegType;
        auto current = self().reg_read(ParentRegType{});
        current ^= Field::mask();
        self().reg_write(ParentRegType{}, current);
    }

  private:
    // Helper functions
    template <typename T, typename Value>
    static auto apply_field_value(T reg_val, const Value& v) {
        using V = std::decay_t<Value>;
        using RegionType = typename V::RegionType;
        static_assert(RegionType::AccessType::can_write, "Cannot write to read-only register");

        if constexpr (requires { V::value; }) {
            // Enumerated value (has static 'value' member)
            return (reg_val & ~RegionType::mask()) |
                   ((static_cast<T>(V::value) << RegionType::shift) & RegionType::mask());
        } else {
            // Dynamic value
            // Runtime range check if policy is enabled
            if constexpr (CheckPolicy::value) {
                if constexpr (RegionType::bit_width < sizeof(T) * bits8) {
                    [[maybe_unused]] auto const max_value = (1ULL << RegionType::bit_width) - 1;
                    if (static_cast<std::uint64_t>(v.assigned_value) > max_value) {
                        ErrorPolicy::on_range_error("Value out of range");
                    }
                }
            }
            return (reg_val & ~RegionType::mask()) |
                   ((static_cast<T>(v.assigned_value) << RegionType::shift) & RegionType::mask());
        }
    }

    template <typename T, typename Value>
    static auto apply_field_modify(T current, const Value& v) {
        return apply_field_value(current, v);
    }

    // Implementation details
    template <typename Value>
    void write_single(Value&& value) const noexcept {
        using V = std::decay_t<Value>;
        using RegionType = typename V::RegionType;
        check_transport_allowed<RegionType>();
        static_assert(RegionType::AccessType::can_write, "Cannot write to read-only register");

        if constexpr (RegionType::is_register) {
            write_register_value(std::forward<Value>(value));
        } else {
            write_field_value(std::forward<Value>(value));
        }
    }

    /// @brief Write a Value to a register-type region.
    template <typename Value>
    void write_register_value(Value&& value) const noexcept {
        using V = std::decay_t<Value>;
        using RegionType = typename V::RegionType;

        if constexpr (requires { V::value; }) {
            // Enumerated value
            check_range_if_enabled<RegionType, std::uint64_t(V::value)>("Register value out of range");
            self().reg_write(RegionType{}, static_cast<typename RegionType::RegValueType>(V::value));
        } else {
            // Dynamic value
            check_dynamic_range_if_enabled<RegionType>(value.assigned_value, "Register value out of range");
            self().reg_write(RegionType{}, value.assigned_value);
        }
    }

    /// @brief Write a Value to a field-type region (masks into parent register).
    template <typename Value>
    void write_field_value(Value&& value) const noexcept {
        using V = std::decay_t<Value>;
        using RegionType = typename V::RegionType;
        using ParentRegType = typename RegionType::ParentRegType;

        auto reg_val = ParentRegType::reset_value();
        if constexpr (requires { V::value; }) {
            reg_val = (reg_val & ~RegionType::mask()) | ((V::value << RegionType::shift) & RegionType::mask());
        } else {
            check_dynamic_range_if_enabled<RegionType>(value.assigned_value, "Field value out of range");
            reg_val =
                (reg_val & ~RegionType::mask()) | ((value.assigned_value << RegionType::shift) & RegionType::mask());
        }
        self().reg_write(ParentRegType{}, reg_val);
    }

    /// @brief Check if a compile-time value exceeds the region's bit width (if policy enabled).
    template <typename RegionType, std::uint64_t Val>
    void check_range_if_enabled(const char* msg) const noexcept {
        if constexpr (CheckPolicy::value) {
            if constexpr (RegionType::bit_width < sizeof(std::uint64_t) * bits8) {
                constexpr auto max_value = (1ULL << RegionType::bit_width) - 1;
                if (Val > max_value) {
                    ErrorPolicy::on_range_error(msg);
                }
            }
        }
    }

    /// @brief Check if a dynamic value exceeds the region's bit width (if policy enabled).
    template <typename RegionType, typename T>
    void check_dynamic_range_if_enabled(T assigned_value, const char* msg) const noexcept {
        if constexpr (CheckPolicy::value) {
            if constexpr (RegionType::bit_width < sizeof(assigned_value) * bits8) {
                auto const max_value = (1ULL << RegionType::bit_width) - 1;
                if (static_cast<std::uint64_t>(assigned_value) > max_value) {
                    ErrorPolicy::on_range_error(msg);
                }
            }
        }
    }

    // Multiple field write (optimized)
    template <typename... Values>
    void write_multiple(Values&&... values) const noexcept {
        // Get the parent register from the first value's region
        using FirstValueType = std::decay_t<std::tuple_element_t<0, std::tuple<Values...>>>;
        using FirstRegionType = typename FirstValueType::RegionType;
        using FirstParentRegType =
            std::conditional_t<FirstRegionType::is_register, FirstRegionType, typename FirstRegionType::ParentRegType>;

        // For fields, we need parent_reg_t
        using FirstRegType =
            std::conditional_t<FirstRegionType::is_register, FirstRegionType, typename FirstRegionType::ParentRegType>;

        static_assert((std::is_same_v<std::conditional_t<std::decay_t<Values>::RegionType::is_register,
                                                         typename std::decay_t<Values>::RegionType,
                                                         typename std::decay_t<Values>::RegionType::ParentRegType>,
                                      FirstParentRegType> &&
                       ...),
                      "All values must target the same register");

        check_transport_allowed<FirstRegType>();

        auto reg_val = FirstRegType::reset_value();
        ((reg_val = apply_field_value(reg_val, values)), ...);
        self().reg_write(FirstRegType{}, reg_val);
    }

    // modify implementation (single RMW)
    template <typename... Values>
    void modify_impl(Values&&... values) const noexcept {
        // Get the parent register from the first value
        using FirstValueType = std::decay_t<std::tuple_element_t<0, std::tuple<Values...>>>;
        using FirstRegionType = typename FirstValueType::RegionType;
        using FirstParentRegType =
            std::conditional_t<FirstRegionType::is_register, FirstRegionType, typename FirstRegionType::ParentRegType>;

        // Get parent register type
        using ParentRegType =
            std::conditional_t<FirstRegionType::is_register, FirstRegionType, typename FirstRegionType::ParentRegType>;

        static_assert((std::is_same_v<std::conditional_t<std::decay_t<Values>::RegionType::is_register,
                                                         typename std::decay_t<Values>::RegionType,
                                                         typename std::decay_t<Values>::RegionType::ParentRegType>,
                                      FirstParentRegType> &&
                       ...),
                      "All values must target the same register");

        check_transport_allowed<ParentRegType>();

        auto current = self().reg_read(ParentRegType{});
        ((current = apply_field_modify(current, values)), ...);
        self().reg_write(ParentRegType{}, current);
    }
};

// DirectTransport has been moved to transport/direct.hh

/// @brief CRTP adapter bridging byte-oriented transports (I2C, SPI) to RegOps.
///
/// Converts RegOps' typed register read/write into raw_read/raw_write byte
/// operations that concrete transports implement. Handles endian conversion
/// between the host CPU byte order and the wire format.
///
/// @tparam Derived       Concrete byte transport (I2cTransport, SpiTransport, etc.).
/// @tparam CheckPolicy   Enable runtime range checks (std::true_type or std::false_type).
/// @tparam ErrorPolicy   Error handler invoked on range violations.
/// @tparam AddressTypeT  Register address width on the bus (uint8_t or uint16_t).
/// @tparam DataEndian    Byte order for data on the wire.
template <class Derived,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressTypeT = std::uint8_t,
          Endian DataEndian = Endian::LITTLE>
class ByteAdapter : private RegOps<Derived, CheckPolicy, ErrorPolicy> {
    friend class RegOps<Derived, CheckPolicy, ErrorPolicy>;

  private:
    friend Derived;
    ByteAdapter() = default;

  protected:
    static_assert(std::is_integral_v<AddressTypeT>, "AddressType must be an integral type");
    static_assert(sizeof(AddressTypeT) <= 2, "AddressType must be 8 or 16 bit");

    /// @brief Pack a register value into a byte buffer according to DataEndian.
    /// @tparam T Register value type.
    /// @param value Value to serialize.
    /// @param buffer Output byte buffer (must be at least sizeof(T) bytes).
    template <typename T>
    static void pack(T value, std::uint8_t* buffer) noexcept {
        if constexpr (DataEndian == Endian::LITTLE) {
            for (std::size_t i = 0; i < sizeof(T); ++i) {
                buffer[i] = (value >> (i * 8)) & 0xFF;
            }
        } else {
            for (std::size_t i = 0; i < sizeof(T); ++i) {
                buffer[i] = (value >> ((sizeof(T) - 1 - i) * 8)) & 0xFF;
            }
        }
    }

    /// @brief Unpack a byte buffer into a register value according to DataEndian.
    /// @tparam T Register value type.
    /// @param buffer Input byte buffer (must be at least sizeof(T) bytes).
    /// @return Deserialized register value.
    template <typename T>
    static T unpack(const std::uint8_t* buffer) noexcept {
        T value = 0;
        if constexpr (DataEndian == Endian::LITTLE) {
            for (std::size_t i = 0; i < sizeof(T); ++i) {
                value |= static_cast<T>(buffer[i]) << (i * 8);
            }
        } else {
            for (std::size_t i = 0; i < sizeof(T); ++i) {
                value |= static_cast<T>(buffer[i]) << ((sizeof(T) - 1 - i) * 8);
            }
        }
        return value;
    }

    using RegOps<Derived, CheckPolicy, ErrorPolicy>::self;

  public:
    using RegOps<Derived, CheckPolicy, ErrorPolicy>::write;
    using RegOps<Derived, CheckPolicy, ErrorPolicy>::read;
    using RegOps<Derived, CheckPolicy, ErrorPolicy>::modify;
    using RegOps<Derived, CheckPolicy, ErrorPolicy>::is;
    using RegOps<Derived, CheckPolicy, ErrorPolicy>::flip;
    using AddressType = AddressTypeT;
    /// @brief Read a register by delegating to raw_read() with endian conversion.
    /// @tparam Reg Register type (provides address and RegValueType).
    /// @return Current register value.
    template <typename Reg>
    [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] auto
    reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        static_assert(sizeof(T) <= 8, "Register size must be <= 64 bits");
        static_assert(Reg::address <= static_cast<Addr>(std::numeric_limits<AddressTypeT>::max()),
                      "Register address exceeds address width for byte transport");

        std::array<std::uint8_t, sizeof(T)> buffer;
        self().raw_read(static_cast<AddressTypeT>(Reg::address), buffer.data(), sizeof(T));
        return unpack<T>(buffer.data());
    }

    /// @brief Write a register by delegating to raw_write() with endian conversion.
    /// @tparam Reg Register type (provides address and RegValueType).
    /// @param value Value to write.
    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;
        static_assert(sizeof(T) <= 8, "Register size must be <= 64 bits");
        static_assert(Reg::address <= static_cast<Addr>(std::numeric_limits<AddressTypeT>::max()),
                      "Register address exceeds address width for byte transport");

        std::array<std::uint8_t, sizeof(T)> buffer;
        pack<T>(value, buffer.data());
        self().raw_write(static_cast<AddressTypeT>(Reg::address), buffer.data(), sizeof(T));
    }
};

// I2cTransport has been moved to transport/i2c.hh

} // namespace umi::mmio
