// SPDX-License-Identifier: MIT
/// @file
/// @brief Extended Monitor tests: multi-buffer, NoBlockTrim mode, control block,
///        binary data, wrap-around, and interleaved read/write patterns.

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <tuple>

#include "test_fixture.hh"

namespace umirtm::test {
namespace {

using umi::test::TestContext;

// =============================================================================
// Monitor configurations
// =============================================================================

// Trim mode monitor: partial writes trim instead of skip
using TrimMonitor = rt::Monitor<1, 1, 32, 32, rt::Mode::NoBlockTrim>;

// Tiny monitor for wrap-around testing
using TinyMonitor = rt::Monitor<1, 1, 8, 8, rt::Mode::NoBlockSkip>;

// Medium monitor for various tests
using MedMonitor64 = rt::Monitor<1, 1, 64, 64, rt::Mode::NoBlockSkip>;

// =============================================================================
// Write + available tracking
// =============================================================================

bool test_available_tracks_writes(TestContext& t) {
    MedMonitor64::init("AVAIL");

    bool ok = true;
    ok &= t.assert_eq(MedMonitor64::get_available<0>(), std::size_t{0});

    auto w1 = MedMonitor64::write<0>(std::string_view{"abc"});
    ok &= t.assert_eq(w1, std::size_t{3});
    ok &= t.assert_eq(MedMonitor64::get_available<0>(), std::size_t{3});

    auto w2 = MedMonitor64::write<0>(std::string_view{"de"});
    ok &= t.assert_eq(w2, std::size_t{2});
    ok &= t.assert_eq(MedMonitor64::get_available<0>(), std::size_t{5});
    return ok;
}

bool test_free_space_decreases(TestContext& t) {
    MedMonitor64::init("FREE");

    bool ok = true;
    ok &= t.assert_eq(MedMonitor64::get_free_space<0>(), std::size_t{63});

    std::ignore = MedMonitor64::write<0>(std::string_view{"12345"});
    ok &= t.assert_eq(MedMonitor64::get_free_space<0>(), std::size_t{58});

    std::ignore = MedMonitor64::write<0>(std::string_view{"67890"});
    ok &= t.assert_eq(MedMonitor64::get_free_space<0>(), std::size_t{53});
    return ok;
}

bool test_free_space_plus_available_equals_capacity(TestContext& t) {
    MedMonitor64::init("CAP");

    std::ignore = MedMonitor64::write<0>(std::string_view{"hello world"});

    bool ok = true;
    auto avail = MedMonitor64::get_available<0>();
    auto free = MedMonitor64::get_free_space<0>();
    // capacity = 63 (64 - 1 for ring buffer)
    ok &= t.assert_eq(avail + free, std::size_t{63});
    return ok;
}

// =============================================================================
// NoBlockTrim mode
// =============================================================================

bool test_trim_mode_partial_write(TestContext& t) {
    TrimMonitor::init("TRIM");

    // Fill 25 of 31 bytes
    auto w1 = TrimMonitor::write<0>(std::string_view{"1234567890123456789012345"});
    bool ok = true;
    ok &= t.assert_eq(w1, std::size_t{25});

    // Try to write 10 bytes, only 6 should fit (trimmed)
    auto w2 = TrimMonitor::write<0>(std::string_view{"ABCDEFGHIJ"});
    ok &= t.assert_eq(w2, std::size_t{6});
    ok &= t.assert_eq(TrimMonitor::get_free_space<0>(), std::size_t{0});
    return ok;
}

bool test_trim_mode_exact_fill(TestContext& t) {
    TrimMonitor::init("TRIM");

    // Capacity is 31 bytes, write exactly 31
    auto w = TrimMonitor::write<0>(std::string_view{"1234567890123456789012345678901"});
    bool ok = true;
    ok &= t.assert_eq(w, std::size_t{31});
    ok &= t.assert_eq(TrimMonitor::get_free_space<0>(), std::size_t{0});
    ok &= t.assert_eq(TrimMonitor::get_available<0>(), std::size_t{31});
    return ok;
}

// =============================================================================
// Tiny buffer edge cases
// =============================================================================

bool test_tiny_single_byte_write(TestContext& t) {
    TinyMonitor::init("TINY");

    auto w = TinyMonitor::write<0>(std::string_view{"X"});
    bool ok = true;
    ok &= t.assert_eq(w, std::size_t{1});
    ok &= t.assert_eq(TinyMonitor::get_available<0>(), std::size_t{1});
    ok &= t.assert_eq(TinyMonitor::get_free_space<0>(), std::size_t{6}); // 7-1
    return ok;
}

bool test_tiny_fill_and_reject(TestContext& t) {
    TinyMonitor::init("TINY");

    // Capacity = 7 (8-1)
    auto w1 = TinyMonitor::write<0>(std::string_view{"1234567"});
    bool ok = true;
    ok &= t.assert_eq(w1, std::size_t{7});

    // Buffer full, should reject
    auto w2 = TinyMonitor::write<0>(std::string_view{"x"});
    ok &= t.assert_eq(w2, std::size_t{0});
    return ok;
}

bool test_tiny_binary_data(TestContext& t) {
    TinyMonitor::init("TINY");

    // Write binary data (with null bytes) via span<const byte>
    std::array<std::byte, 3> data{std::byte{0x00}, std::byte{0xFF}, std::byte{0x42}};
    auto w = TinyMonitor::write<0>(std::span{data});
    bool ok = true;
    ok &= t.assert_eq(w, std::size_t{3});
    ok &= t.assert_eq(TinyMonitor::get_available<0>(), std::size_t{3});
    return ok;
}

// =============================================================================
// Control block access
// =============================================================================

bool test_control_block_pointer(TestContext& t) {
    using SmallMonitor = rt::Monitor<1, 1, 16, 16, rt::Mode::NoBlockSkip>;
    SmallMonitor::init("CB TEST");

    auto* cb = SmallMonitor::get_control_block();
    bool ok = true;
    ok &= t.assert_true(cb != nullptr, "control block is non-null");
    return ok;
}

bool test_control_block_size(TestContext& t) {
    using SmallMonitor = rt::Monitor<1, 1, 16, 16, rt::Mode::NoBlockSkip>;
    auto size = SmallMonitor::get_control_block_size();
    return t.assert_gt(size, std::size_t{0});
}

// =============================================================================
// String view write
// =============================================================================

bool test_write_string_view(TestContext& t) {
    using MedMonitor = rt::Monitor<1, 1, 64, 64, rt::Mode::NoBlockSkip>;
    MedMonitor::init("SV");

    std::string_view sv = "test message";
    auto w = MedMonitor::write<0>(sv);
    return t.assert_eq(w, sv.size());
}

bool test_write_cstring(TestContext& t) {
    using MedMonitor = rt::Monitor<1, 1, 64, 64, rt::Mode::NoBlockSkip>;
    MedMonitor::init("CS");

    auto w = MedMonitor::write<0>("hello");
    return t.assert_eq(w, std::size_t{5});
}

// =============================================================================
// Init reinitializes
// =============================================================================

bool test_reinit_clears(TestContext& t) {
    using SmallMonitor = rt::Monitor<1, 1, 16, 16, rt::Mode::NoBlockSkip>;

    SmallMonitor::init("FIRST");
    std::ignore = SmallMonitor::write<0>(std::string_view{"hello"});

    // Re-init should reset
    SmallMonitor::init("SECOND");

    bool ok = true;
    ok &= t.assert_eq(SmallMonitor::get_available<0>(), std::size_t{0});
    ok &= t.assert_eq(SmallMonitor::get_free_space<0>(), std::size_t{15});
    return ok;
}

// =============================================================================
// Interleaved operations
// =============================================================================

bool test_interleaved_write_log(TestContext& t) {
    using MedMonitor = rt::Monitor<1, 1, 64, 64, rt::Mode::NoBlockSkip>;
    MedMonitor::init("INTERLEAVE");

    // Mix write() and log()
    auto w1 = MedMonitor::write<0>(std::string_view{"[INFO] "});
    MedMonitor::log<0>("startup\n");

    bool ok = true;
    ok &= t.assert_eq(w1, std::size_t{7});
    ok &= t.assert_eq(MedMonitor::get_available<0>(), std::size_t{15}); // 7 + 8
    return ok;
}

// =============================================================================
// Log with embedded nulls
// =============================================================================

bool test_write_binary_with_nulls(TestContext& t) {
    using MedMonitor = rt::Monitor<1, 1, 64, 64, rt::Mode::NoBlockSkip>;
    MedMonitor::init("BIN");

    std::array<std::byte, 5> data{std::byte{0x01}, std::byte{0x00}, std::byte{0x02},
                                   std::byte{0x00}, std::byte{0x03}};
    auto w = MedMonitor::write<0>(std::span{data});

    bool ok = true;
    ok &= t.assert_eq(w, std::size_t{5});
    ok &= t.assert_eq(MedMonitor::get_available<0>(), std::size_t{5});
    return ok;
}

// =============================================================================
// Down buffer read operations
// =============================================================================

bool test_down_buffer_read_byte_out_of_range(TestContext& t) {
    using SmallMonitor = rt::Monitor<1, 1, 16, 16, rt::Mode::NoBlockSkip>;
    SmallMonitor::init("DOWN");

    // Down buffer index 1 is out of range (only 1 down buffer)
    return t.assert_eq(SmallMonitor::read_byte<1>(), -1);
}

bool test_down_buffer_read_line_null_term(TestContext& t) {
    using SmallMonitor = rt::Monitor<1, 1, 16, 16, rt::Mode::NoBlockSkip>;
    SmallMonitor::init("DOWN");

    // Empty down buffer should return nullptr
    char line[32]{};
    auto* result = SmallMonitor::read_line<0>(line, sizeof(line));
    return t.assert_eq(result, static_cast<char*>(nullptr));
}

} // namespace

void run_monitor_extended_tests(umi::test::Suite& suite);

void run_monitor_extended_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("Monitor: write tracking");
    suite.run("available tracks writes", test_available_tracks_writes);
    suite.run("free space decreases", test_free_space_decreases);
    suite.run("free + available = capacity", test_free_space_plus_available_equals_capacity);

    umi::test::Suite::section("Monitor: NoBlockTrim");
    suite.run("partial write (trim)", test_trim_mode_partial_write);
    suite.run("exact fill", test_trim_mode_exact_fill);

    umi::test::Suite::section("Monitor: tiny buffer");
    suite.run("single byte write", test_tiny_single_byte_write);
    suite.run("fill and reject", test_tiny_fill_and_reject);
    suite.run("binary data", test_tiny_binary_data);

    umi::test::Suite::section("Monitor: control block");
    suite.run("non-null pointer", test_control_block_pointer);
    suite.run("non-zero size", test_control_block_size);

    umi::test::Suite::section("Monitor: string write");
    suite.run("string_view write", test_write_string_view);
    suite.run("C-string write", test_write_cstring);

    umi::test::Suite::section("Monitor: reinit");
    suite.run("reinit clears state", test_reinit_clears);

    umi::test::Suite::section("Monitor: interleaved ops");
    suite.run("write + log", test_interleaved_write_log);
    suite.run("binary with nulls", test_write_binary_with_nulls);

    umi::test::Suite::section("Monitor: down buffer");
    suite.run("read_byte OOB", test_down_buffer_read_byte_out_of_range);
    suite.run("read_line empty", test_down_buffer_read_line_null_term);
}

} // namespace umirtm::test
