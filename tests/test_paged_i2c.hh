// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for PagedI2cTransport (page-select caching) and LatchError.
/// @author Shota Moriguchi @tekitounix
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <umimmio/transport/paged_i2c.hh>

#include "test_mock.hh"

namespace umimmio::test {
namespace {

using umi::test::TestContext;
using namespace umi::mmio;

// =============================================================================
// Fake paged I2C bus — records raw frames and models a 2-page register file
// =============================================================================

/// @brief Fake I2C bus for a paged register file.
///
/// Models the wire protocol exactly like real paged hardware: the "current
/// page" is bus-side state that only changes when a frame targets
/// `page_select_addr` (offset 0x00 in these tests). Every write/write_read
/// frame is recorded verbatim in `frames` so tests can assert on exactly
/// which bytes were put on the wire (and therefore whether a page-select was
/// emitted).
///
/// `fail_next_selects` makes the next N page-select writes fail (NACK)
/// without changing the bus-side page — modeling a chip that rejects the
/// select. Register writes and reads are unaffected.
struct FakePagedI2C {
    static constexpr std::uint8_t page_select_addr = 0x00;

    struct Result {
        explicit operator bool() const { return success; }
        bool success = true;
    };

    mutable std::uint8_t page = 0;
    mutable int fail_next_selects = 0;
    mutable std::array<std::array<std::uint8_t, 256>, 2> memory{};
    mutable std::vector<std::vector<std::uint8_t>> frames;

    Result write(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> data) const {
        frames.emplace_back(data.begin(), data.end());
        if (data.size() < 2) {
            return {false};
        }
        if (data[0] == page_select_addr) {
            if (fail_next_selects > 0) {
                --fail_next_selects;
                return {false};
            }
            page = data[1];
            return {true};
        }
        std::uint8_t const reg_addr = data[0];
        std::memcpy(&memory[page][reg_addr], data.data() + 1, data.size() - 1);
        return {true};
    }

    Result write_read(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx) const {
        frames.emplace_back(tx.begin(), tx.end());
        if (tx.empty()) {
            return {false};
        }
        std::uint8_t const reg_addr = tx[0];
        std::memcpy(rx.data(), &memory[page][reg_addr], rx.size());
        return {true};
    }
};

/// @brief Bus that always fails, used for LatchError transport-error tests.
struct FailingPagedI2C {
    struct Result {
        explicit operator bool() const { return success; }
        bool success = false;
    };

    [[nodiscard]] Result write(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> /*data*/) const {
        return {false};
    }

    [[nodiscard]] Result
    write_read(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> /*tx*/, std::span<std::uint8_t> /*rx*/) const {
        return {false};
    }
};

// =============================================================================
// Device/register map — two pages, in-page offsets kept off page_select_addr
// =============================================================================

struct PagedDevice : Device<RW, I2c> {};

/// @brief Register at in-page offset 0x10 on page 0 (logical address 0x0010).
struct Page0Reg : Register<PagedDevice, 0x0010, bits8, RW, 0> {};

/// @brief Register at in-page offset 0x10 on page 1 (logical address 0x0110).
struct Page1Reg : Register<PagedDevice, 0x0110, bits8, RW, 0> {};

/// @brief 16-bit register at in-page offset 0x20 on page 1 (logical address 0x0120).
struct Page1Reg16 : Register<PagedDevice, 0x0120, bits16, RW, 0> {};

struct Page0Field : Field<Page0Reg, 0, 4, Numeric> {};

// =============================================================================
// LatchError tags — one per "device instance" under test
// =============================================================================

struct TestLatchTag {};
struct SelectFailTag {};

} // namespace

// =============================================================================
// Test functions
// =============================================================================

inline void test_paged_first_access_always_selects(TestContext& t) {
    FakePagedI2C bus;
    const PagedI2cTransport<FakePagedI2C> transport(bus, 0x50);

    // The cache starts invalid (chip page unknown at construction), so the
    // first access always emits a page-select write — even for page 0.
    transport.write(Page0Reg::value(static_cast<std::uint8_t>(0x5A)));

    t.eq(bus.frames.size(), std::size_t{2});
    t.eq(bus.frames[0].size(), std::size_t{2});
    t.eq(bus.frames[0][0], static_cast<std::uint8_t>(FakePagedI2C::page_select_addr));
    t.eq(bus.frames[0][1], static_cast<std::uint8_t>(0));
    t.eq(bus.frames[1][0], static_cast<std::uint8_t>(0x10));
    t.eq(bus.frames[1][1], static_cast<std::uint8_t>(0x5A));
}

inline void test_paged_select_emitted_only_on_page_change(TestContext& t) {
    FakePagedI2C bus;
    const PagedI2cTransport<FakePagedI2C> transport(bus, 0x50);

    // First write to page 0: initial select (cache invalid) + reg write.
    transport.write(Page0Reg::value(static_cast<std::uint8_t>(0x01)));
    t.eq(bus.frames.size(), std::size_t{2});

    // Second write to page 0: cache valid and matching -> no page-select.
    transport.write(Page0Reg::value(static_cast<std::uint8_t>(0x02)));
    t.eq(bus.frames.size(), std::size_t{3});

    // Write to page 1: cache (0) != target page (1) -> page-select + reg write.
    transport.write(Page1Reg::value(static_cast<std::uint8_t>(0x03)));
    t.eq(bus.frames.size(), std::size_t{5});
    t.eq(bus.frames[3].size(), std::size_t{2});
    t.eq(bus.frames[3][0], static_cast<std::uint8_t>(FakePagedI2C::page_select_addr));
    t.eq(bus.frames[3][1], static_cast<std::uint8_t>(1));
    t.eq(bus.frames[4][0], static_cast<std::uint8_t>(0x10));
    t.eq(bus.frames[4][1], static_cast<std::uint8_t>(0x03));

    // Another write to page 1: cache already 1 -> no page-select.
    transport.write(Page1Reg::value(static_cast<std::uint8_t>(0x04)));
    t.eq(bus.frames.size(), std::size_t{6});
}

inline void test_paged_invalidate_cache_forces_reselect(TestContext& t) {
    FakePagedI2C bus;
    const PagedI2cTransport<FakePagedI2C> transport(bus, 0x50);

    // Select page 1 once (page-select + write = 2 frames).
    transport.write(Page1Reg::value(static_cast<std::uint8_t>(0x11)));
    t.eq(bus.frames.size(), std::size_t{2});

    // Write to page 1 again: cache already 1, no page-select (1 frame).
    transport.write(Page1Reg::value(static_cast<std::uint8_t>(0x12)));
    t.eq(bus.frames.size(), std::size_t{3});

    // Invalidate the cache (simulating "driver just soft-reset the chip").
    transport.invalidate_page_cache();

    // Next access must re-select regardless of the target page — here the
    // target page (1) equals the last cached page, and the select must
    // still be emitted because the cache no longer trusts the chip state.
    transport.write(Page1Reg::value(static_cast<std::uint8_t>(0x13)));
    t.eq(bus.frames.size(), std::size_t{5});
    t.eq(bus.frames[3][0], static_cast<std::uint8_t>(FakePagedI2C::page_select_addr));
    t.eq(bus.frames[3][1], static_cast<std::uint8_t>(1));

    // Invalidate again and access page 0: the select must also be emitted
    // for page 0 (no "reset lands on page 0" assumption).
    transport.invalidate_page_cache();
    transport.write(Page0Reg::value(static_cast<std::uint8_t>(0x14)));
    t.eq(bus.frames.size(), std::size_t{7});
    t.eq(bus.frames[5][0], static_cast<std::uint8_t>(FakePagedI2C::page_select_addr));
    t.eq(bus.frames[5][1], static_cast<std::uint8_t>(0));
}

inline void test_paged_select_failure_no_cache_advance(TestContext& t) {
    LatchError<SelectFailTag>::latched = false;

    FakePagedI2C bus;
    bus.fail_next_selects = 1;
    const PagedI2cTransport<FakePagedI2C, std::true_type, LatchError<SelectFailTag>> transport(bus, 0x50);

    // First write targets page 1; the page-select write fails (NACK).
    transport.write(Page1Reg::value(static_cast<std::uint8_t>(0x11)));

    // The error is latched, and only the failed page-select frame was sent —
    // the register write must NOT follow a failed select.
    t.is_true(LatchError<SelectFailTag>::take());
    t.eq(bus.frames.size(), std::size_t{1});
    t.eq(bus.frames[0][0], static_cast<std::uint8_t>(FakePagedI2C::page_select_addr));
    t.eq(bus.memory[1][0x10], static_cast<std::uint8_t>(0));

    // The cache must not have advanced: the next access to page 1 retries
    // the page-select (now succeeding) and then performs the register write.
    transport.write(Page1Reg::value(static_cast<std::uint8_t>(0x22)));
    t.eq(bus.frames.size(), std::size_t{3});
    t.eq(bus.frames[1][0], static_cast<std::uint8_t>(FakePagedI2C::page_select_addr));
    t.eq(bus.frames[1][1], static_cast<std::uint8_t>(1));
    t.eq(bus.frames[2][0], static_cast<std::uint8_t>(0x10));
    t.eq(bus.frames[2][1], static_cast<std::uint8_t>(0x22));
    t.eq(bus.memory[1][0x10], static_cast<std::uint8_t>(0x22));
    t.is_true(!LatchError<SelectFailTag>::take());
}

inline void test_paged_typed_register_field_roundtrip(TestContext& t) {
    FakePagedI2C bus;
    const PagedI2cTransport<FakePagedI2C> transport(bus, 0x50);

    transport.write(Page0Reg::value(static_cast<std::uint8_t>(0x3C)));
    transport.write(Page1Reg::value(static_cast<std::uint8_t>(0x7E)));

    // Page isolation: reading page 0 after writing page 1 must still return
    // the page-0 value, not the page-1 value.
    auto page0_val = transport.read(Page0Reg{});
    auto page1_val = transport.read(Page1Reg{});
    t.eq(page0_val.bits(), static_cast<std::uint8_t>(0x3C));
    t.eq(page1_val.bits(), static_cast<std::uint8_t>(0x7E));

    // Field read within a paged register.
    auto field_val = transport.read(Page0Field{});
    t.eq(field_val.bits(), static_cast<std::uint8_t>(0x3C & 0x0F));
}

inline void test_paged_multibyte_register_on_nonzero_page(TestContext& t) {
    FakePagedI2C bus;
    const PagedI2cTransport<FakePagedI2C> transport(bus, 0x50);

    // 16-bit register on page 1: typed roundtrip.
    transport.write(Page1Reg16::value(static_cast<std::uint16_t>(0xABCD)));
    auto val = transport.read(Page1Reg16{});
    t.eq(val.bits(), static_cast<std::uint16_t>(0xABCD));

    // Wire byte order: PagedI2cTransport uses little-endian data, so the
    // low byte lands at the in-page offset and the high byte follows.
    t.eq(bus.memory[1][0x20], static_cast<std::uint8_t>(0xCD));
    t.eq(bus.memory[1][0x21], static_cast<std::uint8_t>(0xAB));
}

inline void test_latch_error_take_semantics(TestContext& t) {
    LatchError<TestLatchTag>::latched = false;

    FailingPagedI2C bus;
    const PagedI2cTransport<FailingPagedI2C, std::true_type, LatchError<TestLatchTag>> transport(bus, 0x50);

    // First access fails at the page-select write (cache starts invalid, so
    // a select is always attempted first) and latches the error.
    transport.write(Page0Reg::value(static_cast<std::uint8_t>(0x01)));
    t.is_true(LatchError<TestLatchTag>::take());

    // take() clears the latch: a second call with no new error returns false.
    t.is_true(!LatchError<TestLatchTag>::take());

    // A fresh failure latches again.
    transport.write(Page0Reg::value(static_cast<std::uint8_t>(0x02)));
    t.is_true(LatchError<TestLatchTag>::take());
    t.is_true(!LatchError<TestLatchTag>::take());

    // A failed read latches too, and the result is zero-filled (never
    // uninitialized bytes surfaced as a typed value).
    auto val = transport.read(Page0Reg{});
    t.is_true(LatchError<TestLatchTag>::take());
    t.eq(val.bits(), static_cast<std::uint8_t>(0));
}

inline void run_paged_i2c_tests(umi::test::Suite& suite) {
    suite.section("PagedI2cTransport — page-select caching");
    suite.run("first access always emits page-select", test_paged_first_access_always_selects);
    suite.run("page-select emitted only on page change", test_paged_select_emitted_only_on_page_change);
    suite.run("invalidate_page_cache() forces re-selection", test_paged_invalidate_cache_forces_reselect);
    suite.run("failed page-select does not advance cache", test_paged_select_failure_no_cache_advance);

    suite.section("PagedI2cTransport — typed register/field access");
    suite.run("Register/Field write+read across pages", test_paged_typed_register_field_roundtrip);
    suite.run("16-bit register on page 1 (roundtrip + byte order)", test_paged_multibyte_register_on_nonzero_page);

    suite.section("LatchError policy");
    suite.run("take() latches once then clears", test_latch_error_take_semantics);
}

} // namespace umimmio::test
