#pragma once

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

namespace rt {

// Configuration options as template parameters
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

// Default configuration - nanoprintf compatible
using DefaultConfig = PrintConfig<true, true, true, false, true, false, false, true>;

// Full featured configuration
using FullConfig = PrintConfig<true, true, true, true, true, true, false, true>;

// Minimal configuration for embedded systems
using MinimalConfig = PrintConfig<false, false, false, false, false, false, false, false>;

// Format specification options
enum class FormatOption : uint8_t { NONE, LITERAL, STAR };

// Length modifiers
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

// Conversion specifiers
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

// Empty type for conditional members
struct Empty {};

// Format specification structure
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

// Integer types based on configuration
template <typename Config>
using printf_int_t =
    std::conditional_t<Config::use_large,
                       std::intmax_t,
                       std::conditional_t<(sizeof(long) > sizeof(std::intptr_t)), long, std::intptr_t>>;

template <typename Config>
using printf_uint_t = std::conditional_t<
    Config::use_large,
    std::uintmax_t,
    std::conditional_t<(sizeof(unsigned long) > sizeof(std::uintptr_t)), unsigned long, std::uintptr_t>>;

namespace detail {
// Output function type (internal)
using PutcFunc = void (*)(int c, void* ctx);
} // namespace detail

namespace detail {
struct BufferContext {
    char* dst;
    std::size_t len;
    std::size_t cur;
};
} // namespace detail

// Conversion buffer size (must fit UINT64_MAX in octal with leading '0')
constexpr std::size_t conversion_buffer_size = 23;

// Forward declarations
template <typename Config = DefaultConfig>
[[gnu::format(printf, 3, 0)]]
int vsnprintf(char* buffer, std::size_t bufsz, const char* format, va_list args);

template <typename Config = DefaultConfig>
[[gnu::format(printf, 3, 4)]]
int snprintf(char* buffer, std::size_t bufsz, const char* format, ...);

namespace detail {
template <typename Config = DefaultConfig>
[[gnu::format(printf, 3, 0)]]
int vpprintf(detail::PutcFunc pc, void* pc_ctx, const char* format, va_list args);

template <typename Config = DefaultConfig>
[[gnu::format(printf, 3, 4)]]
int pprintf(detail::PutcFunc pc, void* pc_ctx, const char* format, ...);
} // namespace detail

// Helper functions
template <typename T>
constexpr T min(T a, T b) {
    return a < b ? a : b;
}

template <typename T>
constexpr T max(T a, T b) {
    return a > b ? a : b;
}

// Buffer putc implementation
inline void bufputc(int c, void* ctx) {
    auto* buf_ctx = static_cast<detail::BufferContext*>(ctx);
    if (buf_ctx->cur < buf_ctx->len) {
        buf_ctx->dst[buf_ctx->cur] = static_cast<char>(c);
    }
    ++buf_ctx->cur;
}

// Strong types to prevent parameter confusion
enum class NumberBase : uint_fast8_t { BINARY = 2, OCTAL = 8, DECIMAL = 10, HEXADECIMAL = 16 };
enum class CaseAdjustment : std::uint8_t { LOWERCASE = 'a' - 'A', UPPERCASE = 0 };

// Unsigned to ASCII (reversed)
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

// Parse flags helper
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

// Parse field width helper
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

// Parse precision helper
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

// Parse length modifier helper
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

// Parse Conversion specifier helper
template <typename Config>
const char* parse_conversion_specifier( 
    const char* cur,
    FormatSpec<Config>& spec) {
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

// Parse format specification
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

// Floating point support
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

    // Convert double to integer representation
    [[gnu::always_inline]]
    static double_bin_t double_to_int_rep(double f) {
        double_bin_t bin;
        std::memcpy(&bin, &f, sizeof(f));
        return bin;
    }

    // Extract and handle special values
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

    // Process integer part of float
    static ftoa_dec_t process_integer_part( 
        char* buf,
        ftoa_dec_t dec,
        double_bin_t& bin,
        ftoa_exp_t& exp,
        uint_fast8_t& carry) {
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

    // Process fractional part of float
    static bool process_fractional_part(
        char* buf,
        ftoa_dec_t prec_val,
        double_bin_t bin,
        uint_fast8_t& carry,
        ftoa_exp_t exp,
        ftoa_dec_t& dec) {
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

    // Float to ASCII (reversed)
    static int
    ftoa_rev(char* buf, const FormatSpec<Config>& spec, double f) { 
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

// Handle integer conversions
template <typename Config>
int handle_integer_conversion( 
    const FormatSpec<Config>& spec,
    va_list& args,
    char* buf,
    int& slen) {
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

// Handle float conversions
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

// Apply field width and padding
template <typename Config>
void apply_field_width( 
    const FormatSpec<Config>& spec,
    detail::PutcFunc pc,
    void* pc_ctx,
    const char* s,
    int slen,
    int& n) {
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

// Main formatting engine
namespace detail {
template <typename Config>
[[gnu::noinline]]
int vpprintf_impl(PutcFunc pc, void* pc_ctx, const char* format, va_list args) {
    int n = 0;
    FormatSpec<Config> spec;

    while (*format) {
        if (*format != '%') {
            pc(*format++, pc_ctx);
            ++n;
            continue;
        }

        // Parse format specifier
        int const fs_len = parse_format_spec<Config>(format, spec);
        if (fs_len == 0) {
            pc(*format++, pc_ctx);
            ++n;
            continue;
        }

        format += fs_len;

        // Process based on Conversion type
        std::array<char, conversion_buffer_size> buf_array;
        char* buf = buf_array.data();
        const char* s = nullptr;
        int slen = 0;

        switch (spec.conv) {
        case Conversion::PERCENT:
            pc('%', pc_ctx);
            ++n;
            continue;

        case Conversion::CHARACTER: {
            int const c = va_arg(args, int);
            pc(c, pc_ctx);
            ++n;
            continue;
        }

        case Conversion::STRING: {
            s = va_arg(args, const char*);
            if (s == nullptr) {
                s = "(null)";
            }
            slen = static_cast<int>(std::strlen(s));

            // Apply precision for strings
            if constexpr (Config::use_precision) {
                if (spec.precision.opt == FormatOption::LITERAL && spec.precision.value >= 0) {
                    slen = min(slen, spec.precision.value);
                }
            }
            break;
        }

        case Conversion::SIGNED_INT:
        case Conversion::UNSIGNED_INT:
        case Conversion::OCTAL:
        case Conversion::HEX:
        case Conversion::BINARY:
        case Conversion::POINTER: {
            handle_integer_conversion<Config>(spec, args, buf, slen);
            s = buf;
            break;
        }

        case Conversion::FLOAT_DEC:
        case Conversion::FLOAT_SCI:
        case Conversion::FLOAT_SHORTEST:
        case Conversion::FLOAT_HEX:
            handle_float_conversion<Config>(spec, args, buf, slen);
            s = buf;
            break;

        case Conversion::WRITEBACK:
            if constexpr (Config::use_writeback) {
                int* p = va_arg(args, int*);
                if (p != nullptr) {
                    *p = n;
                }
            }
            continue;

        case Conversion::NONE:
        default:
            // Unsupported Conversion
            continue;
        }

        // Apply field width and padding
        apply_field_width<Config>(spec, pc, pc_ctx, s, slen, n);
    }

    return n;
}
} // namespace detail

// Public API implementations
// (keep templated versions for custom Config, but provide rt::printf/rt::fprintf/rt::snprintf
// aliases)
template <typename Config>
int vsnprintf(char* buffer, std::size_t bufsz, const char* format, va_list args) {
    detail::BufferContext ctx{.dst = buffer, .len = bufsz, .cur = 0};
    int n = detail::vpprintf_impl<Config>(bufputc, &ctx, format, args);
    if (ctx.len > 0) {
        std::size_t const terminator_idx = min(ctx.cur, ctx.len - 1);
        ctx.dst[terminator_idx] = '\0';
    }
    return n;
}

template <typename Config>
int snprintf(char* buffer, std::size_t bufsz, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf<Config>(buffer, bufsz, format, args);
    va_end(args);
    return n;
}

// Remove public callback-based printf/vprintf/pprintf; keep only stdout version and snprintf family

inline int vprintf(const char* format, va_list args) {
    auto pc = [](int c, void*) {
        char ch = static_cast<char>(c);
        ::write(1, &ch, 1);
    };
    return detail::vpprintf_impl<DefaultConfig>(pc, nullptr, format, args);
}

inline int printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int const n = vprintf(format, args);
    va_end(args);
    return n;
}

} // namespace rt
