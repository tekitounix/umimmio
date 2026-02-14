// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Printf conversion routines: integer, float, field-width formatting.
/// @author Shota Moriguchi @tekitounix
#pragma once

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

#include "printf_config.hh"
#include "printf_parse.hh"

namespace rt {

namespace detail {
/// @brief Per-character output callback type.
using PutcFunc = void (*)(int c, void* ctx);

/// @brief State for buffer-based output via bufputc.
struct BufferContext {
    char* dst;         ///< Destination buffer.
    std::size_t len;   ///< Buffer capacity.
    std::size_t cur;   ///< Characters written so far (may exceed len).
};
} // namespace detail

/// @brief Buffer size for integer/float-to-string conversion (fits UINT64_MAX in octal).
constexpr std::size_t conversion_buffer_size = 23;

/// @brief PutcFunc callback that writes into a BufferContext.
inline void bufputc(int c, void* ctx) {
    auto* buf_ctx = static_cast<detail::BufferContext*>(ctx);
    if (buf_ctx->cur < buf_ctx->len) {
        buf_ctx->dst[buf_ctx->cur] = static_cast<char>(c);
    }
    ++buf_ctx->cur;
}

/// @brief Number base for integer-to-string conversion.
enum class NumberBase : uint_fast8_t { BINARY = 2, OCTAL = 8, DECIMAL = 10, HEXADECIMAL = 16 };
/// @brief Case adjustment for hex digits.
enum class CaseAdjustment : std::uint8_t { LOWERCASE = 'a' - 'A', UPPERCASE = 0 };

/// @brief Convert unsigned integer to ASCII in reverse order.
/// @return Number of characters written to buf.
template <typename Config>
[[gnu::noinline]]
int utoa_rev(printf_uint_t<Config> val, char* buf, CaseAdjustment case_adj, NumberBase base) {
    uint_fast8_t n = 0;
    do {
        auto const d = static_cast<int_fast8_t>(val % static_cast<uint_fast8_t>(base));
        *buf++ = static_cast<char>(((d < 10) ? '0' : ('A' - 10 + static_cast<int>(case_adj))) + d);
        ++n;
        val /= static_cast<uint_fast8_t>(base);
    } while (val);
    return static_cast<int>(n);
}

/// @brief Floating-point formatting support (conditionally compiled via Config::use_float).
/// @tparam Config PrintConfig controlling float feature availability.
template <typename Config>
struct FloatSupport {
    static constexpr bool enabled = Config::use_float;

    // Types for floating point Conversion
    using double_bin_t = std::conditional_t<std::numeric_limits<double>::digits <= 24, uint_fast32_t, uint_fast64_t>;

    using ftoa_exp_t = std::conditional_t<std::numeric_limits<double>::max_exponent <= 128, int_fast8_t, int_fast16_t>;

    using ftoa_man_t = unsigned int;
    using ftoa_dec_t =
        std::conditional_t<conversion_buffer_size <= std::numeric_limits<uint_fast8_t>::max(), uint_fast8_t, int>;

    static constexpr int double_exp_mask = (std::numeric_limits<double>::max_exponent * 2) - 1;
    static constexpr int double_exp_bias = std::numeric_limits<double>::max_exponent - 1;
    static constexpr int double_man_bits = std::numeric_limits<double>::digits - 1;
    static constexpr int double_bin_bits = sizeof(double_bin_t) * 8;
    static constexpr int double_sign_pos = (sizeof(double) * 8) - 1;
    static constexpr int ftoa_man_bits = sizeof(ftoa_man_t) * 8;
    static constexpr int ftoa_shift_bits = min(ftoa_man_bits, std::numeric_limits<double>::digits) - 1;

    /// @brief Reinterpret a double as its integer bit-pattern.
    /// @param f Input floating-point value.
    /// @return Unsigned integer with the same bit representation.
    [[gnu::always_inline]]
    static double_bin_t double_to_int_rep(double f) {
        double_bin_t bin;
        std::memcpy(&bin, &f, sizeof(f));
        return bin;
    }

    /// @brief Extract exponent/mantissa and detect NaN/Inf.
    /// @param f   Input value.
    /// @param bin [out] Mantissa bits.
    /// @param exp [out] Biased exponent.
    /// @param ret [out] Set to reversed special-value string if detected.
    /// @return true if the value is special (NaN or Inf).
    static bool handle_special_values(double f, double_bin_t& bin, ftoa_exp_t& exp, const char*& ret) {
        bin = double_to_int_rep(f);

        // Extract exponent
        exp = static_cast<ftoa_exp_t>(static_cast<ftoa_exp_t>(bin >> double_man_bits) & double_exp_mask);
        bin &= (static_cast<double_bin_t>(1) << double_man_bits) - 1;

        // Special values
        if (exp == static_cast<ftoa_exp_t>(double_exp_mask)) {
            ret = (bin != static_cast<double_bin_t>(0)) ? "NAN" : "FNI";
            return true; // Special value found
        }

        return false; // Normal value
    }

    /// @brief Convert the integer portion of a float into decimal digits (reversed).
    /// @param buf   Output buffer.
    /// @param dec   Current write position in buf.
    /// @param bin   [in/out] Mantissa bits.
    /// @param exp   [in/out] Exponent (base-2).
    /// @param carry [out] Carry from base conversion.
    /// @return New end position in buf, or conversion_buffer_size on overflow.
    static ftoa_dec_t
    process_integer_part(char* buf, ftoa_dec_t dec, double_bin_t& bin, ftoa_exp_t& exp, uint_fast8_t& carry) {
        ftoa_dec_t end = dec;
        ftoa_man_t man_i;

        if (exp >= 0) {
            auto shift_i = static_cast<int_fast8_t>((exp > ftoa_shift_bits) ? static_cast<int>(ftoa_shift_bits) : exp);
            auto exp_i = static_cast<ftoa_exp_t>(exp - shift_i);
            shift_i = static_cast<int_fast8_t>(double_man_bits - shift_i);
            man_i = static_cast<ftoa_man_t>(bin >> shift_i);

            if (exp_i != 0) {
                if (shift_i != 0) {
                    carry = (bin >> (shift_i - 1)) & 0x1;
                }
                exp = double_man_bits; // invalidate fraction part
            }

            // Scale exponent from base-2 to base-10
            for (; exp_i != 0; --exp_i) {
                if ((man_i & (static_cast<ftoa_man_t>(1) << (ftoa_man_bits - 1))) == 0) {
                    man_i = static_cast<ftoa_man_t>(man_i << 1);
                    man_i = static_cast<ftoa_man_t>(man_i | carry);
                    carry = 0;
                } else {
                    if (dec >= conversion_buffer_size) {
                        return conversion_buffer_size; // Signal error
                    }
                    buf[dec++] = '0';
                    carry = (((static_cast<uint_fast8_t>(man_i % 5) + carry) > 2)) ? 1 : 0;
                    man_i /= 5;
                }
            }
        } else {
            man_i = 0;
        }
        end = dec;

        // Print integer digits
        do {
            if (end >= conversion_buffer_size) {
                return conversion_buffer_size; // Signal error
            }
            buf[end++] = static_cast<char>('0' + static_cast<char>(man_i % 10));
            man_i /= 10;
        } while (man_i != 0);

        return end;
    }

    /// @brief Convert the fractional portion of a float into decimal digits.
    /// @param buf      Output buffer.
    /// @param prec_val Number of fractional digits requested.
    /// @param bin      Mantissa bits.
    /// @param carry    [in/out] Carry from integer-part conversion.
    /// @param exp      Exponent (base-2).
    /// @param dec      [out] Updated decimal position.
    /// @return true on success.
    static bool process_fractional_part(
        char* buf, ftoa_dec_t prec_val, double_bin_t bin, uint_fast8_t& carry, ftoa_exp_t exp, ftoa_dec_t& dec) {
        ftoa_man_t man_f;
        ftoa_dec_t dec_f = prec_val;

        if (exp < double_man_bits) {
            auto shift_f = static_cast<int_fast8_t>((exp < 0) ? -1 : exp);
            auto exp_f = static_cast<ftoa_exp_t>(exp - shift_f);
            auto bin_f = bin << ((double_bin_bits - double_man_bits) + shift_f);

            // Extract mantissa
            if (double_bin_bits > ftoa_man_bits) {
                man_f =
                    static_cast<ftoa_man_t>(bin_f >> ((unsigned)(double_bin_bits - ftoa_man_bits) % double_bin_bits));
                carry = static_cast<uint_fast8_t>(
                    (bin_f >> ((unsigned)(double_bin_bits - ftoa_man_bits - 1) % double_bin_bits)) & 0x1);
            } else {
                man_f = static_cast<ftoa_man_t>(static_cast<ftoa_man_t>(bin_f)
                                                << ((unsigned)(ftoa_man_bits - double_bin_bits) % ftoa_man_bits));
                carry = 0;
            }

            // Scale from base-2 to base-10
            for (uint_fast8_t digit = 0; (dec_f != 0) && (exp_f < 4); ++exp_f) {
                if ((man_f > (static_cast<ftoa_man_t>(-4) / 5)) || (digit != 0)) {
                    carry = static_cast<uint_fast8_t>(man_f & 0x1);
                    man_f = static_cast<ftoa_man_t>(man_f >> 1);
                } else {
                    man_f = man_f * 5;
                    if (carry != 0) {
                        man_f = static_cast<ftoa_man_t>(man_f + 3);
                        carry = 0;
                    }
                    if (exp_f < 0) {
                        buf[--dec_f] = '0';
                    } else {
                        ++digit;
                    }
                }
            }

            man_f = static_cast<ftoa_man_t>(man_f + carry);
            carry = (exp_f >= 0) ? 1 : 0;
            dec = 0;
        } else {
            man_f = 0;
        }

        if (dec_f != 0) {
            // Print fraction digits
            for (;;) {
                buf[--dec_f] = static_cast<char>('0' + static_cast<char>(man_f >> (ftoa_man_bits - 4)));
                man_f = (man_f & ~(static_cast<ftoa_man_t>(0xF) << (ftoa_man_bits - 4)));
                if (dec_f == 0) {
                    break;
                }
                man_f = man_f * 10;
            }
            man_f = man_f << 4;
        }

        if (exp < double_man_bits) {
            carry &= static_cast<uint_fast8_t>(man_f >> (ftoa_man_bits - 1));
        }

        return true;
    }

    /// @brief Convert a non-negative double to ASCII digits (reversed in buf).
    /// @param buf  Output buffer (at least conversion_buffer_size bytes).
    /// @param spec FormatSpec for precision and flags.
    /// @param f    Non-negative floating-point value.
    /// @return Positive count of characters, or negative count for special values.
    static int ftoa_rev(char* buf, const FormatSpec<Config>& spec, double f) {
        const char* ret = nullptr;
        double_bin_t bin;
        ftoa_exp_t exp;
        int prec_val = 6; // default
        uint_fast8_t carry = 0;
        ftoa_dec_t dec;
        ftoa_dec_t end;
        bool need_decimal;

        // Handle special values
        if (handle_special_values(f, bin, exp, ret)) {
            goto exit;
        }

        // Check precision limit
        if constexpr (Config::use_precision) {
            if (spec.precision.opt != FormatOption::NONE) {
                prec_val = spec.precision.value;
            }
        }
        if (prec_val > static_cast<int>(conversion_buffer_size - 2)) {
            goto exit;
        }

        // Normal or subnormal number
        if (exp != 0) {
            bin |= static_cast<double_bin_t>(1) << double_man_bits;
        } else {
            ++exp;
        }
        exp = static_cast<ftoa_exp_t>(exp - double_exp_bias);

        carry = 0;
        dec = static_cast<ftoa_dec_t>(prec_val);

        // Add decimal point
        need_decimal = (dec != 0);
        if constexpr (Config::use_alt_form) {
            if (spec.alt_form && dec == 0) {
                need_decimal = true;
            }
        }
        if (need_decimal) {
            buf[dec++] = '.';
        }

        // Process integer part
        end = process_integer_part(buf, dec, bin, exp, carry);
        if (end >= conversion_buffer_size) {
            goto exit;
        }

        // Process fractional part
        if (!process_fractional_part(buf, static_cast<ftoa_dec_t>(prec_val), bin, carry, exp, dec)) {
            goto exit;
        }

        // Round the number
        for (; carry != 0; ++dec) {
            if (dec >= conversion_buffer_size) {
                goto exit;
            }
            if (dec >= end) {
                buf[end++] = '0';
            }
            if (buf[dec] == '.') {
                continue;
            }
            carry = (buf[dec] == '9') ? 1 : 0;
            buf[dec] = static_cast<char>((carry != 0) ? '0' : (buf[dec] + 1));
        }

        return static_cast<int>(end);

    exit:
        if (ret == nullptr) {
            ret = "RRE";
        }
        uint_fast8_t i;
        for (i = 0; ret[i] != '\0'; ++i) {
            buf[i] = static_cast<char>(ret[i] + spec.case_adjust);
        }
        return -static_cast<int>(i);
    }
};

/// @brief Handle integer/pointer conversion: extract value, convert to string, add prefix/sign.
/// @tparam Config PrintConfig.
/// @param spec Format specification.
/// @param args va_list to consume the next argument from.
/// @param buf  Output buffer (at least conversion_buffer_size bytes).
/// @param slen [out] Length of the resulting string in buf.
/// @return 0 on success.
template <typename Config>
int handle_integer_conversion(const FormatSpec<Config>& spec, va_list& args, char* buf, int& slen) {
    printf_uint_t<Config> val = 0;
    bool const is_signed = (spec.conv == Conversion::SIGNED_INT);

    if (spec.conv == Conversion::POINTER) {
        val = reinterpret_cast<std::uintptr_t>(va_arg(args, void*));
    } else {
        // Get value based on length modifier
        if (is_signed) {
            printf_int_t<Config> sval = 0;
            // NOLINTBEGIN(bugprone-branch-clone) - conditional compilation patterns
            switch (spec.length_mod) {
            case LengthModifier::NONE:
            case LengthModifier::H:
            case LengthModifier::HH:
                sval = va_arg(args, int);
                break;
            case LengthModifier::L:
                sval = va_arg(args, long);
                break;
            case LengthModifier::LL:
                if constexpr (Config::use_large) {
                    sval = va_arg(args, long long);
                }
                break;
            case LengthModifier::J:
                if constexpr (Config::use_large) {
                    sval = va_arg(args, std::intmax_t);
                }
                break;
            case LengthModifier::Z:
                if constexpr (Config::use_large) {
                    sval = static_cast<std::make_signed_t<std::size_t>>(va_arg(args, std::size_t));
                }
                break;
            case LengthModifier::T:
                if constexpr (Config::use_large) {
                    sval = va_arg(args, std::ptrdiff_t);
                }
                break;
            case LengthModifier::LD:
            default:
                // Unsupported length modifier
                break;
            }
            // NOLINTEND(bugprone-branch-clone)

            if (sval < 0) {
                const_cast<FormatSpec<Config>&>(spec).prepend = '-';
                val = static_cast<printf_uint_t<Config>>(-sval);
            } else {
                val = static_cast<printf_uint_t<Config>>(sval);
            }
        } else {
            // Unsigned types
            // NOLINTBEGIN(bugprone-branch-clone) - conditional compilation patterns
            switch (spec.length_mod) {
            case LengthModifier::NONE:
            case LengthModifier::H:
            case LengthModifier::HH:
                val = va_arg(args, unsigned int);
                break;
            case LengthModifier::L:
                val = va_arg(args, unsigned long);
                break;
            case LengthModifier::LL:
                if constexpr (Config::use_large) {
                    val = va_arg(args, unsigned long long);
                }
                break;
            case LengthModifier::J:
                if constexpr (Config::use_large) {
                    val = va_arg(args, std::uintmax_t);
                }
                break;
            case LengthModifier::Z:
                if constexpr (Config::use_large) {
                    val = va_arg(args, std::size_t);
                }
                break;
            case LengthModifier::T:
                if constexpr (Config::use_large) {
                    val = static_cast<std::make_unsigned_t<std::ptrdiff_t>>(va_arg(args, std::ptrdiff_t));
                }
                break;
            case LengthModifier::LD:
            default:
                // Unsupported length modifier
                break;
            }
            // NOLINTEND(bugprone-branch-clone)
        }
    }

    // Determine base
    NumberBase base = NumberBase::DECIMAL;
    switch (spec.conv) {
    case Conversion::OCTAL:
        base = NumberBase::OCTAL;
        break;
    case Conversion::HEX:
    case Conversion::POINTER:
        base = NumberBase::HEXADECIMAL;
        break;
    case Conversion::BINARY:
        base = NumberBase::BINARY;
        break;
    case Conversion::SIGNED_INT:
    case Conversion::UNSIGNED_INT:
    default:
        base = NumberBase::DECIMAL;
        break;
    }

    // Convert to string
    CaseAdjustment const case_adjustment =
        (spec.case_adjust == 0) ? CaseAdjustment::UPPERCASE : CaseAdjustment::LOWERCASE;
    slen = utoa_rev<Config>(val, buf, case_adjustment, base);

    // Add prefix for alt form or pointer
    if constexpr (Config::use_alt_form) {
        if ((spec.alt_form && val != 0) || spec.conv == Conversion::POINTER) {
            switch (spec.conv) {
            case Conversion::OCTAL:
                buf[slen++] = '0';
                break;
            case Conversion::HEX:
                buf[slen++] = (spec.case_adjust == 0) ? 'X' : 'x';
                buf[slen++] = '0';
                break;
            case Conversion::POINTER:
                buf[slen++] = 'x';
                buf[slen++] = '0';
                break;
            case Conversion::BINARY:
                if constexpr (Config::use_binary) {
                    buf[slen++] = (spec.case_adjust == 0) ? 'B' : 'b';
                    buf[slen++] = '0';
                }
                break;
            case Conversion::SIGNED_INT:
            case Conversion::UNSIGNED_INT:
            default:
                // No prefix for these conversions
                break;
            }
        }
    }

    // Add sign
    if (spec.prepend != 0) {
        buf[slen++] = spec.prepend;
    }

    // Reverse buffer
    for (int i = 0; i < slen / 2; ++i) {
        std::swap(buf[i], buf[slen - 1 - i]);
    }

    return 0; // success
}

/// @brief Handle float conversion: extract double, format with sign, reverse output.
/// @tparam Config PrintConfig.
/// @param spec Format specification.
/// @param args va_list to consume the next argument from.
/// @param buf  Output buffer (at least conversion_buffer_size bytes).
/// @param slen [out] Length of the resulting string in buf.
/// @return 0 on success.
template <typename Config>
int handle_float_conversion(const FormatSpec<Config>& spec, va_list& args, char* buf, int& slen) {
    if constexpr (Config::use_float) {
        double f = va_arg(args, double);

        // Check for negative
        if (std::signbit(f)) {
            const_cast<FormatSpec<Config>&>(spec).prepend = '-';
            f = -f;
        }

        slen = FloatSupport<Config>::ftoa_rev(buf, spec, f);

        if (slen < 0) {
            // Special value (NaN, Inf, Error)
            slen = -slen;
        } else {
            // Add sign if needed
            if (spec.prepend != 0) {
                buf[slen++] = spec.prepend;
            }

            // Reverse buffer
            for (int i = 0; i < slen / 2; ++i) {
                std::swap(buf[i], buf[slen - 1 - i]);
            }
        }
    }
    return 0; // success
}

/// @brief Emit a formatted string with field-width padding and justification.
/// @tparam Config PrintConfig.
/// @param spec   Format specification (width, padding, justification).
/// @param pc     Character output callback.
/// @param pc_ctx Opaque context for pc.
/// @param s      Formatted string to emit.
/// @param slen   Length of s.
/// @param n      [in/out] Running character count.
template <typename Config>
void apply_field_width(
    const FormatSpec<Config>& spec, detail::PutcFunc pc, void* pc_ctx, const char* s, int slen, int& n) {
    if (s != nullptr && slen > 0) {
        int pad = 0;
        if constexpr (Config::use_field_width) {
            if (spec.field_width.value > slen) {
                pad = spec.field_width.value - slen;
            }

            if (!spec.flags.left_justified && pad > 0) {
                char const pad_char = spec.flags.zero_pad ? '0' : ' ';
                for (int i = 0; i < pad; ++i) {
                    pc(pad_char, pc_ctx);
                    ++n;
                }
            }
        }

        for (int i = 0; i < slen; ++i) {
            pc(s[i], pc_ctx);
            ++n;
        }

        if constexpr (Config::use_field_width) {
            if (spec.flags.left_justified && pad > 0) {
                for (int i = 0; i < pad; ++i) {
                    pc(' ', pc_ctx);
                    ++n;
                }
            }
        }
    }
}

} // namespace rt
