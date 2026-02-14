// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Printf configuration types: PrintConfig, FormatSpec, enums, type aliases.
/// @author Shota Moriguchi @tekitounix
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace rt {

/// @brief Configuration options as template parameters.
/// @tparam UseFieldWidth Enable field width specifiers.
/// @tparam UsePrecision  Enable precision specifiers.
/// @tparam UseFloat      Enable floating-point format (%f, %e, %g).
/// @tparam UseLarge      Enable long long / intmax_t.
/// @tparam UseSmall      Enable short / char length modifiers.
/// @tparam UseBinary     Enable %b binary format.
/// @tparam UseWriteback  Enable %n write-back.
/// @tparam UseAltForm    Enable # alternate form.
template <bool UseFieldWidth = true,
          bool UsePrecision = true,
          bool UseFloat = true,
          bool UseLarge = false,
          bool UseSmall = true,
          bool UseBinary = false,
          bool UseWriteback = false,
          bool UseAltForm = true>
struct PrintConfig {
    static constexpr bool use_field_width = UseFieldWidth;
    static constexpr bool use_precision = UsePrecision;
    static constexpr bool use_float = UseFloat;
    static constexpr bool use_large = UseLarge;
    static constexpr bool use_small = UseSmall;
    static constexpr bool use_binary = UseBinary;
    static constexpr bool use_writeback = UseWriteback;
    static constexpr bool use_alt_form = UseAltForm;

    // Validate configuration
    static_assert(!use_float || use_precision,
                  "Precision format specifiers must be enabled if float support is enabled");
};

using DefaultConfig = PrintConfig<true, true, true, false, true, false, false, true>;      ///< Default config.
using FullConfig = PrintConfig<true, true, true, true, true, true, false, true>;           ///< Full-featured.
using MinimalConfig = PrintConfig<false, false, false, false, false, false, false, false>; ///< Smallest footprint.

/// @brief Format option for field width / precision.
enum class FormatOption : uint8_t { NONE, LITERAL, STAR };

/// @brief C99 length modifiers.
enum class LengthModifier : uint8_t {
    NONE,
    H,  // short
    HH, // char
    L,  // long
    LD, // long double
    LL, // long long
    J,  // intmax_t
    Z,  // size_t
    T   // ptrdiff_t
};

/// @brief Conversion specifier types.
enum class Conversion : uint8_t {
    NONE,
    PERCENT,        // '%'
    CHARACTER,      // 'c'
    STRING,         // 's'
    SIGNED_INT,     // 'i', 'd'
    BINARY,         // 'b'
    OCTAL,          // 'o'
    HEX,            // 'x', 'X'
    UNSIGNED_INT,   // 'u'
    POINTER,        // 'p'
    WRITEBACK,      // 'n'
    FLOAT_DEC,      // 'f', 'F'
    FLOAT_SCI,      // 'e', 'E'
    FLOAT_SHORTEST, // 'g', 'G'
    FLOAT_HEX       // 'a', 'A'
};

/// @brief Empty type for [[no_unique_address]] conditional members.
struct Empty {};

/// @brief Parsed format specification for a single `%` directive.
/// @tparam Config PrintConfig controlling which fields are active.
template <typename Config>
struct FormatSpec {
    // Field width
    struct field_width_t {
        int value;
        FormatOption opt;
    };
    [[no_unique_address]] std::conditional_t<Config::use_field_width, field_width_t, Empty> field_width{};

    // Precision
    struct precision_t {
        int value;
        FormatOption opt;
    };
    [[no_unique_address]] std::conditional_t<Config::use_precision, precision_t, Empty> precision{};

    // Flags
    struct flags_t {
        bool left_justified;
        bool zero_pad;
    };
    [[no_unique_address]] std::conditional_t<Config::use_field_width, flags_t, Empty> flags{};

    char prepend = 0; // ' ' or '+'

    [[no_unique_address]] std::conditional_t<Config::use_alt_form, bool, Empty> alt_form{};

    char case_adjust = 'a' - 'A'; // lowercase by default
    LengthModifier length_mod = LengthModifier::NONE;
    Conversion conv = Conversion::NONE;
};

/// @brief Signed integer type used by printf, sized per Config.
template <typename Config>
using printf_int_t =
    std::conditional_t<Config::use_large,
                       std::intmax_t,
                       std::conditional_t<(sizeof(long) > sizeof(std::intptr_t)), long, std::intptr_t>>;

/// @brief Unsigned integer type used by printf, sized per Config.
template <typename Config>
using printf_uint_t = std::conditional_t<
    Config::use_large,
    std::uintmax_t,
    std::conditional_t<(sizeof(unsigned long) > sizeof(std::uintptr_t)), unsigned long, std::uintptr_t>>;

/// @brief Return the smaller of two values.
/// @tparam T Comparable type.
template <typename T>
constexpr T min(T a, T b) {
    return a < b ? a : b;
}

/// @brief Return the larger of two values.
/// @tparam T Comparable type.
template <typename T>
constexpr T max(T a, T b) {
    return a > b ? a : b;
}

} // namespace rt
