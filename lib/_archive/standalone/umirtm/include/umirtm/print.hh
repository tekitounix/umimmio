// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief `{}` placeholder-based print helpers built on top of printf.
/// @author Shota Moriguchi @tekitounix
///
/// Converts `{}` format strings to printf specifiers at runtime,
/// with support for `{:x}` style format overrides.
#pragma once
#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "printf.hh"

namespace rt {
namespace detail {

/// @brief Deduce the printf specifier character for a given type.
/// @return 'd' for signed int, 'u' for unsigned, 'f' for float, 's' for string, etc.
template <typename T>
constexpr char get_format_spec() {
    if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, char>) {
        return 'c';
    } else if constexpr (std::is_pointer_v<std::remove_reference_t<T>>) {
        if constexpr (std::is_same_v<std::remove_cv_t<std::remove_pointer_t<std::remove_reference_t<T>>>, char>) {
            return 's';
        } else {
            return 'p';
        }
    } else if constexpr (std::is_integral_v<T>) {
        return std::is_unsigned_v<T> ? 'u' : 'd';
    } else if constexpr (std::is_floating_point_v<T>) {
        return 'f';
    } else {
        // Default for string_view and other types
        return 's';
    }
}

/// @brief Converter from `{}` format to printf and dispatch to stdout.
/// @tparam Args Argument types (used to auto-select format specifiers).
template <typename... Args>
struct FormatConverter {
    static constexpr size_t arg_count = sizeof...(Args);
    static constexpr std::array<char, arg_count + 1> type_specs = {get_format_spec<Args>()..., '\0'};
    static int convert_and_print_stdout(const char* fmt, Args... args) {
        std::array<char, 512> buf;
        size_t out = 0;
        size_t arg_idx = 0;
        const char* p = fmt;
        while ((*p != '\0') && (out < sizeof(buf) - 2)) {
            if (*p == '{' && p[1] == '{') {
                buf[out++] = '{';
                p += 2;
            } else if (*p == '}' && p[1] == '}') {
                buf[out++] = '}';
                p += 2;
            } else if (*p == '{' && p[1] == '}') {
                if (arg_idx < arg_count) {
                    buf[out++] = '%';
                    buf[out++] = type_specs[arg_idx++];
                }
                p += 2;
            } else if (*p == '{') {
                const char* end = p + 1;
                while ((*end != '\0') && (*end != '}')) {
                    end++;
                }
                if (*end == '}') {
                    buf[out++] = '%';
                    const char* spec = p + 1;
                    while (spec < end && *spec >= '0' && *spec <= '9') {
                        spec++;
                    }
                    if (spec < end && *spec == ':') {
                        spec++;
                        while (spec < end && out < sizeof(buf) - 1) {
                            char ch = *spec++;
                            buf[out++] = ch;
                            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
                                arg_idx++;
                                break;
                            }
                        }
                    } else {
                        if (arg_idx < arg_count) {
                            buf[out++] = type_specs[arg_idx++];
                        }
                    }
                    p = end + 1;
                } else {
                    buf[out++] = *p++;
                }
            } else {
                buf[out++] = *p++;
            }
        }
        buf[out] = '\0';
        return rt::printf(buf.data(), args...);
    }
};

template <>
struct FormatConverter<> {
    static int convert_and_print_stdout(const char* fmt) {
        // just write as-is to stdout
        return rt::printf("%s", fmt);
    }
};

} // namespace detail

/// @brief Print with `{}` placeholders to stdout.
/// @param fmt Format string with `{}` placeholders.
/// @return Number of characters written.
template <typename... Args>
inline int print(std::string_view fmt, Args... args) {
    return detail::FormatConverter<Args...>::convert_and_print_stdout(fmt.data(), args...);
}

/// @brief Print with `{}` placeholders (const char* overload).
template <typename... Args>
inline int print(const char* fmt, Args... args) {
    return print(std::string_view(fmt), args...);
}

/// @brief Print a newline.
inline int println() {
    return rt::printf("\n");
}

/// @brief Print with `{}` placeholders followed by a newline.
template <typename... Args>
inline int println(std::string_view fmt, Args... args) {
    int n = print(fmt, args...);
    int m = rt::printf("\n");
    return n + m;
}

/// @brief Print with `{}` placeholders followed by a newline (const char* overload).
template <typename... Args>
inline int println(const char* fmt, Args... args) {
    return println(std::string_view(fmt), args...);
}

} // namespace rt