// SPDX-License-Identifier: MIT
/// @file
/// @brief Monitor ring buffer lifecycle tests.
/// @details Tests the circular buffer implementation including write, capacity
///          tracking, wrapping, fill/empty conditions, and multi-write sequences.

#include <cstddef>
#include <cstring>
#include <string_view>

#include "test_fixture.hh"

namespace umirtm::test {
namespace {

using umi::test::TestContext;

// Small monitor for focused testing: 1 up, 1 down, 16-byte buffers
using SmallMonitor = rt::Monitor<1, 1, 16, 16, rt::Mode::NoBlockSkip>;

// Medium monitor for wrapping tests: 1 up, 1 down, 32-byte buffers
using MediumMonitor = rt::Monitor<1, 1, 32, 32, rt::Mode::NoBlockSkip>;

// =============================================================================
// Basic write and capacity
// =============================================================================

bool test_initial_state(TestContext& t) {
    SmallMonitor::init("TEST RTM");

    bool ok = true;
    // Buffer is empty: available = 0, free = size - 1 = 15
    ok &= t.assert_eq(SmallMonitor::get_available<0>(), std::size_t{0});
    ok &= t.assert_eq(SmallMonitor::get_free_space<0>(), std::size_t{15});
    return ok;
}

bool test_write_and_available(TestContext& t) {
    SmallMonitor::init("TEST RTM");

    auto written = SmallMonitor::write<0>(std::string_view{"hello"});

    bool ok = true;
    ok &= t.assert_eq(written, std::size_t{5});
    ok &= t.assert_eq(SmallMonitor::get_available<0>(), std::size_t{5});
    ok &= t.assert_eq(SmallMonitor::get_free_space<0>(), std::size_t{10}); // 15 - 5
    return ok;
}

bool test_write_fills_to_capacity(TestContext& t) {
    SmallMonitor::init("TEST RTM");

    // Write nearly full: 15 bytes (capacity = size - 1 = 15)
    auto first = SmallMonitor::write<0>(std::string_view{"123456789012345"});

    bool ok = true;
    ok &= t.assert_eq(first, std::size_t{15});
    ok &= t.assert_eq(SmallMonitor::get_available<0>(), std::size_t{15});
    ok &= t.assert_eq(SmallMonitor::get_free_space<0>(), std::size_t{0});
    return ok;
}

bool test_write_when_full_returns_zero(TestContext& t) {
    SmallMonitor::init("TEST RTM");

    [[maybe_unused]] auto filled = SmallMonitor::write<0>(std::string_view{"123456789012345"});
    auto overflow = SmallMonitor::write<0>(std::string_view{"x"});

    return t.assert_eq(overflow, std::size_t{0});
}

bool test_write_partial_when_nearly_full(TestContext& t) {
    SmallMonitor::init("TEST RTM");

    // Write 10 bytes, leaving 5 free
    [[maybe_unused]] auto w0 = SmallMonitor::write<0>(std::string_view{"0123456789"});

    // Try to write 8 bytes — only 5 should fit
    auto written = SmallMonitor::write<0>(std::string_view{"ABCDEFGH"});

    bool ok = true;
    ok &= t.assert_eq(written, std::size_t{5});
    ok &= t.assert_eq(SmallMonitor::get_free_space<0>(), std::size_t{0});
    return ok;
}

// =============================================================================
// Multiple sequential writes
// =============================================================================

bool test_sequential_writes(TestContext& t) {
    MediumMonitor::init("TEST RTM");

    // capacity = 31 (32 - 1)
    auto w1 = MediumMonitor::write<0>(std::string_view{"Hello"});  // 5
    auto w2 = MediumMonitor::write<0>(std::string_view{", "});     // 2
    auto w3 = MediumMonitor::write<0>(std::string_view{"World!"}); // 6

    bool ok = true;
    ok &= t.assert_eq(w1, std::size_t{5});
    ok &= t.assert_eq(w2, std::size_t{2});
    ok &= t.assert_eq(w3, std::size_t{6});
    ok &= t.assert_eq(MediumMonitor::get_available<0>(), std::size_t{13});  // 5+2+6
    ok &= t.assert_eq(MediumMonitor::get_free_space<0>(), std::size_t{18}); // 31-13
    return ok;
}

// =============================================================================
// log() (fire and forget)
// =============================================================================

bool test_log_does_not_require_check(TestContext& t) {
    SmallMonitor::init("TEST RTM");

    // log() should not require [[nodiscard]] handling
    SmallMonitor::log<0>("test");

    return t.assert_eq(SmallMonitor::get_available<0>(), std::size_t{4});
}

// =============================================================================
// Empty data write
// =============================================================================

bool test_write_empty_string(TestContext& t) {
    SmallMonitor::init("TEST RTM");

    auto written = SmallMonitor::write<0>(std::string_view{""});

    bool ok = true;
    ok &= t.assert_eq(written, std::size_t{0});
    ok &= t.assert_eq(SmallMonitor::get_available<0>(), std::size_t{0});
    return ok;
}

// =============================================================================
// Down buffer (read) operations
// =============================================================================

bool test_read_byte_empty(TestContext& t) {
    SmallMonitor::init("TEST RTM");

    // Reading from empty down buffer should return -1
    auto byte = SmallMonitor::read_byte<0>();
    return t.assert_eq(byte, -1);
}

bool test_read_line_empty(TestContext& t) {
    SmallMonitor::init("TEST RTM");

    char line[64]{};
    auto* result = SmallMonitor::read_line<0>(line, sizeof(line));
    return t.assert_eq(result, static_cast<char*>(nullptr));
}

bool test_read_line_zero_max(TestContext& t) {
    SmallMonitor::init("TEST RTM");

    char line[1]{};
    auto* result = SmallMonitor::read_line<0>(line, 0);
    return t.assert_eq(result, static_cast<char*>(nullptr));
}

// =============================================================================
// Out-of-range buffer index (compile-time bounded)
// =============================================================================

bool test_out_of_range_buffer(TestContext& t) {
    // SmallMonitor has 1 up and 1 down buffer (index 0 only)
    // Index 1 is out of range — should return 0 safely
    bool ok = true;
    ok &= t.assert_eq(SmallMonitor::get_available<1>(), std::size_t{0});
    ok &= t.assert_eq(SmallMonitor::get_free_space<1>(), std::size_t{0});
    ok &= t.assert_eq(SmallMonitor::read_byte<1>(), -1);
    return ok;
}

// =============================================================================
// Practical: logging session simulation
// =============================================================================

bool test_logging_session(TestContext& t) {
    MediumMonitor::init("APP LOG");

    bool ok = true;

    // Simulate a series of log messages (like a real embedded app would do)
    auto w1 = MediumMonitor::write<0>(std::string_view{"[INF] boot\n"});
    ok &= t.assert_eq(w1, std::size_t{11});

    auto w2 = MediumMonitor::write<0>(std::string_view{"[INF] init ok\n"});
    ok &= t.assert_eq(w2, std::size_t{14});

    ok &= t.assert_eq(MediumMonitor::get_available<0>(), std::size_t{25});

    // Try to write a message that won't fully fit
    auto w3 = MediumMonitor::write<0>(std::string_view{"[ERR] overflow test msg\n"});
    // Only 6 bytes free (31 - 25), so partial write
    ok &= t.assert_eq(w3, std::size_t{6});
    ok &= t.assert_eq(MediumMonitor::get_free_space<0>(), std::size_t{0});

    return ok;
}

} // namespace

void run_monitor_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("Monitor: initial state");
    suite.run("empty buffer", test_initial_state);

    umi::test::Suite::section("Monitor: write");
    suite.run("basic write + available", test_write_and_available);
    suite.run("fill to capacity", test_write_fills_to_capacity);
    suite.run("write when full", test_write_when_full_returns_zero);
    suite.run("partial write", test_write_partial_when_nearly_full);
    suite.run("sequential writes", test_sequential_writes);
    suite.run("empty string", test_write_empty_string);
    suite.run("log (fire and forget)", test_log_does_not_require_check);

    umi::test::Suite::section("Monitor: read");
    suite.run("read_byte empty", test_read_byte_empty);
    suite.run("read_line empty", test_read_line_empty);
    suite.run("read_line zero max", test_read_line_zero_max);

    umi::test::Suite::section("Monitor: bounds");
    suite.run("out-of-range buffer index", test_out_of_range_buffer);

    umi::test::Suite::section("Monitor: practical session");
    suite.run("logging session simulation", test_logging_session);
}

} // namespace umirtm::test
