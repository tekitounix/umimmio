// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Printf format string parsing: flags, width, precision, length, conversion.
/// @author Shota Moriguchi @tekitounix
#pragma once

#include "printf_config.hh"

namespace rt {

/// @brief Parse printf flag characters (`-`, `0`, `+`, ` `, `#`).
/// @tparam Config PrintConfig controlling which flags are active.
/// @param format Pointer into format string (at `%`).
/// @param spec   FormatSpec to populate.
/// @return Pointer past the last consumed flag character.
template <typename Config>
const char* parse_flags(const char* format, FormatSpec<Config>& spec) {
    const char* cur = format;

    // Initialize flags
    if constexpr (Config::use_field_width) {
        spec.flags.left_justified = false;
        spec.flags.zero_pad = false;
    }
    spec.case_adjust = 'a' - 'A';
    spec.prepend = 0;
    if constexpr (Config::use_alt_form) {
        spec.alt_form = false;
    }

    // Parse flags
    while (*++cur) {
        switch (*cur) {
        case '-':
            if constexpr (Config::use_field_width) {
                spec.flags.left_justified = true;
            }
            continue;
        case '0':
            if constexpr (Config::use_field_width) {
                spec.flags.zero_pad = true;
            }
            continue;
        case '+':
            spec.prepend = '+';
            continue;
        case ' ':
            if (spec.prepend == 0) {
                spec.prepend = ' ';
            }
            continue;
        case '#':
            if constexpr (Config::use_alt_form) {
                spec.alt_form = true;
            }
            continue;
        default:
            break;
        }
        break;
    }

    return cur;
}

/// @brief Parse field-width specifier (literal or `*`).
/// @tparam Config PrintConfig controlling field-width support.
/// @param cur  Current position in format string.
/// @param spec FormatSpec to populate.
/// @return Pointer past the consumed field-width specifier.
template <typename Config>
const char* parse_field_width(const char* cur, FormatSpec<Config>& spec) {
    if constexpr (Config::use_field_width) {
        spec.field_width.value = 0;
        spec.field_width.opt = FormatOption::NONE;

        if (*cur == '*') {
            spec.field_width.opt = FormatOption::STAR;
            ++cur;
        } else {
            while (*cur >= '0' && *cur <= '9') {
                spec.field_width.opt = FormatOption::LITERAL;
                spec.field_width.value = spec.field_width.value * 10 + (*cur++ - '0');
            }
        }
    }

    return cur;
}

/// @brief Parse precision specifier (`.N` or `.*`).
/// @tparam Config PrintConfig controlling precision support.
/// @param cur  Current position in format string.
/// @param spec FormatSpec to populate.
/// @return Pointer past the consumed precision specifier.
template <typename Config>
const char* parse_precision(const char* cur, FormatSpec<Config>& spec) {
    if constexpr (Config::use_precision) {
        spec.precision.value = 0;
        spec.precision.opt = FormatOption::NONE;

        if (*cur == '.') {
            ++cur;
            if (*cur == '*') {
                spec.precision.opt = FormatOption::STAR;
                ++cur;
            } else {
                while (*cur >= '0' && *cur <= '9') {
                    spec.precision.opt = FormatOption::LITERAL;
                    spec.precision.value = spec.precision.value * 10 + (*cur++ - '0');
                }
                if (spec.precision.opt == FormatOption::NONE) {
                    spec.precision.opt = FormatOption::LITERAL;
                }
            }
        }
    }

    return cur;
}

/// @brief Parse length modifier (`h`, `hh`, `l`, `ll`, `L`, `j`, `z`, `t`).
/// @tparam Config PrintConfig controlling large/small modifier support.
/// @param cur  Current position in format string.
/// @param spec FormatSpec to populate.
/// @return Pointer past the consumed length modifier.
template <typename Config>
const char* parse_length_modifier(const char* cur, FormatSpec<Config>& spec) {
    spec.length_mod = LengthModifier::NONE;
    switch (*cur) {
    case 'h':
        if constexpr (Config::use_small) {
            spec.length_mod = LengthModifier::H;
            ++cur;
            if (*cur == 'h') {
                spec.length_mod = LengthModifier::HH;
                ++cur;
            }
        }
        break;
    case 'l':
        spec.length_mod = LengthModifier::L;
        ++cur;
        if constexpr (Config::use_large) {
            if (*cur == 'l') {
                spec.length_mod = LengthModifier::LL;
                ++cur;
            }
        }
        break;
    case 'L':
        spec.length_mod = LengthModifier::LD;
        ++cur;
        break;
    case 'j':
        if constexpr (Config::use_large) {
            spec.length_mod = LengthModifier::J;
            ++cur;
        }
        break;
    case 'z':
        if constexpr (Config::use_large) {
            spec.length_mod = LengthModifier::Z;
            ++cur;
        }
        break;
    case 't':
        if constexpr (Config::use_large) {
            spec.length_mod = LengthModifier::T;
            ++cur;
        }
        break;
    default:
        // No length modifier
        break;
    }

    return cur;
}

/// @brief Parse conversion specifier character (`d`, `x`, `f`, `s`, etc.).
/// @tparam Config PrintConfig controlling which specifiers are enabled.
/// @param cur  Current position in format string.
/// @param spec FormatSpec to populate with conversion type and case.
/// @return Pointer at the consumed specifier character.
template <typename Config>
const char* parse_conversion_specifier(const char* cur, FormatSpec<Config>& spec) {
    spec.conv = Conversion::NONE;
    switch (*cur) {
    case '%':
        spec.conv = Conversion::PERCENT;
        break;
    case 'c':
        spec.conv = Conversion::CHARACTER;
        break;
    case 's':
        spec.conv = Conversion::STRING;
        break;
    case 'd':
    case 'i':
        spec.conv = Conversion::SIGNED_INT;
        break;
    case 'u':
        spec.conv = Conversion::UNSIGNED_INT;
        break;
    case 'o':
        spec.conv = Conversion::OCTAL;
        break;
    case 'x':
        spec.conv = Conversion::HEX;
        break;
    case 'X':
        spec.conv = Conversion::HEX;
        spec.case_adjust = 0;
        break;
    case 'p':
        spec.conv = Conversion::POINTER;
        break;

    case 'b':
        if constexpr (Config::use_binary) {
            spec.conv = Conversion::BINARY;
        }
        break;

    case 'B':
        if constexpr (Config::use_binary) {
            spec.conv = Conversion::BINARY;
            spec.case_adjust = 0;
        }
        break;

    case 'n':
        if constexpr (Config::use_writeback) {
            spec.conv = Conversion::WRITEBACK;
        }
        break;

    case 'f':
    case 'F':
        if constexpr (Config::use_float) {
            spec.conv = Conversion::FLOAT_DEC;
            if (*cur == 'F') {
                spec.case_adjust = 0;
            }
        }
        break;

    case 'e':
    case 'E':
        if constexpr (Config::use_float) {
            spec.conv = Conversion::FLOAT_SCI;
            if (*cur == 'E') {
                spec.case_adjust = 0;
            }
        }
        break;

    case 'g':
    case 'G':
        if constexpr (Config::use_float) {
            spec.conv = Conversion::FLOAT_SHORTEST;
            if (*cur == 'G') {
                spec.case_adjust = 0;
            }
        }
        break;

    case 'a':
    case 'A':
        if constexpr (Config::use_float) {
            spec.conv = Conversion::FLOAT_HEX;
            if (*cur == 'A') {
                spec.case_adjust = 0;
            }
        }
        break;
    default:
        // Unknown Conversion specifier
        break;
    }

    return cur;
}

/// @brief Parse a complete `%` format specification (flags -> width -> precision -> length -> conversion).
/// @tparam Config PrintConfig controlling enabled features.
/// @param format Pointer to the `%` character.
/// @param spec   FormatSpec to populate.
/// @return Number of characters consumed (including `%`), or 0 on failure.
template <typename Config>
[[gnu::noinline]]
int parse_format_spec(const char* format, FormatSpec<Config>& spec) {
    const char* cur = format;

    cur = parse_flags<Config>(cur, spec);
    cur = parse_field_width<Config>(cur, spec);
    cur = parse_precision<Config>(cur, spec);
    cur = parse_length_modifier<Config>(cur, spec);
    cur = parse_conversion_specifier<Config>(cur, spec);

    if (spec.conv == Conversion::NONE) {
        return 0;
    }

    return static_cast<int>(cur - format + 1);
}

} // namespace rt
