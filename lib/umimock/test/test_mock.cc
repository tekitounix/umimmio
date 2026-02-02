// SPDX-License-Identifier: MIT
// umimock unit tests

#include <umimock/mock.hh>

#include <umitest.hh>

using namespace umi::mock;
using namespace umitest;

// ============================================================================
// Concept verification
// ============================================================================

static void test_concept(Suite& s) {
    s.section("Generatable concept");

    // MockSignal satisfies Generatable
    static_assert(Generatable<MockSignal>, "MockSignal must satisfy Generatable");

    // int does not satisfy Generatable
    static_assert(!Generatable<int>, "int must not satisfy Generatable");

    s.check(true, "static_assert passed");
}

// ============================================================================
// MockSignal basics
// ============================================================================

static void test_constant(Suite& s) {
    s.section("Constant signal");

    MockSignal sig(Shape::CONSTANT, 0.5f);

    s.check_eq(static_cast<int>(sig.get_shape()), static_cast<int>(Shape::CONSTANT));
    s.check_near(sig.get_value(), 0.5f);
    s.check_near(sig.generate(), 0.5f);
    s.check_near(sig.generate(), 0.5f);
}

static void test_ramp(Suite& s) {
    s.section("Ramp signal");

    MockSignal sig(Shape::RAMP, 1.0f);

    float first = sig.generate();
    float second = sig.generate();
    s.check(second > first, "ramp increases over time");
    s.check_near(first, 0.01f);
}

// ============================================================================
// set_value with this-> disambiguation
// ============================================================================

static void test_set_value(Suite& s) {
    s.section("set_value (this-> disambiguation)");

    MockSignal sig;
    s.check_near(sig.get_value(), default_value);

    sig.set_value(0.75f);
    s.check_near(sig.get_value(), 0.75f);
    s.check_near(sig.generate(), 0.75f);
}

// ============================================================================
// Reset
// ============================================================================

static void test_reset(Suite& s) {
    s.section("reset");

    MockSignal sig(Shape::RAMP, 1.0f);
    sig.generate();
    sig.generate();
    sig.reset();

    s.check_near(sig.get_value(), default_value);
}

// ============================================================================
// fill_buffer (concept-constrained template)
// ============================================================================

static void test_fill_buffer(Suite& s) {
    s.section("fill_buffer");

    MockSignal sig(Shape::CONSTANT, 0.25f);
    float buf[4] = {};
    fill_buffer(sig, buf, 4);

    for (int i = 0; i < 4; ++i) {
        s.check_near(buf[i], 0.25f);
    }
}

// ============================================================================
// constexpr verification
// ============================================================================

static void test_constexpr(Suite& s) {
    s.section("constexpr");

    constexpr MockSignal sig(Shape::CONSTANT, 1.0f);
    s.check(sig.get_value() == 1.0f, "constexpr construction");
    s.check(default_value == 0.0f, "constexpr default_value");
    s.check(max_ramp_steps == 100, "constexpr max_ramp_steps");
}

// ============================================================================
// detail namespace
// ============================================================================

static void test_detail(Suite& s) {
    s.section("detail::clamp01");

    s.check_near(detail::clamp01(-1.0f), 0.0f);
    s.check_near(detail::clamp01(0.5f), 0.5f);
    s.check_near(detail::clamp01(2.0f), 1.0f);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    Suite s("umimock");
    test_concept(s);
    test_constant(s);
    test_ramp(s);
    test_set_value(s);
    test_reset(s);
    test_fill_buffer(s);
    test_constexpr(s);
    test_detail(s);

    return s.summary();
}
