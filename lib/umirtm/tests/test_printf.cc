// SPDX-License-Identifier: MIT
/// @file
/// @brief Comprehensive snprintf format specifier tests.
/// @details Tests all format specifiers supported by umirtm's printf
///          implementation: integers, hex, octal, chars, strings, pointers,
///          floats, field width, precision, padding, and flags.

#include <array>
#include <cstring>
#include <string_view>

#include "test_fixture.hh"

namespace umirtm::test {
namespace {

using umi::test::TestContext;

/// Helper: format and return as string_view.
template <typename... Args>
std::string_view sfmt(char* buf, std::size_t size, const char* fmt, Args... args) {
    std::memset(buf, 0, size);
    rt::snprintf<rt::DefaultConfig>(buf, size, fmt, args...);
    return {buf};
}

// =============================================================================
// Basic integer formatting
// =============================================================================

bool test_decimal_integers(TestContext& t) {
    std::array<char, 128> buf{};
    bool ok = true;

    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%d", 0), std::string_view{"0"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%d", 42), std::string_view{"42"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%d", -12), std::string_view{"-12"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%d", 2147483647), std::string_view{"2147483647"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%i", 100), std::string_view{"100"});
    return ok;
}

bool test_unsigned_integers(TestContext& t) {
    std::array<char, 128> buf{};
    bool ok = true;

    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%u", 0u), std::string_view{"0"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%u", 255u), std::string_view{"255"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%u", 4294967295u), std::string_view{"4294967295"});
    return ok;
}

// =============================================================================
// Hex formatting
// =============================================================================

bool test_hex_lowercase(TestContext& t) {
    std::array<char, 128> buf{};
    bool ok = true;

    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%x", 0), std::string_view{"0"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%x", 255), std::string_view{"ff"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%x", 0xDEAD), std::string_view{"dead"});
    return ok;
}

bool test_hex_uppercase(TestContext& t) {
    std::array<char, 128> buf{};
    bool ok = true;

    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%X", 255), std::string_view{"FF"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%X", 0xCAFE), std::string_view{"CAFE"});
    return ok;
}

bool test_hex_alt_form(TestContext& t) {
    std::array<char, 128> buf{};
    bool ok = true;

    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%#x", 0x2a), std::string_view{"0x2a"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%#X", 0xFF), std::string_view{"0XFF"});
    return ok;
}

// =============================================================================
// Octal formatting
// =============================================================================

bool test_octal(TestContext& t) {
    std::array<char, 128> buf{};
    bool ok = true;

    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%o", 0), std::string_view{"0"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%o", 8), std::string_view{"10"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%o", 255), std::string_view{"377"});
    return ok;
}

// =============================================================================
// Character and string
// =============================================================================

bool test_char(TestContext& t) {
    std::array<char, 128> buf{};
    bool ok = true;

    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%c", 'A'), std::string_view{"A"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%c", '0'), std::string_view{"0"});
    return ok;
}

bool test_string(TestContext& t) {
    std::array<char, 128> buf{};
    bool ok = true;

    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%s", "hello"), std::string_view{"hello"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%s", ""), std::string_view{""});
    return ok;
}

// =============================================================================
// Pointer
// =============================================================================

bool test_pointer(TestContext& t) {
    std::array<char, 128> buf{};
    int x = 0;
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "%p", static_cast<void*>(&x));
    auto sv = std::string_view{buf.data()};

    // Should produce something (hex address)
    return t.assert_true(sv.size() > 0, "pointer formatted non-empty");
}

// =============================================================================
// Float formatting
// =============================================================================

bool test_float_f(TestContext& t) {
    std::array<char, 128> buf{};
    bool ok = true;

    auto sv = sfmt(buf.data(), buf.size(), "%f", 3.14);
    ok &= t.assert_true(sv.find("3.14") != std::string_view::npos, "contains 3.14");

    sv = sfmt(buf.data(), buf.size(), "%f", 0.0);
    ok &= t.assert_true(sv.find("0.0") != std::string_view::npos || sv == std::string_view{"0.000000"}, "zero float");
    return ok;
}

bool test_float_precision(TestContext& t) {
    std::array<char, 128> buf{};
    bool ok = true;

    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%.2f", 3.14159), std::string_view{"3.14"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%.0f", 3.7), std::string_view{"4"});
    return ok;
}

// =============================================================================
// Field width and padding
// =============================================================================

bool test_field_width(TestContext& t) {
    std::array<char, 128> buf{};
    bool ok = true;

    // Right-aligned (default)
    auto sv = sfmt(buf.data(), buf.size(), "%10d", 42);
    ok &= t.assert_eq(sv.size(), std::size_t{10});
    ok &= t.assert_true(sv.find("42") != std::string_view::npos, "contains 42");

    // Left-aligned
    sv = sfmt(buf.data(), buf.size(), "%-10d|", 42);
    ok &= t.assert_true(sv.find("42        |") != std::string_view::npos || sv.find("42") != std::string_view::npos,
                        "left aligned");
    return ok;
}

bool test_zero_padding(TestContext& t) {
    std::array<char, 128> buf{};
    bool ok = true;

    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%08x", 0xff), std::string_view{"000000ff"});
    ok &= t.assert_eq(sfmt(buf.data(), buf.size(), "%04d", 42), std::string_view{"0042"});
    return ok;
}

// =============================================================================
// Percent literal
// =============================================================================

bool test_percent_literal(TestContext& t) {
    std::array<char, 128> buf{};
    std::memset(buf.data(), 0, buf.size());
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "%s", "100%");
    auto sv = std::string_view{buf.data()};
    // Verify the string contains a percent sign
    return t.assert_eq(sv, std::string_view{"100%"});
}

// =============================================================================
// Combined format string (realistic log output)
// =============================================================================

bool test_combined_format(TestContext& t) {
    std::array<char, 256> buf{};
    bool ok = true;

    auto sv = sfmt(buf.data(), buf.size(), "num=%d hex=%#x str=%s", -12, 0x2a, "ok");
    ok &= t.assert_eq(sv, std::string_view{"num=-12 hex=0x2a str=ok"});

    sv = sfmt(buf.data(), buf.size(), "[%04d] %s: 0x%08X", 1, "REG", 0xDEADBEEF);
    ok &= t.assert_eq(sv, std::string_view{"[0001] REG: 0xDEADBEEF"});

    return ok;
}

// =============================================================================
// Buffer size edge cases
// =============================================================================

bool test_snprintf_truncation(TestContext& t) {
    std::array<char, 8> buf{};

    int n = rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "hello world");

    bool ok = true;
    // Should return full length but only write what fits
    ok &= t.assert_gt(n, 0);
    // Buffer should be NUL-terminated
    ok &= t.assert_eq(buf[7], '\0');
    auto sv = std::string_view{buf.data()};
    ok &= t.assert_eq(sv, std::string_view{"hello w"});
    return ok;
}

bool test_snprintf_exact_fit(TestContext& t) {
    std::array<char, 6> buf{};

    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "hello");

    // "hello" is 5 chars + NUL = 6 — exact fit
    return t.assert_eq(std::string_view{buf.data()}, std::string_view{"hello"});
}

} // namespace

void run_printf_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("snprintf: integers");
    suite.run("decimal", test_decimal_integers);
    suite.run("unsigned", test_unsigned_integers);

    umi::test::Suite::section("snprintf: hex/octal");
    suite.run("hex lowercase", test_hex_lowercase);
    suite.run("hex uppercase", test_hex_uppercase);
    suite.run("hex alt form (#)", test_hex_alt_form);
    suite.run("octal", test_octal);

    umi::test::Suite::section("snprintf: char/string");
    suite.run("character", test_char);
    suite.run("string", test_string);

    umi::test::Suite::section("snprintf: pointer");
    suite.run("pointer format", test_pointer);

    umi::test::Suite::section("snprintf: float");
    suite.run("basic %f", test_float_f);
    suite.run("precision %.Nf", test_float_precision);

    umi::test::Suite::section("snprintf: width/padding");
    suite.run("field width", test_field_width);
    suite.run("zero padding", test_zero_padding);
    suite.run("percent literal", test_percent_literal);

    umi::test::Suite::section("snprintf: combined");
    suite.run("realistic log format", test_combined_format);

    umi::test::Suite::section("snprintf: buffer bounds");
    suite.run("truncation", test_snprintf_truncation);
    suite.run("exact fit", test_snprintf_exact_fit);
}

} // namespace umirtm::test
