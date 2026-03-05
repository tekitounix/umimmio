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

bool test_register_write_and_read(TestContext& t) {
    MockTransport hw;

    // Write a known value and read it back
    hw.write(DataReg::value(0xDEAD'BEEFU));
    auto val = hw.read(DataReg{});

    return t.assert_eq(val.bits(), static_cast<uint32_t>(0xDEAD'BEEF));
}

bool test_register_write_zero(TestContext& t) {
    MockTransport hw;

    hw.write(DataReg::value(0xFFFF'FFFFU));
    hw.write(DataReg::value(0U));

    return t.assert_eq(hw.read(DataReg{}).bits(), static_cast<uint32_t>(0));
}

bool test_register_16bit_write_read(TestContext& t) {
    MockTransport hw;

    hw.write(CtrlReg::value(static_cast<uint16_t>(0x1234)));
    auto val = hw.read(CtrlReg{});

    return t.assert_eq(val.bits(), static_cast<uint16_t>(0x1234));
}

// =============================================================================
// Field read/write
// =============================================================================

bool test_field_write_single(TestContext& t) {
    MockTransport hw;

    // Write just the enable bit
    hw.write(ConfigEnable::Set{});

    bool ok = true;
    // Enable bit should be set (bit 0)
    auto reg_val = hw.read(ConfigReg{});
    ok &= t.assert_true((reg_val.bits() & 1U) == 1U, "enable bit set");
    // Also verify via field read
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    return ok;
}

bool test_field_read_extraction(TestContext& t) {
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

    bool ok = true;
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(2));
    ok &= t.assert_eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0xAB));
    return ok;
}

bool test_field_read_ctrl_reg(TestContext& t) {
    MockTransport hw;

    // CtrlReg at 0x0C, 16-bit
    // bit 0: start, bit 1: irq_en, bits 4-7: channel
    uint16_t raw = 0U;
    raw |= 1U;           // start
    raw |= (1U << 1);    // irq_en
    raw |= (0x0FU << 4); // channel = 15
    hw.poke<uint16_t>(0x0C, raw);

    bool ok = true;
    ok &= t.assert_eq(hw.read(CtrlStart{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(CtrlIrqEn{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(CtrlChannel{}).bits(), static_cast<uint8_t>(0x0F));
    return ok;
}

// =============================================================================
// Read-Modify-Write (modify)
// =============================================================================

bool test_modify_single_field(TestContext& t) {
    MockTransport hw;

    // Set initial config: prescaler = 0x10, mode = NORMAL, enable = 0
    uint32_t const init = (0x10U << 8);
    hw.poke<uint32_t>(0x04, init);

    // Modify only enable — prescaler and mode should be preserved
    hw.modify(ConfigEnable::Set{});

    bool ok = true;
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x10));
    ok &= t.assert_eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(0));
    return ok;
}

bool test_modify_preserves_other_fields(TestContext& t) {
    MockTransport hw;

    // Initial: all fields set
    uint32_t const init = 1U | (3U << 1) | (0xFFU << 8);
    hw.poke<uint32_t>(0x04, init);

    // Modify just prescaler to 0x42
    hw.modify(ConfigPrescaler::value(static_cast<uint8_t>(0x42)));

    bool ok = true;
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(3));
    ok &= t.assert_eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x42));
    return ok;
}

bool test_modify_multiple_fields(TestContext& t) {
    MockTransport hw;

    // Start from known state
    hw.poke<uint32_t>(0x04, 0U);

    // Modify both enable and mode in one RMW operation
    hw.modify(ConfigEnable::Set{}, ModeFast{});

    bool ok = true;
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(ModeVal::FAST));
    ok &= t.assert_eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0));
    return ok;
}

// =============================================================================
// Multi-field write (from reset value)
// =============================================================================

bool test_multi_field_write(TestContext& t) {
    MockTransport hw;

    // Write multiple fields at once — starts from reset value
    hw.write(ConfigEnable::Set{}, ModeTest{}, ConfigPrescaler::value(static_cast<uint8_t>(0x55)));

    bool ok = true;
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(ModeVal::TEST));
    ok &= t.assert_eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x55));
    return ok;
}

// =============================================================================
// Value enumeration and is()
// =============================================================================

bool test_enum_value_write_and_check(TestContext& t) {
    MockTransport hw;

    hw.write(ConfigEnable::Set{}, ModeLowPower{});

    bool ok = true;
    ok &= t.assert_true(hw.is(ModeLowPower{}), "mode is LOW_POWER");
    ok &= t.assert_true(!hw.is(ModeFast{}), "mode is not FAST");
    ok &= t.assert_true(hw.is(ConfigEnable::Set{}), "enable is set");
    ok &= t.assert_true(!hw.is(ConfigEnable::Reset{}), "enable is not reset");
    return ok;
}

bool test_dynamic_value_is(TestContext& t) {
    MockTransport hw;

    hw.write(ConfigPrescaler::value(static_cast<uint8_t>(100)));

    bool ok = true;
    ok &= t.assert_true(hw.is(ConfigPrescaler::value(static_cast<uint8_t>(100))), "prescaler is 100");
    ok &= t.assert_true(!hw.is(ConfigPrescaler::value(static_cast<uint8_t>(99))), "prescaler is not 99");
    return ok;
}

// =============================================================================
// 1-bit field flip
// =============================================================================

bool test_flip_1bit_field(TestContext& t) {
    MockTransport hw;
    hw.poke<uint32_t>(0x04, 0U);

    bool ok = true;
    // Initially disabled
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(0));

    // Flip → should be 1
    hw.flip(ConfigEnable{});
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));

    // Flip again → should be 0
    hw.flip(ConfigEnable{});
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(0));
    return ok;
}

bool test_flip_preserves_other_bits(TestContext& t) {
    MockTransport hw;

    // Set prescaler, mode, but enable = 0
    uint32_t const init = (2U << 1) | (0x42U << 8);
    hw.poke<uint32_t>(0x04, init);

    hw.flip(ConfigEnable{});

    bool ok = true;
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(2));
    ok &= t.assert_eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x42));
    return ok;
}

// =============================================================================
// Practical: peripheral initialization sequence
// =============================================================================

bool test_peripheral_init_sequence(TestContext& t) {
    MockTransport hw;

    // Step 1: Configure prescaler and mode (peripheral is disabled)
    hw.write(ConfigPrescaler::value(static_cast<uint8_t>(0x10)), ModeFast{});

    bool ok = true;
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(0));
    ok &= t.assert_eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x10));
    ok &= t.assert_true(hw.is(ModeFast{}), "mode is FAST");

    // Step 2: Enable IRQ and select channel 3
    hw.write(CtrlIrqEn::Set{}, CtrlChannel::value(static_cast<uint8_t>(3)));
    ok &= t.assert_eq(hw.read(CtrlChannel{}).bits(), static_cast<uint8_t>(3));
    ok &= t.assert_eq(hw.read(CtrlIrqEn{}).bits(), static_cast<uint8_t>(1));

    // Step 3: Enable peripheral
    hw.modify(ConfigEnable::Set{});
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    // Prescaler and mode should be preserved
    ok &= t.assert_eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x10));
    ok &= t.assert_true(hw.is(ModeFast{}), "mode still FAST");

    // Step 4: Start operation
    hw.modify(CtrlStart::Set{});
    ok &= t.assert_eq(hw.read(CtrlStart{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(CtrlIrqEn{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(CtrlChannel{}).bits(), static_cast<uint8_t>(3));

    return ok;
}

// =============================================================================
// RegisterReader — get() and is()
// =============================================================================

bool test_register_reader_get(TestContext& t) {
    MockTransport hw;

    // Set up register with known bit pattern:
    // bits 0: enable = 1
    // bits 1-2: mode = 0b01 (FAST)
    // bits 8-15: prescaler = 0x12
    hw.poke<uint32_t>(0x04, 0x0000'1203U);
    auto cfg = hw.read(ConfigReg{});

    bool ok = true;
    ok &= t.assert_eq(cfg.get(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(cfg.get(ConfigMode{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(cfg.get(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x12));
    return ok;
}

bool test_register_reader_is(TestContext& t) {
    MockTransport hw;

    // enable=1, mode=FAST(01), prescaler=0x10
    hw.poke<uint32_t>(0x04, 0x0000'1003U);
    auto cfg = hw.read(ConfigReg{});

    bool ok = true;
    ok &= t.assert_true(cfg.is(ConfigEnable::Set{}), "enable bit should be set");
    ok &= t.assert_true(cfg.is(ModeFast{}), "mode should be FAST");
    ok &= t.assert_true(!cfg.is(ModeNormal{}), "mode should not be NORMAL");
    return ok;
}

} // namespace

void run_register_field_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("Register read/write");
    suite.run("write and read back", test_register_write_and_read);
    suite.run("write zero", test_register_write_zero);
    suite.run("16-bit register", test_register_16bit_write_read);

    umi::test::Suite::section("Field read/write");
    suite.run("write single field", test_field_write_single);
    suite.run("field extraction", test_field_read_extraction);
    suite.run("ctrl reg fields", test_field_read_ctrl_reg);

    umi::test::Suite::section("Read-modify-write");
    suite.run("modify single field", test_modify_single_field);
    suite.run("preserves other fields", test_modify_preserves_other_fields);
    suite.run("modify multiple fields", test_modify_multiple_fields);

    umi::test::Suite::section("Multi-field write");
    suite.run("write multiple fields", test_multi_field_write);

    umi::test::Suite::section("Value/is()");
    suite.run("enum value write+check", test_enum_value_write_and_check);
    suite.run("dynamic value is()", test_dynamic_value_is);

    umi::test::Suite::section("1-bit flip");
    suite.run("toggle enable", test_flip_1bit_field);
    suite.run("flip preserves neighbors", test_flip_preserves_other_bits);

    umi::test::Suite::section("Practical workflow");
    suite.run("peripheral init sequence", test_peripheral_init_sequence);

    umi::test::Suite::section("RegisterReader");
    suite.run("get() field extraction", test_register_reader_get);
    suite.run("is() value matching", test_register_reader_is);
}

} // namespace umimmio::test
