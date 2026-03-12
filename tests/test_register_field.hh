// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Register/Field/Value operations — practical workflow tests.
/// @author Shota Moriguchi @tekitounix
/// @details Simulates real-world register operations on a mock device:
///          reading, writing, read-modify-write, field extraction, multi-field
///          writes, and 1-bit field operations.
#pragma once

#include "test_mock.hh"

namespace umimmio::test {
namespace {

using umi::test::TestContext;

// =============================================================================
// Helper types for RegisterArray / IndexedArray tests
// =============================================================================

struct ArrayDevice : Device<> {
    static constexpr Addr base_address = 0x1000;
};

template <std::size_t N>
struct ArrayReg : Register<ArrayDevice, 0x10 + (N * 4), bits32> {
    static_assert(N < 8);
};

using TestRegArray = RegisterArray<ArrayReg, 8>;

using TestIdxArray8 = IndexedArray<ArrayDevice, 0x100, 32>;
using TestIdxArray16 = IndexedArray<ArrayDevice, 0x200, 16, bits16>;
using TestIdxArray16Stride4 = IndexedArray<ArrayDevice, 0x300, 8, bits16, 4>;
using BoundsIdxArray = IndexedArray<ArrayDevice, 0x100, 4, bits8>;

} // namespace

inline void run_register_field_tests(umi::test::Suite& suite) {
    suite.section("Register read/write");

    suite.run("write and read back", [](TestContext& t) {
        const MockTransport hw;
        hw.write(DataReg::value(0xDEAD'BEEFU));
        auto val = hw.read(DataReg{});
        t.eq(val.bits(), static_cast<std::uint32_t>(0xDEAD'BEEF));
    });

    suite.run("write zero", [](TestContext& t) {
        const MockTransport hw;
        hw.write(DataReg::value(0xFFFF'FFFFU));
        hw.write(DataReg::value(0U));
        t.eq(hw.read(DataReg{}).bits(), static_cast<std::uint32_t>(0));
    });

    suite.run("16-bit register", [](TestContext& t) {
        const MockTransport hw;
        hw.write(CtrlReg::value(static_cast<std::uint16_t>(0x1234)));
        auto val = hw.read(CtrlReg{});
        t.eq(val.bits(), static_cast<std::uint16_t>(0x1234));
    });

    suite.section("Field read/write");

    suite.run("write single field", [](TestContext& t) {
        const MockTransport hw;
        hw.write(ConfigEnable::Set{});
        auto reg_val = hw.read(ConfigReg{});
        t.is_true((reg_val.bits() & 1U) == 1U);
        t.is_true(hw.is(ConfigEnable::Set{}));
    });

    suite.run("field extraction", [](TestContext& t) {
        MockTransport hw;
        std::uint32_t raw = 0U;
        raw |= 1U;
        raw |= (2U << 1);
        raw |= (0xABU << 8);
        hw.poke<std::uint32_t>(0x04, raw);
        t.is_true(hw.is(ConfigEnable::Set{}));
        t.is_true(hw.is(ModeLowPower{}));
        t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<std::uint8_t>(0xAB));
    });

    suite.run("ctrl reg fields", [](TestContext& t) {
        MockTransport hw;
        std::uint16_t raw = 0U;
        raw |= 1U;
        raw |= (1U << 1);
        raw |= (0x0FU << 4);
        hw.poke<std::uint16_t>(0x0C, raw);
        t.is_true(hw.is(CtrlStart::Set{}));
        t.is_true(hw.is(CtrlIrqEn::Set{}));
        t.eq(hw.read(CtrlChannel{}).bits(), static_cast<std::uint8_t>(0x0F));
    });

    suite.section("Read-modify-write");

    suite.run("modify single field", [](TestContext& t) {
        MockTransport hw;
        std::uint32_t const init = (0x10U << 8);
        hw.poke<std::uint32_t>(0x04, init);
        hw.modify(ConfigEnable::Set{});
        t.is_true(hw.is(ConfigEnable::Set{}));
        t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<std::uint8_t>(0x10));
        t.is_true(hw.is(ModeNormal{}));
    });

    suite.run("preserves other fields", [](TestContext& t) {
        MockTransport hw;
        std::uint32_t const init = 1U | (3U << 1) | (0xFFU << 8);
        hw.poke<std::uint32_t>(0x04, init);
        hw.modify(ConfigPrescaler::value(static_cast<std::uint8_t>(0x42)));
        t.is_true(hw.is(ConfigEnable::Set{}));
        t.is_true(hw.is(ModeTest{}));
        t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<std::uint8_t>(0x42));
    });

    suite.run("modify multiple fields", [](TestContext& t) {
        MockTransport hw;
        hw.poke<std::uint32_t>(0x04, 0U);
        hw.modify(ConfigEnable::Set{}, ModeFast{});
        t.is_true(hw.is(ConfigEnable::Set{}));
        t.is_true(hw.is(ModeFast{}));
        t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<std::uint8_t>(0));
    });

    suite.section("Multi-field write");

    suite.run("write multiple fields", [](TestContext& t) {
        const MockTransport hw;
        hw.write(ConfigEnable::Set{}, ModeTest{}, ConfigPrescaler::value(static_cast<std::uint8_t>(0x55)));
        t.is_true(hw.is(ConfigEnable::Set{}));
        t.is_true(hw.is(ModeTest{}));
        t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<std::uint8_t>(0x55));
    });

    suite.section("Value/is()");

    suite.run("enum value write+check", [](TestContext& t) {
        const MockTransport hw;
        hw.write(ConfigEnable::Set{}, ModeLowPower{});
        t.is_true(hw.is(ModeLowPower{}));
        t.is_true(!hw.is(ModeFast{}));
        t.is_true(hw.is(ConfigEnable::Set{}));
        t.is_true(!hw.is(ConfigEnable::Reset{}));
    });

    suite.run("dynamic value is()", [](TestContext& t) {
        const MockTransport hw;
        hw.write(ConfigPrescaler::value(static_cast<std::uint8_t>(100)));
        t.is_true(hw.is(ConfigPrescaler::value(static_cast<std::uint8_t>(100))));
        t.is_true(!hw.is(ConfigPrescaler::value(static_cast<std::uint8_t>(99))));
    });

    suite.section("1-bit flip");

    suite.run("toggle enable", [](TestContext& t) {
        MockTransport hw;
        hw.poke<std::uint32_t>(0x04, 0U);
        t.is_true(hw.is(ConfigEnable::Reset{}));
        hw.flip(ConfigEnable{});
        t.is_true(hw.is(ConfigEnable::Set{}));
        hw.flip(ConfigEnable{});
        t.is_true(hw.is(ConfigEnable::Reset{}));
    });

    suite.run("flip preserves neighbors", [](TestContext& t) {
        MockTransport hw;
        std::uint32_t const init = (2U << 1) | (0x42U << 8);
        hw.poke<std::uint32_t>(0x04, init);
        hw.flip(ConfigEnable{});
        t.is_true(hw.is(ConfigEnable::Set{}));
        t.is_true(hw.is(ModeLowPower{}));
        t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<std::uint8_t>(0x42));
    });

    suite.section("Practical workflow");

    suite.run("peripheral init sequence", [](TestContext& t) {
        const MockTransport hw;
        hw.write(ConfigPrescaler::value(static_cast<std::uint8_t>(0x10)), ModeFast{});
        t.is_true(hw.is(ConfigEnable::Reset{}));
        t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<std::uint8_t>(0x10));
        t.is_true(hw.is(ModeFast{}));

        hw.write(CtrlIrqEn::Set{}, CtrlChannel::value(static_cast<std::uint8_t>(3)));
        t.eq(hw.read(CtrlChannel{}).bits(), static_cast<std::uint8_t>(3));
        t.is_true(hw.is(CtrlIrqEn::Set{}));

        hw.modify(ConfigEnable::Set{});
        t.is_true(hw.is(ConfigEnable::Set{}));
        t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<std::uint8_t>(0x10));
        t.is_true(hw.is(ModeFast{}));

        hw.modify(CtrlStart::Set{});
        t.is_true(hw.is(CtrlStart::Set{}));
        t.is_true(hw.is(CtrlIrqEn::Set{}));
        t.eq(hw.read(CtrlChannel{}).bits(), static_cast<std::uint8_t>(3));
    });

    suite.section("RegionValue");

    suite.run("get() field extraction", [](TestContext& t) {
        MockTransport hw;
        hw.poke<std::uint32_t>(0x04, 0x0000'1203U);
        auto cfg = hw.read(ConfigReg{});
        t.is_true(cfg.is(ConfigEnable::Set{}));
        t.is_true(cfg.is(ModeFast{}));
        t.eq(cfg.get(ConfigPrescaler{}).bits(), static_cast<std::uint8_t>(0x12));
    });

    suite.run("is() value matching", [](TestContext& t) {
        MockTransport hw;
        hw.poke<std::uint32_t>(0x04, 0x0000'1003U);
        auto cfg = hw.read(ConfigReg{});
        t.is_true(cfg.is(ConfigEnable::Set{}));
        t.is_true(cfg.is(ModeFast{}));
        t.is_true(!cfg.is(ModeNormal{}));
    });

    suite.section("1-bit custom aliases");

    suite.run("write with custom alias", [](TestContext& t) {
        const MockTransport hw;
        hw.write(CtrlIrqEn::Enabled{}, CtrlChannel::value(static_cast<std::uint8_t>(5)));
        t.is_true(hw.is(CtrlIrqEn::Enabled{}));
        t.is_true(hw.is(CtrlIrqEn::Set{}));
        t.eq(hw.read(CtrlChannel{}).bits(), static_cast<std::uint8_t>(5));
    });

    suite.run("modify with custom alias", [](TestContext& t) {
        MockTransport hw;
        hw.poke<std::uint16_t>(0x0C, 0U);
        hw.modify(CtrlIrqEn::Enabled{});
        t.is_true(hw.is(CtrlIrqEn::Enabled{}));
        hw.modify(CtrlIrqEn::Disabled{});
        t.is_true(hw.is(CtrlIrqEn::Disabled{}));
        t.is_true(hw.is(CtrlIrqEn::Reset{}));
    });

    suite.section("dispatch");

    suite.run("basic dispatch", [](TestContext& t) {
        std::size_t called_with = 999;
        dispatch<4>(2, [&]<std::size_t I>() { called_with = I; });
        t.eq(called_with, std::size_t{2});
    });

    suite.run("boundary index", [](TestContext& t) {
        std::size_t called_with = 999;
        dispatch<4>(3, [&]<std::size_t I>() { called_with = I; });
        t.eq(called_with, std::size_t{3});
    });

    suite.run("index zero", [](TestContext& t) {
        std::size_t called_with = 999;
        dispatch<4>(0, [&]<std::size_t I>() { called_with = I; });
        t.eq(called_with, std::size_t{0});
    });

    suite.run("out of range (IgnoreError)", [](TestContext& t) {
        bool called = false;
        dispatch<4, IgnoreError>(4, [&]<std::size_t I>() { called = true; });
        t.is_true(!called);
    });

    suite.run("zero range N=0", [](TestContext& t) {
        bool called = false;
        dispatch<0, IgnoreError>(0, [&]<std::size_t I>() { called = true; });
        t.is_true(!called);
    });

    suite.run("dispatch_r zero range N=0", [](TestContext& t) {
        auto result = dispatch_r<0, int, IgnoreError>(0, []<std::size_t I>() -> int { return 42; }, -1);
        t.eq(result, -1);
    });

    suite.run("dispatch_r basic", [](TestContext& t) {
        auto result = dispatch_r<4, int>(2, []<std::size_t I>() -> int { return static_cast<int>(I * 10); });
        t.eq(result, 20);
    });

    suite.run("dispatch_r out of range", [](TestContext& t) {
        auto result =
            dispatch_r<4, int, IgnoreError>(4, []<std::size_t I>() -> int { return static_cast<int>(I); }, -1);
        t.eq(result, -1);
    });

    suite.section("RegisterArray");

    suite.run("size", [](TestContext& t) { t.eq(TestRegArray::size, std::size_t{8}); });

    suite.run("element address", [](TestContext& t) {
        t.eq(TestRegArray::Element<0>::address, static_cast<Addr>(0x1010));
        t.eq(TestRegArray::Element<3>::address, static_cast<Addr>(0x101C));
        t.eq(TestRegArray::Element<7>::address, static_cast<Addr>(0x102C));
    });

    suite.run("dispatch with array", [](TestContext& t) {
        Addr addr = 0;
        dispatch<TestRegArray::size>(5, [&]<std::size_t I>() { addr = TestRegArray::Element<I>::address; });
        t.eq(addr, static_cast<Addr>(0x1024));
    });

    suite.section("IndexedArray");

    suite.run("size", [](TestContext& t) {
        t.eq(TestIdxArray8::size, std::size_t{32});
        t.eq(TestIdxArray16::size, std::size_t{16});
    });

    suite.run("entry width", [](TestContext& t) {
        t.eq(TestIdxArray8::entry_width, std::size_t{8});
        t.eq(TestIdxArray16::entry_width, std::size_t{16});
    });

    suite.run("stride", [](TestContext& t) {
        t.eq(TestIdxArray8::stride, std::size_t{1});
        t.eq(TestIdxArray16::stride, std::size_t{2});
        t.eq(TestIdxArray16Stride4::stride, std::size_t{4});
    });

    suite.run("8-bit entry address", [](TestContext& t) {
        t.eq(TestIdxArray8::Entry<0>::address, static_cast<Addr>(0x1100));
        t.eq(TestIdxArray8::Entry<1>::address, static_cast<Addr>(0x1101));
        t.eq(TestIdxArray8::Entry<31>::address, static_cast<Addr>(0x111F));
    });

    suite.run("16-bit entry address", [](TestContext& t) {
        t.eq(TestIdxArray16::Entry<0>::address, static_cast<Addr>(0x1200));
        t.eq(TestIdxArray16::Entry<1>::address, static_cast<Addr>(0x1202));
        t.eq(TestIdxArray16::Entry<15>::address, static_cast<Addr>(0x121E));
    });

    suite.run("custom stride address", [](TestContext& t) {
        t.eq(TestIdxArray16Stride4::Entry<0>::address, static_cast<Addr>(0x1300));
        t.eq(TestIdxArray16Stride4::Entry<1>::address, static_cast<Addr>(0x1304));
        t.eq(TestIdxArray16Stride4::Entry<7>::address, static_cast<Addr>(0x131C));
    });

    suite.section("IndexedArray runtime bounds");

    suite.run("write_entry OOB (IgnoreError)", [](TestContext& t) {
        BoundsIdxArray::write_entry<IgnoreError>(4, 0xFF);
        t.is_true(true);
    });

    suite.run("read_entry OOB (IgnoreError)", [](TestContext& t) {
        auto val = BoundsIdxArray::read_entry<IgnoreError>(4);
        t.eq(val, static_cast<std::uint8_t>(0));
    });

    suite.run("write_entry OOB (CustomErrorHandler)", [](TestContext& t) {
        static bool called = false;
        static const char* captured_msg = nullptr;
        called = false;
        captured_msg = nullptr;

        using Handler = CustomErrorHandler<+[](const char* msg) {
            called = true;
            captured_msg = msg;
        }>;

        BoundsIdxArray::write_entry<Handler>(99, 0x42);
        t.is_true(called);
        t.is_true(captured_msg != nullptr);
    });

    suite.run("read_entry OOB (CustomErrorHandler)", [](TestContext& t) {
        static bool called = false;
        called = false;

        using Handler = CustomErrorHandler<+[](const char* /*msg*/) { called = true; }>;

        auto val = BoundsIdxArray::read_entry<Handler>(4);
        t.is_true(called);
        t.eq(val, static_cast<std::uint8_t>(0));
    });
}

} // namespace umimmio::test
