// SPDX-License-Identifier: MIT
// umitest self-test — the framework tests itself

#include <umitest.hh>

using namespace umitest;

// =============================================================================
// Structured tests (run with TestContext)
// =============================================================================

bool test_assert_true(TestContext& t) {
    t.assert_true(true);
    t.assert_true(1 == 1);
    t.assert_true(42 > 0, "positive number");
    return true;
}

bool test_assert_eq(TestContext& t) {
    t.assert_eq(1, 1);
    t.assert_eq(0u, 0u);
    t.assert_eq('a', 'a');
    t.assert_eq(-1, -1);
    return true;
}

bool test_assert_ne(TestContext& t) {
    t.assert_ne(1, 2);
    t.assert_ne('a', 'b');
    return true;
}

bool test_assert_comparisons(TestContext& t) {
    t.assert_lt(1, 2);
    t.assert_le(1, 1);
    t.assert_le(1, 2);
    t.assert_gt(2, 1);
    t.assert_ge(2, 2);
    t.assert_ge(3, 2);
    return true;
}

bool test_assert_near(TestContext& t) {
    t.assert_near(1.0f, 1.0001f);
    t.assert_near(0.0, 0.0);
    t.assert_near(3.14, 3.14159, 0.01);
    return true;
}

bool test_early_return(TestContext& t) {
    if (!t.assert_eq(1, 1))
        return false;
    if (!t.assert_lt(0, 1))
        return false;
    return true;
}

bool test_lambda(TestContext&) {
    return true;
}

// =============================================================================
// Enum formatting
// =============================================================================

enum class Color { RED, GREEN = 42, BLUE = 255 };

bool test_enum_format(TestContext& t) {
    t.assert_eq(Color::RED, Color::RED);
    t.assert_ne(Color::RED, Color::GREEN);
    return true;
}

// =============================================================================
// Main
// =============================================================================

int main() {
    Suite s("umitest");

    s.section("assert_true");
    s.run("basic conditions", test_assert_true);

    s.section("assert_eq / assert_ne");
    s.run("equality", test_assert_eq);
    s.run("inequality", test_assert_ne);

    s.section("Comparisons");
    s.run("lt/le/gt/ge", test_assert_comparisons);

    s.section("assert_near");
    s.run("floating point", test_assert_near);

    s.section("Early return pattern");
    s.run("chained asserts", test_early_return);

    s.section("Lambda");
    s.run("empty lambda", test_lambda);
    s.run("inline lambda", [](TestContext& t) {
        t.assert_eq(2 + 2, 4);
        return true;
    });

    s.section("Enum");
    s.run("enum comparison", test_enum_format);

    s.section("Inline checks");
    s.check(true);
    s.check(1 > 0, "one is positive");
    s.check_eq(10, 10);
    s.check_ne(1, 2);
    s.check_lt(1, 2);
    s.check_le(2, 2);
    s.check_gt(3, 2);
    s.check_ge(3, 3);
    s.check_near(1.0f, 1.0001f);

    return s.summary();
}
