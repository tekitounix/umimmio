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
// Basic register read/write
// =============================================================================

void test_register_write_and_read(TestContext& t) {
    const MockTransport hw;

    // Write a known value and read it back
    hw.write(DataReg::value(0xDEAD'BEEFU));
    auto val = hw.read(DataReg{});

    t.eq(val.bits(), static_cast<uint32_t>(0xDEAD'BEEF));
}

void test_register_write_zero(TestContext& t) {
    const MockTransport hw;

    hw.write(DataReg::value(0xFFFF'FFFFU));
    hw.write(DataReg::value(0U));

    t.eq(hw.read(DataReg{}).bits(), static_cast<uint32_t>(0));
}

void test_register_16bit_write_read(TestContext& t) {
    const MockTransport hw;

    hw.write(CtrlReg::value(static_cast<uint16_t>(0x1234)));
    auto val = hw.read(CtrlReg{});

    t.eq(val.bits(), static_cast<uint16_t>(0x1234));
}

// =============================================================================
// Field read/write
// =============================================================================

void test_field_write_single(TestContext& t) {
    const MockTransport hw;

    // Write just the enable bit
    hw.write(ConfigEnable::Set{});
    // Enable bit should be set (bit 0)
    auto reg_val = hw.read(ConfigReg{});
    t.is_true((reg_val.bits() & 1U) == 1U);
    // Also verify via is()
    t.is_true(hw.is(ConfigEnable::Set{}));
}

void test_field_read_extraction(TestContext& t) {
    MockTransport hw;

    // Set up register with known bit pattern:
    // bits 0: enable = 1
    // bits 1-2: mode = 0b10 (LOW_POWER)
    // bits 8-15: prescaler = 0xAB
    uint32_t raw = 0U;
    raw |= 1U;           // enable
    raw |= (2U << 1);    // mode = 2
    raw |= (0xABU << 8); // prescaler = 0xAB
    hw.poke<uint32_t>(0x04, raw);
    t.is_true(hw.is(ConfigEnable::Set{}));
    t.is_true(hw.is(ModeLowPower{}));
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0xAB));
}

void test_field_read_ctrl_reg(TestContext& t) {
    MockTransport hw;

    // CtrlReg at 0x0C, 16-bit
    // bit 0: start, bit 1: irq_en, bits 4-7: channel
    uint16_t raw = 0U;
    raw |= 1U;           // start
    raw |= (1U << 1);    // irq_en
    raw |= (0x0FU << 4); // channel = 15
    hw.poke<uint16_t>(0x0C, raw);
    t.is_true(hw.is(CtrlStart::Set{}));
    t.is_true(hw.is(CtrlIrqEn::Set{}));
    t.eq(hw.read(CtrlChannel{}).bits(), static_cast<uint8_t>(0x0F));
}

// =============================================================================
// Read-Modify-Write (modify)
// =============================================================================

void test_modify_single_field(TestContext& t) {
    MockTransport hw;

    // Set initial config: prescaler = 0x10, mode = NORMAL, enable = 0
    uint32_t const init = (0x10U << 8);
    hw.poke<uint32_t>(0x04, init);

    // Modify only enable — prescaler and mode should be preserved
    hw.modify(ConfigEnable::Set{});
    t.is_true(hw.is(ConfigEnable::Set{}));
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x10));
    t.is_true(hw.is(ModeNormal{}));
}

void test_modify_preserves_other_fields(TestContext& t) {
    MockTransport hw;

    // Initial: all fields set
    uint32_t const init = 1U | (3U << 1) | (0xFFU << 8);
    hw.poke<uint32_t>(0x04, init);

    // Modify just prescaler to 0x42
    hw.modify(ConfigPrescaler::value(static_cast<uint8_t>(0x42)));
    t.is_true(hw.is(ConfigEnable::Set{}));
    t.is_true(hw.is(ModeTest{}));
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x42));
}

void test_modify_multiple_fields(TestContext& t) {
    MockTransport hw;

    // Start from known state
    hw.poke<uint32_t>(0x04, 0U);

    // Modify both enable and mode in one RMW operation
    hw.modify(ConfigEnable::Set{}, ModeFast{});
    t.is_true(hw.is(ConfigEnable::Set{}));
    t.is_true(hw.is(ModeFast{}));
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0));
}

// =============================================================================
// Multi-field write (from reset value)
// =============================================================================

void test_multi_field_write(TestContext& t) {
    const MockTransport hw;

    // Write multiple fields at once — starts from reset value
    hw.write(ConfigEnable::Set{}, ModeTest{}, ConfigPrescaler::value(static_cast<uint8_t>(0x55)));
    t.is_true(hw.is(ConfigEnable::Set{}));
    t.is_true(hw.is(ModeTest{}));
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x55));
}

// =============================================================================
// Value enumeration and is()
// =============================================================================

void test_enum_value_write_and_check(TestContext& t) {
    const MockTransport hw;

    hw.write(ConfigEnable::Set{}, ModeLowPower{});
    t.is_true(hw.is(ModeLowPower{}));
    t.is_true(!hw.is(ModeFast{}));
    t.is_true(hw.is(ConfigEnable::Set{}));
    t.is_true(!hw.is(ConfigEnable::Reset{}));
}

void test_dynamic_value_is(TestContext& t) {
    const MockTransport hw;

    hw.write(ConfigPrescaler::value(static_cast<uint8_t>(100)));
    t.is_true(hw.is(ConfigPrescaler::value(static_cast<uint8_t>(100))));
    t.is_true(!hw.is(ConfigPrescaler::value(static_cast<uint8_t>(99))));
}

// =============================================================================
// 1-bit field flip
// =============================================================================

void test_flip_1bit_field(TestContext& t) {
    MockTransport hw;
    hw.poke<uint32_t>(0x04, 0U);
    // Initially disabled
    t.is_true(hw.is(ConfigEnable::Reset{}));

    // Flip → should be 1
    hw.flip(ConfigEnable{});
    t.is_true(hw.is(ConfigEnable::Set{}));

    // Flip again → should be 0
    hw.flip(ConfigEnable{});
    t.is_true(hw.is(ConfigEnable::Reset{}));
}

void test_flip_preserves_other_bits(TestContext& t) {
    MockTransport hw;

    // Set prescaler, mode, but enable = 0
    uint32_t const init = (2U << 1) | (0x42U << 8);
    hw.poke<uint32_t>(0x04, init);

    hw.flip(ConfigEnable{});
    t.is_true(hw.is(ConfigEnable::Set{}));
    t.is_true(hw.is(ModeLowPower{}));
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x42));
}

// =============================================================================
// Practical: peripheral initialization sequence
// =============================================================================

void test_peripheral_init_sequence(TestContext& t) {
    const MockTransport hw;

    // Step 1: Configure prescaler and mode (peripheral is disabled)
    hw.write(ConfigPrescaler::value(static_cast<uint8_t>(0x10)), ModeFast{});
    t.is_true(hw.is(ConfigEnable::Reset{}));
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x10));
    t.is_true(hw.is(ModeFast{}));

    // Step 2: Enable IRQ and select channel 3
    hw.write(CtrlIrqEn::Set{}, CtrlChannel::value(static_cast<uint8_t>(3)));
    t.eq(hw.read(CtrlChannel{}).bits(), static_cast<uint8_t>(3));
    t.is_true(hw.is(CtrlIrqEn::Set{}));

    // Step 3: Enable peripheral
    hw.modify(ConfigEnable::Set{});
    t.is_true(hw.is(ConfigEnable::Set{}));
    // Prescaler and mode should be preserved
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x10));
    t.is_true(hw.is(ModeFast{}));

    // Step 4: Start operation
    hw.modify(CtrlStart::Set{});
    t.is_true(hw.is(CtrlStart::Set{}));
    t.is_true(hw.is(CtrlIrqEn::Set{}));
    t.eq(hw.read(CtrlChannel{}).bits(), static_cast<uint8_t>(3));
}

// =============================================================================
// RegionValue — get() and is()
// =============================================================================

void test_register_reader_get(TestContext& t) {
    MockTransport hw;

    // Set up register with known bit pattern:
    // bits 0: enable = 1
    // bits 1-2: mode = 0b01 (FAST)
    // bits 8-15: prescaler = 0x12
    hw.poke<uint32_t>(0x04, 0x0000'1203U);
    auto cfg = hw.read(ConfigReg{});
    t.is_true(cfg.is(ConfigEnable::Set{}));
    t.is_true(cfg.is(ModeFast{}));
    t.eq(cfg.get(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x12));
}

void test_register_reader_is(TestContext& t) {
    MockTransport hw;

    // enable=1, mode=FAST(01), prescaler=0x10
    hw.poke<uint32_t>(0x04, 0x0000'1003U);
    auto cfg = hw.read(ConfigReg{});
    t.is_true(cfg.is(ConfigEnable::Set{}));
    t.is_true(cfg.is(ModeFast{}));
    t.is_true(!cfg.is(ModeNormal{}));
}

// =============================================================================
// 1-bit field with custom Value aliases
// =============================================================================

void test_1bit_custom_value_write(TestContext& t) {
    const MockTransport hw;

    // Write using custom alias
    hw.write(CtrlIrqEn::Enabled{}, CtrlChannel::value(static_cast<uint8_t>(5)));
    t.is_true(hw.is(CtrlIrqEn::Enabled{}));
    t.is_true(hw.is(CtrlIrqEn::Set{})); // auto-generated alias works too
    t.eq(hw.read(CtrlChannel{}).bits(), static_cast<uint8_t>(5));
}

void test_1bit_custom_value_modify(TestContext& t) {
    MockTransport hw;
    hw.poke<uint16_t>(0x0C, 0U);

    hw.modify(CtrlIrqEn::Enabled{});
    t.is_true(hw.is(CtrlIrqEn::Enabled{}));

    hw.modify(CtrlIrqEn::Disabled{});
    t.is_true(hw.is(CtrlIrqEn::Disabled{}));
    t.is_true(hw.is(CtrlIrqEn::Reset{})); // Disabled == Reset (both value 0)
}

// =============================================================================
// dispatch — runtime-to-compile-time bridge
// =============================================================================

void test_dispatch_basic(TestContext& t) {
    std::size_t called_with = 999;
    dispatch<4>(2, [&]<std::size_t I>() { called_with = I; });
    t.eq(called_with, std::size_t{2});
}

void test_dispatch_boundary(TestContext& t) {
    std::size_t called_with = 999;
    dispatch<4>(3, [&]<std::size_t I>() { called_with = I; });
    t.eq(called_with, std::size_t{3});
}

void test_dispatch_index_zero(TestContext& t) {
    std::size_t called_with = 999;
    dispatch<4>(0, [&]<std::size_t I>() { called_with = I; });
    t.eq(called_with, std::size_t{0});
}

void test_dispatch_out_of_range_ignore(TestContext& t) {
    bool called = false;
    dispatch<4, IgnoreError>(4, [&]<std::size_t I>() { called = true; });
    t.is_true(!called);
}

void test_dispatch_zero_range(TestContext& t) {
    // dispatch<0>() — empty range: any index is out of range, fn never called
    bool called = false;
    dispatch<0, IgnoreError>(0, [&]<std::size_t I>() { called = true; });
    t.is_true(!called);
}

void test_dispatch_r_zero_range(TestContext& t) {
    // dispatch_r<0>() — empty range: returns default_val
    auto result = dispatch_r<0, int, IgnoreError>(0, []<std::size_t I>() -> int { return 42; }, -1);
    t.eq(result, -1);
}

void test_dispatch_r_basic(TestContext& t) {
    auto result = dispatch_r<4, int>(2, []<std::size_t I>() -> int { return static_cast<int>(I * 10); });
    t.eq(result, 20);
}

void test_dispatch_r_out_of_range(TestContext& t) {
    auto result = dispatch_r<4, int, IgnoreError>(4, []<std::size_t I>() -> int { return static_cast<int>(I); }, -1);
    t.eq(result, -1);
}

// =============================================================================
// RegisterArray — metadata type
// =============================================================================

struct ArrayDevice : Device<> {
    static constexpr Addr base_address = 0x1000;
};

template <std::size_t N>
struct ArrayReg : Register<ArrayDevice, 0x10 + (N * 4), bits32> {
    static_assert(N < 8);
};

using TestRegArray = RegisterArray<ArrayReg, 8>;

void test_register_array_size(TestContext& t) {
    t.eq(TestRegArray::size, std::size_t{8});
}

void test_register_array_element_address(TestContext& t) {
    t.eq(TestRegArray::Element<0>::address, static_cast<Addr>(0x1010));
    t.eq(TestRegArray::Element<3>::address, static_cast<Addr>(0x101C));
    t.eq(TestRegArray::Element<7>::address, static_cast<Addr>(0x102C));
}

void test_register_array_dispatch(TestContext& t) {
    Addr addr = 0;
    dispatch<TestRegArray::size>(5, [&]<std::size_t I>() { addr = TestRegArray::Element<I>::address; });
    t.eq(addr, static_cast<Addr>(0x1024));
}

// =============================================================================
// IndexedArray — Entry type address calculation
// =============================================================================

using TestIdxArray8 = IndexedArray<ArrayDevice, 0x100, 32>;
using TestIdxArray16 = IndexedArray<ArrayDevice, 0x200, 16, bits16>;
using TestIdxArray16Stride4 = IndexedArray<ArrayDevice, 0x300, 8, bits16, 4>;

void test_indexed_array_size(TestContext& t) {
    t.eq(TestIdxArray8::size, std::size_t{32});
    t.eq(TestIdxArray16::size, std::size_t{16});
}

void test_indexed_array_entry_width(TestContext& t) {
    t.eq(TestIdxArray8::entry_width, std::size_t{8});
    t.eq(TestIdxArray16::entry_width, std::size_t{16});
}

void test_indexed_array_stride(TestContext& t) {
    t.eq(TestIdxArray8::stride, std::size_t{1});
    t.eq(TestIdxArray16::stride, std::size_t{2});
    t.eq(TestIdxArray16Stride4::stride, std::size_t{4});
}

void test_indexed_array_entry_address_8bit(TestContext& t) {
    t.eq(TestIdxArray8::Entry<0>::address, static_cast<Addr>(0x1100));
    t.eq(TestIdxArray8::Entry<1>::address, static_cast<Addr>(0x1101));
    t.eq(TestIdxArray8::Entry<31>::address, static_cast<Addr>(0x111F));
}

void test_indexed_array_entry_address_16bit(TestContext& t) {
    t.eq(TestIdxArray16::Entry<0>::address, static_cast<Addr>(0x1200));
    t.eq(TestIdxArray16::Entry<1>::address, static_cast<Addr>(0x1202));
    t.eq(TestIdxArray16::Entry<15>::address, static_cast<Addr>(0x121E));
}

void test_indexed_array_entry_address_stride(TestContext& t) {
    t.eq(TestIdxArray16Stride4::Entry<0>::address, static_cast<Addr>(0x1300));
    t.eq(TestIdxArray16Stride4::Entry<1>::address, static_cast<Addr>(0x1304));
    t.eq(TestIdxArray16Stride4::Entry<7>::address, static_cast<Addr>(0x131C));
}

// =============================================================================
// IndexedArray — runtime bounds error policy
// =============================================================================

/// @brief Device for IndexedArray runtime bounds testing (non-zero base for
///        compile-time address computation, but runtime read/write is tested
///        via error policy — the volatile pointer is never dereferenced because
///        IgnoreError returns before the access).
using BoundsIdxArray = IndexedArray<ArrayDevice, 0x100, 4, bits8>;

void test_indexed_array_write_entry_oob(TestContext& t) {
    // Out-of-range write_entry with IgnoreError — silently returns, no crash
    BoundsIdxArray::write_entry<IgnoreError>(4, 0xFF);
    // If we reach here, IgnoreError correctly prevented the OOB access
    t.is_true(true);
}

void test_indexed_array_read_entry_oob(TestContext& t) {
    // Out-of-range read_entry with IgnoreError — returns 0, no crash
    auto val = BoundsIdxArray::read_entry<IgnoreError>(4);
    t.eq(val, static_cast<std::uint8_t>(0));
}

void test_indexed_array_write_entry_oob_custom_handler(TestContext& t) {
    // CustomErrorHandler captures the error message
    static bool called = false;
    static const char* captured_msg = nullptr;
    called = false;
    captured_msg = nullptr;

    auto reset = []() {
        called = false;
        captured_msg = nullptr;
    };
    (void)reset;

    using Handler = CustomErrorHandler<+[](const char* msg) {
        called = true;
        captured_msg = msg;
    }>;

    BoundsIdxArray::write_entry<Handler>(99, 0x42);
    t.is_true(called);
    // Verify error message is not null
    t.is_true(captured_msg != nullptr);
}

void test_indexed_array_read_entry_oob_custom_handler(TestContext& t) {
    static bool called = false;
    called = false;

    using Handler = CustomErrorHandler<+[](const char* /*msg*/) { called = true; }>;

    auto val = BoundsIdxArray::read_entry<Handler>(4);
    t.is_true(called);
    t.eq(val, static_cast<std::uint8_t>(0));
}

} // namespace

inline void register_register_field_tests(umi::test::Suite& suite) {
    suite.section("Register read/write");
    suite.run("write and read back", test_register_write_and_read);
    suite.run("write zero", test_register_write_zero);
    suite.run("16-bit register", test_register_16bit_write_read);

    suite.section("Field read/write");
    suite.run("write single field", test_field_write_single);
    suite.run("field extraction", test_field_read_extraction);
    suite.run("ctrl reg fields", test_field_read_ctrl_reg);

    suite.section("Read-modify-write");
    suite.run("modify single field", test_modify_single_field);
    suite.run("preserves other fields", test_modify_preserves_other_fields);
    suite.run("modify multiple fields", test_modify_multiple_fields);

    suite.section("Multi-field write");
    suite.run("write multiple fields", test_multi_field_write);

    suite.section("Value/is()");
    suite.run("enum value write+check", test_enum_value_write_and_check);
    suite.run("dynamic value is()", test_dynamic_value_is);

    suite.section("1-bit flip");
    suite.run("toggle enable", test_flip_1bit_field);
    suite.run("flip preserves neighbors", test_flip_preserves_other_bits);

    suite.section("Practical workflow");
    suite.run("peripheral init sequence", test_peripheral_init_sequence);

    suite.section("RegionValue");
    suite.run("get() field extraction", test_register_reader_get);
    suite.run("is() value matching", test_register_reader_is);

    suite.section("1-bit custom aliases");
    suite.run("write with custom alias", test_1bit_custom_value_write);
    suite.run("modify with custom alias", test_1bit_custom_value_modify);

    suite.section("dispatch");
    suite.run("basic dispatch", test_dispatch_basic);
    suite.run("boundary index", test_dispatch_boundary);
    suite.run("index zero", test_dispatch_index_zero);
    suite.run("out of range (IgnoreError)", test_dispatch_out_of_range_ignore);
    suite.run("zero range N=0", test_dispatch_zero_range);
    suite.run("dispatch_r zero range N=0", test_dispatch_r_zero_range);
    suite.run("dispatch_r basic", test_dispatch_r_basic);
    suite.run("dispatch_r out of range", test_dispatch_r_out_of_range);

    suite.section("RegisterArray");
    suite.run("size", test_register_array_size);
    suite.run("element address", test_register_array_element_address);
    suite.run("dispatch with array", test_register_array_dispatch);

    suite.section("IndexedArray");
    suite.run("size", test_indexed_array_size);
    suite.run("entry width", test_indexed_array_entry_width);
    suite.run("stride", test_indexed_array_stride);
    suite.run("8-bit entry address", test_indexed_array_entry_address_8bit);
    suite.run("16-bit entry address", test_indexed_array_entry_address_16bit);
    suite.run("custom stride address", test_indexed_array_entry_address_stride);

    suite.section("IndexedArray runtime bounds");
    suite.run("write_entry OOB (IgnoreError)", test_indexed_array_write_entry_oob);
    suite.run("read_entry OOB (IgnoreError)", test_indexed_array_read_entry_oob);
    suite.run("write_entry OOB (CustomErrorHandler)", test_indexed_array_write_entry_oob_custom_handler);
    suite.run("read_entry OOB (CustomErrorHandler)", test_indexed_array_read_entry_oob_custom_handler);
}

} // namespace umimmio::test
