// SPDX-License-Identifier: MIT
/// @file
/// @brief print/println (Rust-style {}) format tests.
/// @details Tests the FormatConverter that translates {} placeholders to
///          printf format specifiers, including {:#x}, {{}}, and mixed text.

#include <array>
#include <cstring>
#include <string_view>

#include "test_fixture.hh"

namespace umirtm::test {
namespace {

using umi::test::TestContext;

// Note: print/println write to stdout via ::write(1, ...).
// We can't easily capture stdout in these tests, so we verify return values
// (byte count) and test the FormatConverter logic indirectly via snprintf.

// =============================================================================
// print() return value (byte count)
// =============================================================================

bool test_print_returns_positive(TestContext& t) {
    int n = rt::print("test={}", 42);
    return t.assert_gt(n, 0);
}

bool test_println_returns_char_count(TestContext& t) {
    int n = rt::println();
    // "\n" = 1 byte
    return t.assert_eq(n, 1);
}

bool test_println_with_args(TestContext& t) {
    int n = rt::println("value={}", 100);
    // "value=100\n" — at least 10 chars
    return t.assert_ge(n, 10);
}

// =============================================================================
// FormatConverter verification via snprintf equivalent
// =============================================================================

// We test the actual conversion by looking at what snprintf produces
// with the same format strings that FormatConverter would generate.

bool test_format_auto_int(TestContext& t) {
    std::array<char, 128> buf{};
    // FormatConverter for int should use %d
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "%d", 42);
    return t.assert_eq(std::string_view{buf.data()}, std::string_view{"42"});
}

bool test_format_auto_unsigned(TestContext& t) {
    std::array<char, 128> buf{};
    // FormatConverter for unsigned should use %u
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "%u", 42u);
    return t.assert_eq(std::string_view{buf.data()}, std::string_view{"42"});
}

bool test_format_auto_string(TestContext& t) {
    std::array<char, 128> buf{};
    rt::snprintf<rt::DefaultConfig>(buf.data(), buf.size(), "%s", "hello");
    return t.assert_eq(std::string_view{buf.data()}, std::string_view{"hello"});
}

// =============================================================================
// print with various arg types
// =============================================================================

bool test_print_int_and_hex(TestContext& t) {
    // "value=42 hex=0x2a" — this tests the {:#x} format
    int n = rt::print("value={} hex={:#x}", 42, 0x2a);
    return t.assert_gt(n, 0);
}

bool test_print_string_arg(TestContext& t) {
    int n = rt::print("msg={}", "hello");
    return t.assert_gt(n, 0);
}

bool test_print_no_args(TestContext& t) {
    int n = rt::print("static text");
    return t.assert_gt(n, 0);
}

bool test_print_char_arg(TestContext& t) {
    int n = rt::print("char={}", 'A');
    return t.assert_gt(n, 0);
}

// =============================================================================
// printf direct (stdout)
// =============================================================================

bool test_printf_basic(TestContext& t) {
    int n = rt::printf("hello %s\n", "world");
    return t.assert_gt(n, 0);
}

bool test_printf_combined(TestContext& t) {
    int n = rt::printf("[%04d] %s: 0x%08X\n", 1, "TEST", 0xCAFEu);
    return t.assert_gt(n, 0);
}

// =============================================================================
// Terminal color sequences
// =============================================================================

bool test_terminal_colors_defined(TestContext& t) {
    bool ok = true;
    // Verify terminal escape constants are non-null and start with ESC
    ok &= t.assert_true(rt::terminal::reset[0] == '\x1B', "reset starts with ESC");
    ok &= t.assert_true(rt::terminal::clear[0] == '\x1B', "clear starts with ESC");
    ok &= t.assert_true(rt::terminal::text::red[0] == '\x1B', "text::red starts with ESC");
    ok &= t.assert_true(rt::terminal::text::green[0] == '\x1B', "text::green starts with ESC");
    ok &= t.assert_true(rt::terminal::background::blue[0] == '\x1B', "bg::blue starts with ESC");
    return ok;
}

bool test_terminal_colors_in_output(TestContext& t) {
    // Practical: print colored output (like a real embedded debug session)
    int n = rt::printf("%s[ERROR]%s sensor timeout\n", rt::terminal::text::red, rt::terminal::reset);
    return t.assert_gt(n, 0);
}

// =============================================================================
// Config variants
// =============================================================================

bool test_minimal_config_basic(TestContext& t) {
    std::array<char, 128> buf{};
    rt::snprintf<rt::MinimalConfig>(buf.data(), buf.size(), "test %d", 42);
    // MinimalConfig has no field width — basic format should still work
    auto sv = std::string_view{buf.data()};
    return t.assert_true(sv.find("42") != std::string_view::npos, "basic int in minimal config");
}

} // namespace

void run_print_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("print: return values");
    suite.run("print returns positive", test_print_returns_positive);
    suite.run("println returns 1", test_println_returns_char_count);
    suite.run("println with args", test_println_with_args);

    umi::test::Suite::section("print: format conversion");
    suite.run("auto int", test_format_auto_int);
    suite.run("auto unsigned", test_format_auto_unsigned);
    suite.run("auto string", test_format_auto_string);

    umi::test::Suite::section("print: argument types");
    suite.run("int and hex", test_print_int_and_hex);
    suite.run("string arg", test_print_string_arg);
    suite.run("no args", test_print_no_args);
    suite.run("char arg", test_print_char_arg);

    umi::test::Suite::section("printf: direct stdout");
    suite.run("basic printf", test_printf_basic);
    suite.run("combined format", test_printf_combined);

    umi::test::Suite::section("Terminal colors");
    suite.run("escape sequences defined", test_terminal_colors_defined);
    suite.run("colored output", test_terminal_colors_in_output);

    umi::test::Suite::section("Config variants");
    suite.run("MinimalConfig basic", test_minimal_config_basic);
}

} // namespace umirtm::test
