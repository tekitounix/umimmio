// SPDX-License-Identifier: MIT
// umitest — Zero-macro lightweight test framework for C++23
#pragma once

#include <array>
#include <cmath>
#include <cstdio>
#include <source_location>
#include <type_traits>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"

namespace umi::test {

// =============================================================================
// ANSI colors
// =============================================================================

#ifndef UMI_TEST_NO_COLOR
constexpr const char* green = "\033[32m";
constexpr const char* red = "\033[31m";
constexpr const char* cyan = "\033[36m";
constexpr const char* reset = "\033[0m";
#else
constexpr const char* green = "";
constexpr const char* red = "";
constexpr const char* cyan = "";
constexpr const char* reset = "";
#endif

// =============================================================================
// Value formatting (snprintf-based, no <iostream>)
// =============================================================================

template <typename T>
void format_value(char* buf, std::size_t size, const T& v) {
    if constexpr (std::is_same_v<T, bool>) {
        std::snprintf(buf, size, "%s", v ? "true" : "false");
    } else if constexpr (std::is_same_v<T, char>) {
        std::snprintf(buf, size, "'%c' (%d)", v, static_cast<int>(v));
    } else if constexpr (std::is_unsigned_v<T>) {
        std::snprintf(buf, size, "%llu", static_cast<unsigned long long>(v));
    } else if constexpr (std::is_integral_v<T>) {
        std::snprintf(buf, size, "%lld", static_cast<long long>(v));
    } else if constexpr (std::is_floating_point_v<T>) {
        std::snprintf(buf, size, "%.6g", static_cast<double>(v));
    } else if constexpr (std::is_enum_v<T>) {
        std::snprintf(buf, size, "%lld", static_cast<long long>(static_cast<std::underlying_type_t<T>>(v)));
    } else if constexpr (std::is_pointer_v<T>) {
        std::snprintf(buf, size, "%p", static_cast<const void*>(v));
    } else {
        std::snprintf(buf, size, "(?)");
    }
}

// =============================================================================
// Forward declaration
// =============================================================================

class Suite;

// =============================================================================
// TestContext — used inside run() test functions
// =============================================================================

class TestContext {
  public:
    explicit TestContext(Suite& s) : suite(s) {}

    [[nodiscard]] bool has_failed() const { return failed; }
    void clear_failed() { failed = false; }

    bool assert_true(bool cond, const char* msg = nullptr, std::source_location loc = std::source_location::current());

    template <typename A, typename B>
    bool assert_eq(const A& a, const B& b, std::source_location loc = std::source_location::current());

    template <typename A, typename B>
    bool assert_ne(const A& a, const B& b, std::source_location loc = std::source_location::current());

    template <typename A, typename B>
    bool assert_lt(const A& a, const B& b, std::source_location loc = std::source_location::current());

    template <typename A, typename B>
    bool assert_le(const A& a, const B& b, std::source_location loc = std::source_location::current());

    template <typename A, typename B>
    bool assert_gt(const A& a, const B& b, std::source_location loc = std::source_location::current());

    template <typename A, typename B>
    bool assert_ge(const A& a, const B& b, std::source_location loc = std::source_location::current());

    template <typename A, typename B>
    bool
    assert_near(const A& a, const B& b, double eps = 0.001, std::source_location loc = std::source_location::current());

  private:
    void mark_failed() { failed = true; }

    Suite& suite;
    bool failed = false;
};

// =============================================================================
// Suite — test runner and statistics
// =============================================================================

class Suite {
  public:
    explicit Suite(const char* name) : name(name) {}

    // -- Section --

    static void section(const char* title) { std::printf("\n%s[%s]%s\n", cyan, title, reset); }

    // -- run() : structured test with TestContext --

    template <typename F>
    void run(const char* test_name, F&& fn) {
        TestContext ctx(*this);
        ctx.clear_failed();
        const bool returned_ok = fn(ctx);
        if (!returned_ok || ctx.has_failed()) {
            std::printf("  %s... %sFAIL%s\n", test_name, red, reset);
            failed++;
        } else {
            std::printf("  %s... %sOK%s\n", test_name, green, reset);
            passed++;
        }
    }

    // -- check() : inline checks (no TestContext needed) --

    bool check(bool cond, const char* msg = nullptr, std::source_location loc = std::source_location::current()) {
        if (cond) {
            passed++;
            return true;
        }
        record_fail(loc, msg);
        failed++;
        return false;
    }

    template <typename A, typename B>
    bool check_eq(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        if (a == b) {
            passed++;
            return true;
        }
        record_fail_cmp(a, "==", b, loc);
        failed++;
        return false;
    }

    template <typename A, typename B>
    bool check_ne(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        if (a != b) {
            passed++;
            return true;
        }
        record_fail_cmp(a, "!=", b, loc);
        failed++;
        return false;
    }

    template <typename A, typename B>
    bool check_lt(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        if (a < b) {
            passed++;
            return true;
        }
        record_fail_cmp(a, "<", b, loc);
        failed++;
        return false;
    }

    template <typename A, typename B>
    bool check_le(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        if (a <= b) {
            passed++;
            return true;
        }
        record_fail_cmp(a, "<=", b, loc);
        failed++;
        return false;
    }

    template <typename A, typename B>
    bool check_gt(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        if (a > b) {
            passed++;
            return true;
        }
        record_fail_cmp(a, ">", b, loc);
        failed++;
        return false;
    }

    template <typename A, typename B>
    bool check_ge(const A& a, const B& b, std::source_location loc = std::source_location::current()) {
        if (a >= b) {
            passed++;
            return true;
        }
        record_fail_cmp(a, ">=", b, loc);
        failed++;
        return false;
    }

    template <typename A, typename B>
    bool
    check_near(const A& a, const B& b, double eps = 0.001, std::source_location loc = std::source_location::current()) {
        if (std::abs(static_cast<double>(a) - static_cast<double>(b)) < eps) {
            passed++;
            return true;
        }
        std::array<char, 64> va{};
        std::array<char, 64> vb{};
        format_value(va.data(), va.size(), a);
        format_value(vb.data(), vb.size(), b);
        std::printf("  %sFAIL: got %s, expected %s (eps=%.6g)%s\n    at %s:%u\n",
                    red,
                    va.data(),
                    vb.data(),
                    eps,
                    reset,
                    loc.file_name(),
                    static_cast<unsigned>(loc.line()));
        failed++;
        return false;
    }

    // -- Summary --

    int summary() {
        const int total = passed + failed;
        std::printf("\n%s=================================%s\n", cyan, reset);
        if (failed == 0) {
            std::printf("%s%s: %d/%d passed%s\n", green, name, passed, total, reset);
        } else {
            std::printf("%s%s: %d/%d passed, %d FAILED%s\n", red, name, passed, total, failed, reset);
        }
        std::printf("%s=================================%s\n", cyan, reset);
        return failed > 0 ? 1 : 0;
    }

    // -- Recording (used by TestContext) --

    static void record_fail(std::source_location loc, const char* msg = nullptr) {
        if (msg != nullptr) {
            std::printf(
                "  %sFAIL: %s%s\n    at %s:%u\n", red, msg, reset, loc.file_name(), static_cast<unsigned>(loc.line()));
        } else {
            std::printf("  %sFAIL%s at %s:%u\n", red, reset, loc.file_name(), static_cast<unsigned>(loc.line()));
        }
    }

    template <typename A, typename B>
    void record_fail_cmp(const A& a, const char* op, const B& b, std::source_location loc) {
        std::array<char, 64> va{};
        std::array<char, 64> vb{};
        format_value(va.data(), va.size(), a);
        format_value(vb.data(), vb.size(), b);
        std::printf("  %sFAIL: %s %s %s (got %s, expected %s)%s\n    at %s:%u\n",
                    red,
                    va.data(),
                    op,
                    vb.data(),
                    va.data(),
                    vb.data(),
                    reset,
                    loc.file_name(),
                    static_cast<unsigned>(loc.line()));
    }

  private:
    const char* name;
    int passed = 0;
    int failed = 0;
};

// =============================================================================
// TestContext implementation (needs Suite to be complete)
// =============================================================================

inline bool TestContext::assert_true(bool cond, const char* msg, std::source_location loc) {
    if (!cond) {
        Suite::record_fail(loc, msg);
        mark_failed();
    }
    return cond;
}

template <typename A, typename B>
bool TestContext::assert_eq(const A& a, const B& b, std::source_location loc) {
    if (a == b) {
        return true;
    }
    suite.record_fail_cmp(a, "==", b, loc);
    mark_failed();
    return false;
}

template <typename A, typename B>
bool TestContext::assert_ne(const A& a, const B& b, std::source_location loc) {
    if (a != b) {
        return true;
    }
    suite.record_fail_cmp(a, "!=", b, loc);
    mark_failed();
    return false;
}

template <typename A, typename B>
bool TestContext::assert_lt(const A& a, const B& b, std::source_location loc) {
    if (a < b) {
        return true;
    }
    suite.record_fail_cmp(a, "<", b, loc);
    mark_failed();
    return false;
}

template <typename A, typename B>
bool TestContext::assert_le(const A& a, const B& b, std::source_location loc) {
    if (a <= b) {
        return true;
    }
    suite.record_fail_cmp(a, "<=", b, loc);
    mark_failed();
    return false;
}

template <typename A, typename B>
bool TestContext::assert_gt(const A& a, const B& b, std::source_location loc) {
    if (a > b) {
        return true;
    }
    suite.record_fail_cmp(a, ">", b, loc);
    mark_failed();
    return false;
}

template <typename A, typename B>
bool TestContext::assert_ge(const A& a, const B& b, std::source_location loc) {
    if (a >= b) {
        return true;
    }
    suite.record_fail_cmp(a, ">=", b, loc);
    mark_failed();
    return false;
}

template <typename A, typename B>
bool TestContext::assert_near(const A& a, const B& b, double eps, std::source_location loc) {
    if (std::abs(static_cast<double>(a) - static_cast<double>(b)) < eps) {
        return true;
    }
    std::array<char, 64> va{};
    std::array<char, 64> vb{};
    format_value(va.data(), va.size(), a);
    format_value(vb.data(), vb.size(), b);
    std::printf("  %sFAIL: got %s, expected %s (eps=%.6g)%s\n    at %s:%u\n",
                red,
                va.data(),
                vb.data(),
                eps,
                reset,
                loc.file_name(),
                static_cast<unsigned>(loc.line()));
    mark_failed();
    return false;
}

} // namespace umi::test

#pragma GCC diagnostic pop
