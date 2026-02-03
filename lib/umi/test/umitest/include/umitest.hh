// SPDX-License-Identifier: MIT
// umitest — Zero-macro lightweight test framework for C++23
#pragma once

#include <cmath>
#include <cstdio>
#include <source_location>
#include <type_traits>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"

namespace umitest {

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
    if constexpr (std::is_same_v<T, bool>)
        std::snprintf(buf, size, "%s", v ? "true" : "false");
    else if constexpr (std::is_same_v<T, char>)
        std::snprintf(buf, size, "'%c' (%d)", v, static_cast<int>(v));
    else if constexpr (std::is_unsigned_v<T>)
        std::snprintf(buf, size, "%llu", static_cast<unsigned long long>(v));
    else if constexpr (std::is_integral_v<T>)
        std::snprintf(buf, size, "%lld", static_cast<long long>(v));
    else if constexpr (std::is_floating_point_v<T>)
        std::snprintf(buf, size, "%.6g", static_cast<double>(v));
    else if constexpr (std::is_enum_v<T>)
        std::snprintf(buf, size, "%lld", static_cast<long long>(static_cast<std::underlying_type_t<T>>(v)));
    else if constexpr (std::is_pointer_v<T>)
        std::snprintf(buf, size, "%p", static_cast<const void*>(v));
    else
        std::snprintf(buf, size, "(?)");
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
    bool failed = false;

    explicit TestContext(Suite& s) : suite(s) {}

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
    Suite& suite;
};

// =============================================================================
// Suite — test runner and statistics
// =============================================================================

class Suite {
  public:
    explicit Suite(const char* name) : name(name) {}

    // -- Section --

    void section(const char* title) { std::printf("\n%s[%s]%s\n", cyan, title, reset); }

    // -- run() : structured test with TestContext --

    template <typename F>
    void run(const char* test_name, F&& fn) {
        TestContext ctx(*this);
        bool returned_ok = fn(ctx);
        if (!returned_ok || ctx.failed) {
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
        char va[64], vb[64];
        format_value(va, sizeof(va), a);
        format_value(vb, sizeof(vb), b);
        std::printf("  %sFAIL: got %s, expected %s (eps=%.6g)%s\n    at %s:%u\n",
                    red,
                    va,
                    vb,
                    eps,
                    reset,
                    loc.file_name(),
                    static_cast<unsigned>(loc.line()));
        failed++;
        return false;
    }

    // -- Summary --

    int summary() {
        int total = passed + failed;
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

    void record_fail(std::source_location loc, const char* msg = nullptr) {
        if (msg)
            std::printf(
                "  %sFAIL: %s%s\n    at %s:%u\n", red, msg, reset, loc.file_name(), static_cast<unsigned>(loc.line()));
        else
            std::printf("  %sFAIL%s at %s:%u\n", red, reset, loc.file_name(), static_cast<unsigned>(loc.line()));
    }

    template <typename A, typename B>
    void record_fail_cmp(const A& a, const char* op, const B& b, std::source_location loc) {
        char va[64], vb[64];
        format_value(va, sizeof(va), a);
        format_value(vb, sizeof(vb), b);
        std::printf("  %sFAIL: %s %s %s (got %s, expected %s)%s\n    at %s:%u\n",
                    red,
                    va,
                    op,
                    vb,
                    va,
                    vb,
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
        suite.record_fail(loc, msg);
        failed = true;
    }
    return cond;
}

template <typename A, typename B>
bool TestContext::assert_eq(const A& a, const B& b, std::source_location loc) {
    if (a == b)
        return true;
    suite.record_fail_cmp(a, "==", b, loc);
    failed = true;
    return false;
}

template <typename A, typename B>
bool TestContext::assert_ne(const A& a, const B& b, std::source_location loc) {
    if (a != b)
        return true;
    suite.record_fail_cmp(a, "!=", b, loc);
    failed = true;
    return false;
}

template <typename A, typename B>
bool TestContext::assert_lt(const A& a, const B& b, std::source_location loc) {
    if (a < b)
        return true;
    suite.record_fail_cmp(a, "<", b, loc);
    failed = true;
    return false;
}

template <typename A, typename B>
bool TestContext::assert_le(const A& a, const B& b, std::source_location loc) {
    if (a <= b)
        return true;
    suite.record_fail_cmp(a, "<=", b, loc);
    failed = true;
    return false;
}

template <typename A, typename B>
bool TestContext::assert_gt(const A& a, const B& b, std::source_location loc) {
    if (a > b)
        return true;
    suite.record_fail_cmp(a, ">", b, loc);
    failed = true;
    return false;
}

template <typename A, typename B>
bool TestContext::assert_ge(const A& a, const B& b, std::source_location loc) {
    if (a >= b)
        return true;
    suite.record_fail_cmp(a, ">=", b, loc);
    failed = true;
    return false;
}

template <typename A, typename B>
bool TestContext::assert_near(const A& a, const B& b, double eps, std::source_location loc) {
    if (std::abs(static_cast<double>(a) - static_cast<double>(b)) < eps)
        return true;
    char va[64], vb[64];
    format_value(va, sizeof(va), a);
    format_value(vb, sizeof(vb), b);
    std::printf("  %sFAIL: got %s, expected %s (eps=%.6g)%s\n    at %s:%u\n",
                red,
                va,
                vb,
                eps,
                reset,
                loc.file_name(),
                static_cast<unsigned>(loc.line()));
    failed = true;
    return false;
}

} // namespace umitest

#pragma GCC diagnostic pop
