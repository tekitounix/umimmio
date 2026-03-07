// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Register/Field/Value operations — practical workflow tests.
/// @author Shota Moriguchi @tekitounix
/// @details Simulates real-world register operations on a mock device:
///          reading, writing, read-modify-write, field extraction, multi-field
///          writes, and 1-bit field operations.

#include "test_fixture.hh"

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
    // Also verify via field read
    t.eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
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
    t.eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    t.eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(2));
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
    t.eq(hw.read(CtrlStart{}).bits(), static_cast<uint8_t>(1));
    t.eq(hw.read(CtrlIrqEn{}).bits(), static_cast<uint8_t>(1));
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
    t.eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x10));
    t.eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(0));
}

void test_modify_preserves_other_fields(TestContext& t) {
    MockTransport hw;

    // Initial: all fields set
    uint32_t const init = 1U | (3U << 1) | (0xFFU << 8);
    hw.poke<uint32_t>(0x04, init);

    // Modify just prescaler to 0x42
    hw.modify(ConfigPrescaler::value(static_cast<uint8_t>(0x42)));
    t.eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    t.eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(3));
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x42));
}

void test_modify_multiple_fields(TestContext& t) {
    MockTransport hw;

    // Start from known state
    hw.poke<uint32_t>(0x04, 0U);

    // Modify both enable and mode in one RMW operation
    hw.modify(ConfigEnable::Set{}, ModeFast{});
    t.eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    t.eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(ModeVal::FAST));
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0));
}

// =============================================================================
// Multi-field write (from reset value)
// =============================================================================

void test_multi_field_write(TestContext& t) {
    const MockTransport hw;

    // Write multiple fields at once — starts from reset value
    hw.write(ConfigEnable::Set{}, ModeTest{}, ConfigPrescaler::value(static_cast<uint8_t>(0x55)));
    t.eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    t.eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(ModeVal::TEST));
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
    t.eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(0));

    // Flip → should be 1
    hw.flip(ConfigEnable{});
    t.eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));

    // Flip again → should be 0
    hw.flip(ConfigEnable{});
    t.eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(0));
}

void test_flip_preserves_other_bits(TestContext& t) {
    MockTransport hw;

    // Set prescaler, mode, but enable = 0
    uint32_t const init = (2U << 1) | (0x42U << 8);
    hw.poke<uint32_t>(0x04, init);

    hw.flip(ConfigEnable{});
    t.eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    t.eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(2));
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x42));
}

// =============================================================================
// Practical: peripheral initialization sequence
// =============================================================================

void test_peripheral_init_sequence(TestContext& t) {
    const MockTransport hw;

    // Step 1: Configure prescaler and mode (peripheral is disabled)
    hw.write(ConfigPrescaler::value(static_cast<uint8_t>(0x10)), ModeFast{});
    t.eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(0));
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x10));
    t.is_true(hw.is(ModeFast{}));

    // Step 2: Enable IRQ and select channel 3
    hw.write(CtrlIrqEn::Set{}, CtrlChannel::value(static_cast<uint8_t>(3)));
    t.eq(hw.read(CtrlChannel{}).bits(), static_cast<uint8_t>(3));
    t.eq(hw.read(CtrlIrqEn{}).bits(), static_cast<uint8_t>(1));

    // Step 3: Enable peripheral
    hw.modify(ConfigEnable::Set{});
    t.eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    // Prescaler and mode should be preserved
    t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x10));
    t.is_true(hw.is(ModeFast{}));

    // Step 4: Start operation
    hw.modify(CtrlStart::Set{});
    t.eq(hw.read(CtrlStart{}).bits(), static_cast<uint8_t>(1));
    t.eq(hw.read(CtrlIrqEn{}).bits(), static_cast<uint8_t>(1));
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
    t.eq(cfg.get(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    t.eq(cfg.get(ConfigMode{}).bits(), static_cast<uint8_t>(1));
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

} // namespace

void run_register_field_tests(umi::test::Suite& suite) {
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
}

} // namespace umimmio::test
