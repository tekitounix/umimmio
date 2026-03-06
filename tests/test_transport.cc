// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Transport layer tests — MockTransport, ByteAdapter with mock I2C.
/// @author Shota Moriguchi @tekitounix
/// @details Tests both direct (RAM-backed) and byte-oriented transports.

#include <array>
#include <cstring>
#include <span>
#include <variant>

#include <umimmio/transport/i2c.hh>

#include "test_fixture.hh"

namespace umimmio::test {
namespace {

using umi::test::TestContext;

// =============================================================================
// Type traits and compile-time properties
// =============================================================================

bool test_uint_fit_sizes(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(sizeof(UintFit<8>), 1U);
    ok &= t.assert_eq(sizeof(UintFit<16>), 2U);
    ok &= t.assert_eq(sizeof(UintFit<32>), 4U);
    ok &= t.assert_eq(sizeof(UintFit<64>), 8U);
    // Edge: 1-bit fits in uint8_t
    ok &= t.assert_eq(sizeof(UintFit<1>), 1U);
    ok &= t.assert_eq(sizeof(UintFit<7>), 1U);
    ok &= t.assert_eq(sizeof(UintFit<9>), 2U);
    ok &= t.assert_eq(sizeof(UintFit<33>), 8U);
    return ok;
}

bool test_access_policy_traits(TestContext& t) {
    bool ok = true;
    ok &= t.assert_true(RW::can_read && RW::can_write);
    ok &= t.assert_true(RO::can_read && !RO::can_write);
    ok &= t.assert_true(!WO::can_read && WO::can_write);
    return ok;
}

bool test_register_static_properties(TestContext& t) {
    bool ok = true;

    // StatusReg: offset 0x00, 32-bit, reset = 0x0001
    ok &= t.assert_eq(StatusReg::address, static_cast<Addr>(0x00));
    ok &= t.assert_eq(StatusReg::bit_width, 32U);
    ok &= t.assert_eq(StatusReg::reset_value(), static_cast<uint32_t>(0x0001));
    ok &= t.assert_true(StatusReg::AccessType::can_read);
    ok &= t.assert_true(!StatusReg::AccessType::can_write);

    // ConfigReg: offset 0x04, 32-bit, reset = 0xFF00
    ok &= t.assert_eq(ConfigReg::address, static_cast<Addr>(0x04));
    ok &= t.assert_eq(ConfigReg::reset_value(), static_cast<uint32_t>(0xFF00));

    // CtrlReg: offset 0x0C, 16-bit
    ok &= t.assert_eq(CtrlReg::address, static_cast<Addr>(0x0C));
    ok &= t.assert_eq(CtrlReg::bit_width, 16U);

    return ok;
}

bool test_field_static_properties(TestContext& t) {
    bool ok = true;

    // ConfigEnable: bit 0, width 1
    ok &= t.assert_eq(ConfigEnable::shift, 0U);
    ok &= t.assert_eq(ConfigEnable::bit_width, 1U);
    ok &= t.assert_eq(ConfigEnable::mask(), static_cast<uint32_t>(0x1));

    // ConfigMode: bit 1, width 2
    ok &= t.assert_eq(ConfigMode::shift, 1U);
    ok &= t.assert_eq(ConfigMode::bit_width, 2U);
    ok &= t.assert_eq(ConfigMode::mask(), static_cast<uint32_t>(0x6));

    // ConfigPrescaler: bit 8, width 8
    ok &= t.assert_eq(ConfigPrescaler::shift, 8U);
    ok &= t.assert_eq(ConfigPrescaler::bit_width, 8U);
    ok &= t.assert_eq(ConfigPrescaler::mask(), static_cast<uint32_t>(0xFF00));

    // CtrlChannel: bit 4, width 4
    ok &= t.assert_eq(CtrlChannel::shift, 4U);
    ok &= t.assert_eq(CtrlChannel::bit_width, 4U);
    ok &= t.assert_eq(CtrlChannel::mask(), static_cast<uint16_t>(0xF0));

    return ok;
}

bool test_value_static_properties(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(ConfigEnable::Set::value, static_cast<uint8_t>(1));
    ok &= t.assert_eq(ConfigEnable::Reset::value, static_cast<uint8_t>(0));
    ok &= t.assert_eq(ModeNormal::value, static_cast<uint8_t>(ModeVal::NORMAL));
    ok &= t.assert_eq(ModeFast::value, static_cast<uint8_t>(ModeVal::FAST));
    return ok;
}

// =============================================================================
// MockTransport direct buffer access
// =============================================================================

bool test_mock_transport_peek_poke(TestContext& t) {
    MockTransport hw;

    hw.poke<uint32_t>(0x00, 0x12345678U);
    bool ok = t.assert_eq(hw.peek<uint32_t>(0x00), 0x12345678U);

    hw.poke<uint16_t>(0x10, uint16_t{0xABCD});
    ok &= t.assert_eq(hw.peek<uint16_t>(0x10), static_cast<uint16_t>(0xABCD));

    hw.clear_memory();
    ok &= t.assert_eq(hw.peek<uint32_t>(0x00), 0U);
    return ok;
}

// =============================================================================
// Mock I2C driver for ByteAdapter testing
// =============================================================================

/// @brief Minimal I2C driver mock that stores register data in RAM.
struct MockI2C {
    mutable std::array<uint8_t, 256> memory{};

    struct Result {
        explicit operator bool() const { return success; }
        bool success = true;
    };

    Result write(uint8_t /*dev_addr*/, std::span<const uint8_t> data) const {
        if (data.size() < 2) {
            return {false};
        }
        uint8_t const reg_addr = data[0];
        std::memcpy(&memory[reg_addr], data.data() + 1, data.size() - 1);
        return {true};
    }

    Result write_read(uint8_t /*dev_addr*/, std::span<const uint8_t> tx, std::span<uint8_t> rx) const {
        if (tx.empty()) {
            return {false};
        }
        uint8_t const reg_addr = tx[0];
        std::memcpy(rx.data(), &memory[reg_addr], rx.size());
        return {true};
    }
};

/// @brief I2C device for testing — allows I2c
struct I2CDevice : Device<RW, I2c> {};

/// @brief Register on I2C device at offset 0x10
struct I2CReg : Register<I2CDevice, 0x10, bits32, RW, 0> {};
struct I2CField : Field<I2CReg, 0, 8, Numeric> {};

bool test_i2c_transport_write_read(TestContext& t) {
    MockI2C i2c;
    const I2cTransport<MockI2C> transport(i2c, 0x50);

    // Write a value through I2C transport
    transport.write(I2CReg::value(0xDEAD'BEEFU));

    // Read it back
    auto val = transport.read(I2CReg{});

    return t.assert_eq(val.bits(), static_cast<uint32_t>(0xDEAD'BEEF));
}

bool test_i2c_transport_field_operations(TestContext& t) {
    MockI2C i2c;
    const I2cTransport<MockI2C> transport(i2c, 0x50);

    // Write via register
    transport.write(I2CReg::value(0x00000042U));

    // Read field
    auto field_val = transport.read(I2CField{});
    return t.assert_eq(field_val.bits(), static_cast<uint8_t>(0x42));
}

bool test_i2c_transport_modify(TestContext& t) {
    MockI2C i2c;
    const I2cTransport<MockI2C> transport(i2c, 0x50);

    // Pre-load value
    transport.write(I2CReg::value(0xFF00'0000U));

    // Modify just the low 8 bits
    transport.modify(I2CField::value(static_cast<uint8_t>(0xAB)));

    bool ok = true;
    auto reg_val = transport.read(I2CReg{});
    // Low byte should be 0xAB, high bytes preserved
    ok &= t.assert_eq(reg_val.bits() & 0xFFU, 0xABU);
    ok &= t.assert_eq(reg_val.bits() & 0xFF00'0000U, 0xFF00'0000U);
    return ok;
}

// =============================================================================
// W1C clear() test
// =============================================================================

bool test_w1c_clear(TestContext& t) {
    MockTransport hw;

    hw.poke<uint32_t>(0x14, 0x0103U); // OVR=1, EOC=1, Enable=1
    hw.clear(W1cOvr{});
    // Mixed register (W1C + non-W1C): clear() uses RMW.
    // Read 0x0103, mask W1C bits (&~0x03 = 0x0100), set OVR (|0x01 = 0x0101).
    // Enable (bit 8) preserved. EOC (bit 1, W1C) not accidentally cleared.
    return t.assert_eq(hw.peek<uint32_t>(0x14), 0x0101U);
}

// =============================================================================
// reset() test
// =============================================================================

bool test_register_reset(TestContext& t) {
    MockTransport hw;

    hw.poke<uint32_t>(0x04, 0xAAAA'AAAA);
    hw.reset(ConfigReg{});
    return t.assert_eq(hw.peek<uint32_t>(0x04), static_cast<uint32_t>(0xFF00));
}

// =============================================================================
// W1C modify() safety — W1C bits should be masked in RMW
// =============================================================================

bool test_w1c_modify_safety(TestContext& t) {
    MockTransport hw;

    // Set OVR=1, EOC=1, Enable=1
    hw.poke<uint32_t>(0x14, 0x0103U);

    // Modify the Enable field (bit 8) — W1C bits (0,1) should be masked to 0
    hw.modify(W1cRegEnable::Set{});

    auto result = hw.peek<uint32_t>(0x14);
    bool ok = true;
    // Enable (bit 8) should be set
    ok &= t.assert_true((result & 0x0100U) != 0, "enable bit should be set");
    // W1C bits (0,1) should be 0 (masked by w1c_mask)
    ok &= t.assert_eq(result & 0x03U, 0U);
    return ok;
}

// =============================================================================
// read_variant() test
// =============================================================================

/// @brief Visitor helper for std::visit (C++17 pattern).
template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

bool test_read_variant_match(TestContext& t) {
    MockTransport hw;

    // Mode = FAST (bits 1-2 = 01) → value at offset 0x04 = 0x02
    hw.poke<uint32_t>(0x04, 0x02U);
    auto mode = hw.read_variant<ConfigMode, ModeNormal, ModeFast, ModeLowPower, ModeTest>();

    bool matched = false;
    std::visit(Overloaded{
                   [&](ModeFast) { matched = true; },
                   [&](auto) { matched = false; },
               },
               mode);
    return t.assert_true(matched, "should match ModeFast");
}

bool test_read_variant_unknown(TestContext& t) {
    MockTransport hw;

    // Set mode to a value not in the variant list
    hw.poke<uint32_t>(0x04, 0x06U); // bits 1-2 = 0b11 = TEST
    // Only list Normal and Fast — TEST should be UnknownValue
    auto mode = hw.read_variant<ConfigMode, ModeNormal, ModeFast>();

    bool is_unknown = false;
    std::visit(Overloaded{
                   [&](UnknownValue<ConfigMode> u) {
                       is_unknown = true;
                       // value should be 3 (TEST)
                       (void)u;
                   },
                   [&](auto) { is_unknown = false; },
               },
               mode);
    return t.assert_true(is_unknown, "should be UnknownValue");
}

// =============================================================================
// CustomErrorHandler test
// =============================================================================

bool custom_handler_called = false;
void custom_handler(const char* /*msg*/) noexcept {
    custom_handler_called = true;
}

/// @brief MockTransport variant that uses CustomErrorHandler for range errors.
struct CustomErrTransport : private RegOps<std::true_type, CustomErrorHandler<custom_handler>> {
  public:
    using RegOps<std::true_type, CustomErrorHandler<custom_handler>>::write;
    using RegOps<std::true_type, CustomErrorHandler<custom_handler>>::read;
    using RegOps<std::true_type, CustomErrorHandler<custom_handler>>::is;
    using TransportTag = Direct;

    std::array<std::uint8_t, 256> mutable memory{};

    template <typename Reg>
    auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        typename Reg::RegValueType val{};
        std::memcpy(&val, &memory[Reg::address], sizeof(val));
        return val;
    }

    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType val) const noexcept {
        std::memcpy(&memory[Reg::address], &val, sizeof(val));
    }
};

bool test_custom_error_handler_callback(TestContext& t) {
    const CustomErrTransport hw;
    // ConfigPrescaler is 8-bit (bits 8-15). Value 256 exceeds max (255).
    custom_handler_called = false;
    hw.write(DynamicValue<ConfigPrescaler, uint16_t>{256});
    return t.assert_true(custom_handler_called, "custom handler should be called");
}

// =============================================================================
// Multi-field modify with W1C register
// =============================================================================

bool test_w1c_modify_multi_field(TestContext& t) {
    MockTransport hw;

    // Set OVR=1, EOC=1, Enable=1, Mode=0
    hw.poke<uint32_t>(0x14, 0x0103U);

    // Modify both Enable and Mode at once — W1C bits (0,1) must be masked
    hw.modify(W1cRegEnable::Set{}, W1cRegMode::Set{});

    auto result = hw.peek<uint32_t>(0x14);
    bool ok = true;
    ok &= t.assert_true((result & 0x0100U) != 0, "enable bit set");
    ok &= t.assert_true((result & 0x0200U) != 0, "mode bit set");
    ok &= t.assert_eq(result & 0x03U, 0U);
    return ok;
}

// =============================================================================
// 8-bit register through RegOps
// =============================================================================

bool test_8bit_register_ops(TestContext& t) {
    const MockTransport hw;

    // Write full register
    hw.write(ByteReg::value(static_cast<uint8_t>(0x5A)));
    bool ok = t.assert_eq(hw.peek<uint8_t>(0x18), static_cast<uint8_t>(0x5A));

    // Read field
    auto low = hw.read(ByteLow{});
    ok &= t.assert_eq(low.bits(), static_cast<uint8_t>(0x0A));
    auto high = hw.read(ByteHigh{});
    ok &= t.assert_eq(high.bits(), static_cast<uint8_t>(0x05));

    // Modify single field (preserves other nibble)
    hw.modify(ByteLow::value(static_cast<uint8_t>(0x0F)));
    ok &= t.assert_eq(hw.peek<uint8_t>(0x18), static_cast<uint8_t>(0x5F));

    // Reset
    hw.reset(ByteReg{});
    ok &= t.assert_eq(hw.peek<uint8_t>(0x18), static_cast<uint8_t>(0xA5));

    return ok;
}

// =============================================================================
// flip() in W1C-containing register — W1C bits must be masked
// =============================================================================

/// @brief Verifies that flip() on a non-W1C field in a W1C register masks W1C bits.
/// This is the edge case that caught Bug #1: without W1C masking, flip() would
/// write back 1s to W1C flag bits, accidentally clearing hardware flags.
bool test_flip_in_w1c_register(TestContext& t) {
    MockTransport hw;

    // OVR=1, EOC=1, Enable=0 → raw = 0x0003
    hw.poke<uint32_t>(0x14, 0x0003U);

    // flip(Enable) should:
    //   1. Read 0x0003
    //   2. XOR Enable mask (0x0100) → 0x0103
    //   3. Mask W1C bits (&~0x03) → 0x0100
    //   4. Write 0x0100
    hw.flip(W1cRegEnable{});

    bool ok = true;
    auto result = hw.peek<uint32_t>(0x14);
    ok &= t.assert_true((result & 0x0100U) != 0, "Enable should be flipped to 1");
    ok &= t.assert_true((result & 0x03U) == 0U, "W1C bits should be masked to 0");
    return ok;
}

/// @brief flip() in W1C register toggles back — ensures bidirectional flip works.
bool test_flip_in_w1c_register_toggle_back(TestContext& t) {
    MockTransport hw;

    // Enable=1, Mode=1, OVR=1
    hw.poke<uint32_t>(0x14, 0x0301U);

    // flip(Enable) should:
    //   1. Read 0x0301
    //   2. XOR Enable mask → 0x0201
    //   3. Mask W1C (&~0x03) → 0x0200
    //   4. Write 0x0200
    hw.flip(W1cRegEnable{});

    bool ok = true;
    auto result = hw.peek<uint32_t>(0x14);
    ok &= t.assert_true((result & 0x0100U) == 0U, "Enable should be flipped to 0");
    ok &= t.assert_true((result & 0x0200U) != 0, "Mode should be preserved");
    ok &= t.assert_true((result & 0x03U) == 0U, "W1C bits should be masked to 0");
    return ok;
}

// =============================================================================
// clear() on all-W1C register — direct write path (no RMW needed)
// =============================================================================

bool test_clear_all_w1c_register(TestContext& t) {
    MockTransport hw;

    // All flags set: 0x0007 (bits 0,1,2)
    hw.poke<uint16_t>(0x1C, uint16_t{0x0007});

    // clear(Flag1) should directly write Flag1::mask() = 0x0002
    // because all bits are W1C → no non-W1C fields to preserve.
    hw.clear(AllW1cFlag1{});

    bool ok = true;
    auto result = hw.peek<uint16_t>(0x1C);
    // Direct write: only Flag1 bit is written (0x0002), NOT RMW.
    // In real HW: writing 1 to bit 1 clears it. But in RAM mock,
    // reg_write overwrites, so result = 0x0002.
    ok &= t.assert_eq(result, uint16_t{0x0002});
    return ok;
}

// =============================================================================
// clear() selective — one W1C field while others stay
// =============================================================================

/// @brief Clear only EOC while OVR is also set — both are W1C, only EOC should clear.
/// In a mixed register, clear() uses RMW: read → mask all W1C → set target → write.
/// This ensures only the requested W1C bit gets the write-1-to-clear command.
bool test_clear_selective_w1c(TestContext& t) {
    MockTransport hw;

    // OVR=1, EOC=1, Enable=1 → raw = 0x0103
    hw.poke<uint32_t>(0x14, 0x0103U);

    // clear(EOC) should:
    //   1. Read 0x0103
    //   2. Mask W1C bits (&~0x03) → 0x0100
    //   3. Set EOC mask (|0x02) → 0x0102
    //   4. Write 0x0102
    hw.clear(W1cEoc{});

    bool ok = true;
    auto result = hw.peek<uint32_t>(0x14);
    ok &= t.assert_true(result == 0x0102U, "Enable preserved, only EOC written as 1-to-clear");
    return ok;
}

// =============================================================================
// clear() non-W1C field preservation — thorough verification
// =============================================================================

/// @brief Multiple non-W1C fields must all survive clear() in a mixed register.
bool test_clear_preserves_all_non_w1c(TestContext& t) {
    MockTransport hw;

    // OVR=1, EOC=1, Enable=1, Mode=1 → raw = 0x0303
    hw.poke<uint32_t>(0x14, 0x0303U);

    hw.clear(W1cOvr{});

    bool ok = true;
    auto result = hw.peek<uint32_t>(0x14);
    // RMW: read 0x0303, mask W1C (&~0x03) → 0x0300, set OVR (|0x01) → 0x0301
    ok &= t.assert_true((result & 0x0100U) != 0, "Enable preserved");
    ok &= t.assert_true((result & 0x0200U) != 0, "Mode preserved");
    ok &= t.assert_true((result & 0x03U) == 0x01U, "Only OVR bit written as 1-to-clear");
    return ok;
}

// =============================================================================
// clear() when target W1C bit is already 0 — should still write 1
// =============================================================================

bool test_clear_already_cleared_w1c(TestContext& t) {
    MockTransport hw;

    // OVR=0, EOC=0, Enable=1 → only non-W1C field set
    hw.poke<uint32_t>(0x14, 0x0100U);

    // clear(OVR) even though OVR is already 0 — should still write the bit
    hw.clear(W1cOvr{});

    bool ok = true;
    auto result = hw.peek<uint32_t>(0x14);
    // RMW: read 0x0100, mask W1C (&~0x03) → 0x0100, set OVR (|0x01) → 0x0101
    ok &= t.assert_true(result == 0x0101U, "Enable preserved, OVR written as 1-to-clear");
    return ok;
}

// =============================================================================
// flip() on 16-bit W1C register — different register widths
// =============================================================================

bool test_flip_16bit_w1c_register(TestContext& t) {
    MockTransport hw;

    // Ctrl16: W1C bits 0-1 set, Enable=0
    hw.poke<uint16_t>(0x20, uint16_t{0x0003});

    // flip(Enable) — W1C bits should be masked
    hw.flip(Ctrl16Enable{});

    bool ok = true;
    auto result = hw.peek<uint16_t>(0x20);
    ok &= t.assert_true((result & 0x0100U) != 0, "Enable should be 1");
    ok &= t.assert_true((result & 0x03U) == uint16_t{0}, "16-bit W1C bits masked");
    return ok;
}

// =============================================================================
// modify() with DynamicValue on W1C register
// =============================================================================

bool test_modify_dynamic_value_w1c_register(TestContext& t) {
    MockTransport hw;

    // Ctrl16: W1C bits set, Enable=0, Mode=0
    hw.poke<uint16_t>(0x20, uint16_t{0x0003});

    // modify() with DynamicValue — W1C bits must be masked
    hw.modify(Ctrl16Mode::value(static_cast<uint8_t>(0x02)));

    bool ok = true;
    auto result = hw.peek<uint16_t>(0x20);
    ok &= t.assert_true(((result >> 4) & 0x03) == uint16_t{0x02}, "Mode should be 2");
    ok &= t.assert_true((result & 0x03U) == uint16_t{0}, "W1C bits should be masked");
    return ok;
}

// =============================================================================
// write() single field resets others to reset_value
// =============================================================================

/// @brief Verify that write() with a single field sets other fields to reset_value,
/// NOT to zero. ConfigReg has reset=0xFF00 (prescaler = 0xFF).
bool test_write_single_field_resets_others(TestContext& t) {
    MockTransport hw;

    // Pre-fill with something else
    hw.poke<uint32_t>(0x04, 0x0000'0000U);

    // write(Enable::Set) should write reset_value(0xFF00) | Enable(0x01) = 0xFF01
    hw.write(ConfigEnable::Set{});

    bool ok = true;
    auto result = hw.peek<uint32_t>(0x04);
    ok &= t.assert_true((result & 0x01U) == 1U, "Enable bit set");
    // Prescaler should be 0xFF (from reset_value), NOT 0x00
    ok &= t.assert_true(((result >> 8) & 0xFFU) == 0xFFU, "Prescaler should be reset value 0xFF");
    ok &= t.assert_true(result == 0xFF01U, "Full register = reset_value | enable");
    return ok;
}

// =============================================================================
// DynamicValue boundary values
// =============================================================================

/// @brief Max value for field width should be accepted.
bool test_dynamic_value_max_boundary(TestContext& t) {
    const MockTransport hw;

    // ConfigPrescaler: 8-bit field → max = 255
    hw.write(ConfigPrescaler::value(static_cast<uint8_t>(255)));

    bool ok = true;
    ok &= t.assert_eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(255));
    return ok;
}

/// @brief Zero value should work for all field types.
bool test_dynamic_value_zero(TestContext& t) {
    MockTransport hw;

    hw.poke<uint32_t>(0x04, 0xFFFF'FFFFU);
    hw.modify(ConfigPrescaler::value(static_cast<uint8_t>(0)));

    bool ok = true;
    ok &= t.assert_eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0));
    // Other fields (enable, mode) should be preserved
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(3));
    return ok;
}

// =============================================================================
// RegionValue field comparison operators
// =============================================================================

bool test_region_value_field_eq_value(TestContext& t) {
    MockTransport hw;

    hw.poke<uint32_t>(0x04, 0x02U); // Mode = FAST (bits 1-2 = 01)
    auto mode_val = hw.read(ConfigMode{});

    bool ok = true;
    ok &= t.assert_true(mode_val == ModeFast{}, "RegionValue<Field> == Value match");
    ok &= t.assert_true(!(mode_val == ModeNormal{}), "RegionValue<Field> != Value mismatch");
    return ok;
}

bool test_region_value_field_eq_dynamic(TestContext& t) {
    MockTransport hw;

    hw.poke<uint32_t>(0x04, 0x42U << 8); // Prescaler = 0x42
    auto presc_val = hw.read(ConfigPrescaler{});

    bool ok = true;
    // DynamicValue comparison uses .bits() since value() returns DynamicValue
    // with base Field type parameter, not the derived type alias.
    ok &= t.assert_true(presc_val.bits() == static_cast<uint8_t>(0x42), "RegionValue<Field> bits match");
    ok &= t.assert_true(presc_val.bits() != static_cast<uint8_t>(0x43), "RegionValue<Field> bits mismatch");
    // is() at RegOps level handles DynamicValue correctly
    ok &= t.assert_true(hw.is(ConfigPrescaler::value(static_cast<uint8_t>(0x42))), "is() with DynamicValue match");
    ok &= t.assert_true(!hw.is(ConfigPrescaler::value(static_cast<uint8_t>(0x43))), "is() with DynamicValue mismatch");
    return ok;
}

// =============================================================================
// is() with out-of-range DynamicValue — triggers error policy
// =============================================================================

bool test_is_out_of_range_dynamic_value(TestContext& t) {
    custom_handler_called = false;
    const CustomErrTransport hw;

    // ConfigPrescaler is 8-bit (max 255). Compare with 256.
    (void)hw.is(DynamicValue<ConfigPrescaler, uint16_t>{256});

    return t.assert_true(custom_handler_called, "is() should invoke error handler for out-of-range value");
}

// =============================================================================
// modify() multiple fields with mixed Value + DynamicValue
// =============================================================================

bool test_modify_mixed_value_dynamic(TestContext& t) {
    MockTransport hw;

    hw.poke<uint32_t>(0x04, 0U);

    // Modify: Set enable (Value) + prescaler 0x42 (DynamicValue) in one RMW
    hw.modify(ConfigEnable::Set{}, ConfigPrescaler::value(static_cast<uint8_t>(0x42)));

    bool ok = true;
    ok &= t.assert_eq(hw.read(ConfigEnable{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(ConfigPrescaler{}).bits(), static_cast<uint8_t>(0x42));
    ok &= t.assert_eq(hw.read(ConfigMode{}).bits(), static_cast<uint8_t>(0));
    return ok;
}

// =============================================================================
// 64-bit register through direct RegOps
// =============================================================================

bool test_64bit_register_write_read(TestContext& t) {
    const MockTransport hw;

    hw.write(Reg64Direct::value(0x0102'0304'0506'0708ULL));
    auto val = hw.read(Reg64Direct{});
    return t.assert_eq(val.bits(), 0x0102'0304'0506'0708ULL);
}

bool test_64bit_register_mask(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(Reg64Direct::mask(), ~std::uint64_t{0});
    ok &= t.assert_eq(Field64Low32::mask(), static_cast<std::uint64_t>(0xFFFF'FFFF));
    ok &= t.assert_eq(Field64High32::mask(), 0xFFFF'FFFF'0000'0000ULL);
    return ok;
}

bool test_64bit_field_read(TestContext& t) {
    const MockTransport hw;

    hw.write(Reg64Direct::value(0xAAAA'BBBB'CCCC'DDDDULL));
    bool ok = true;
    ok &= t.assert_eq(hw.read(Field64Low32{}).bits(), 0xCCCC'DDDDU);
    ok &= t.assert_eq(hw.read(Field64High32{}).bits(), 0xAAAA'BBBBU);
    return ok;
}

bool test_64bit_modify(TestContext& t) {
    MockTransport hw;

    hw.poke<std::uint64_t>(0x28, 0xFFFF'FFFF'0000'0000ULL);
    hw.modify(Field64Low32::value(0xDEAD'BEEFU));

    bool ok = true;
    ok &= t.assert_eq(hw.read(Field64Low32{}).bits(), 0xDEAD'BEEFU);
    ok &= t.assert_eq(hw.read(Field64High32{}).bits(), 0xFFFF'FFFFU);
    return ok;
}

bool test_64bit_reset(TestContext& t) {
    MockTransport hw;

    hw.poke<std::uint64_t>(0x28, 0xFFFF'FFFF'FFFF'FFFFULL);
    hw.reset(Reg64Direct{});
    return t.assert_eq(hw.peek<std::uint64_t>(0x28), 0ULL);
}

// =============================================================================
// Non-zero base_address Device
// =============================================================================

struct MmioPeripheral : Device<> {
    static constexpr Addr base_address = 0x4001'3000;
};
struct MmioCtrl : Register<MmioPeripheral, 0x00, bits32, RW, 0> {};
struct MmioStatus : Register<MmioPeripheral, 0x04, bits32, RO, 0> {};
struct MmioCtrlEn : Field<MmioCtrl, 0, 1> {};

bool test_nonzero_base_address(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(MmioCtrl::address, static_cast<Addr>(0x4001'3000));
    ok &= t.assert_eq(MmioStatus::address, static_cast<Addr>(0x4001'3004));
    ok &= t.assert_eq(MmioCtrlEn::address, static_cast<Addr>(0x4001'3000));
    return ok;
}

struct MmioPeripheralBlock : Block<MmioPeripheral, 0x100> {};
struct MmioBlockReg : Register<MmioPeripheralBlock, 0x10, bits16> {};

bool test_nonzero_base_with_block(TestContext& t) {
    return t.assert_eq(MmioBlockReg::address, static_cast<Addr>(0x4001'3110));
}

// =============================================================================
// RegionValue edge cases
// =============================================================================

bool test_region_value_eq_region_value(TestContext& t) {
    MockTransport hw;

    hw.poke<uint32_t>(0x04, 0x1234U);
    auto val1 = hw.read(ConfigReg{});
    auto val2 = hw.read(ConfigReg{});

    bool ok = true;
    ok &= t.assert_true(val1 == val2, "same reads should be equal");

    hw.poke<uint32_t>(0x04, 0x5678U);
    auto val3 = hw.read(ConfigReg{});
    ok &= t.assert_true(!(val1 == val3), "different reads should differ");
    return ok;
}

bool test_region_value_field_eq_dynamic_operator(TestContext& t) {
    MockTransport hw;

    hw.poke<uint32_t>(0x04, 0x42U << 8); // Prescaler = 0x42
    auto presc_val = hw.read(ConfigPrescaler{});

    bool ok = true;
    // Direct operator== with DynamicValue (requires exact type match)
    // Field::value() returns DynamicValue<Field<...base...>, T>, so we
    // construct DynamicValue<ConfigPrescaler, T> manually for the operator.
    ok &= t.assert_true(presc_val == DynamicValue<ConfigPrescaler, uint8_t>{0x42},
                        "RegionValue<Field> == DynamicValue match");
    ok &= t.assert_true(!(presc_val == DynamicValue<ConfigPrescaler, uint8_t>{0x43}),
                        "RegionValue<Field> == DynamicValue mismatch");
    return ok;
}

bool test_region_value_is_with_dynamic_value(TestContext& t) {
    MockTransport hw;

    hw.poke<uint32_t>(0x04, 0x42U << 8); // Prescaler = 0x42

    bool ok = true;
    // RegOps::is() with field-level DynamicValue (the primary API path)
    ok &= t.assert_true(hw.is(ConfigPrescaler::value(static_cast<uint8_t>(0x42))),
                        "RegOps::is(DynamicValue<Field>) match");
    ok &= t.assert_true(!hw.is(ConfigPrescaler::value(static_cast<uint8_t>(0x43))),
                        "RegOps::is(DynamicValue<Field>) mismatch");
    return ok;
}

bool test_region_value_is_with_register_dynamic(TestContext& t) {
    MockTransport hw;

    hw.poke<uint32_t>(0x04, 0x4200U);

    bool ok = true;
    // RegOps::is() with register-level DynamicValue
    ok &= t.assert_true(hw.is(ConfigReg::value(0x4200U)), "RegOps::is(Reg DynamicValue) match");
    ok &= t.assert_true(!hw.is(ConfigReg::value(0x4201U)), "RegOps::is(Reg DynamicValue) mismatch");
    return ok;
}

bool test_region_value_field_eq_field(TestContext& t) {
    MockTransport hw;

    hw.poke<uint32_t>(0x04, 0x02U); // Mode = FAST (bits 1-2 = 01)
    auto mode1 = hw.read(ConfigMode{});
    auto mode2 = hw.read(ConfigMode{});

    bool ok = true;
    ok &= t.assert_true(mode1 == mode2, "same field reads should be equal");
    return ok;
}

// =============================================================================
// Value::shifted_value direct assertion
// =============================================================================

bool test_value_shifted_value(TestContext& t) {
    bool ok = true;
    // ConfigEnable: bit 0, width 1 → shift = 0 → shifted_value = 1 << 0 = 1
    ok &= t.assert_eq(ConfigEnable::Set::shifted_value, static_cast<uint32_t>(1));
    ok &= t.assert_eq(ConfigEnable::Reset::shifted_value, static_cast<uint32_t>(0));

    // ConfigMode: shift = 1 → FAST(1) shifted = 1 << 1 = 2
    ok &= t.assert_eq(ModeFast::shifted_value, static_cast<uint32_t>(2));
    // LOW_POWER(2) shifted = 2 << 1 = 4
    ok &= t.assert_eq(ModeLowPower::shifted_value, static_cast<uint32_t>(4));
    // TEST(3) shifted = 3 << 1 = 6
    ok &= t.assert_eq(ModeTest::shifted_value, static_cast<uint32_t>(6));
    return ok;
}

// =============================================================================
// Multi-transport device
// =============================================================================

struct DualTransportDevice : Device<RW, Direct, I2c> {};
struct DualReg : Register<DualTransportDevice, 0x00, bits32, RW, 0> {};

bool test_multi_transport_device(TestContext& t) {
    // DualTransportDevice allows both Direct and I2c
    using Allowed = DualTransportDevice::AllowedTransportsType;
    bool ok = true;
    ok &= t.assert_true((std::tuple_size_v<Allowed> == 2), "should allow 2 transports");
    return ok;
}

} // namespace

void run_transport_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("Type traits");
    suite.run("UintFit sizes", test_uint_fit_sizes);
    suite.run("access policy flags", test_access_policy_traits);

    umi::test::Suite::section("Static properties");
    suite.run("register addresses/reset", test_register_static_properties);
    suite.run("field shift/width/mask", test_field_static_properties);
    suite.run("Value constants", test_value_static_properties);

    umi::test::Suite::section("MockTransport internals");
    suite.run("peek/poke/reset", test_mock_transport_peek_poke);

    umi::test::Suite::section("I2C transport (mock)");
    suite.run("write/read", test_i2c_transport_write_read);
    suite.run("field operations", test_i2c_transport_field_operations);
    suite.run("modify (RMW)", test_i2c_transport_modify);

    umi::test::Suite::section("W1C / reset / read_variant");
    suite.run("W1C clear()", test_w1c_clear);
    suite.run("register reset()", test_register_reset);
    suite.run("W1C modify() safety", test_w1c_modify_safety);
    suite.run("read_variant() match", test_read_variant_match);
    suite.run("read_variant() unknown", test_read_variant_unknown);

    umi::test::Suite::section("CustomErrorHandler");
    suite.run("callback invoked on range error", test_custom_error_handler_callback);

    umi::test::Suite::section("Multi-field modify with W1C");
    suite.run("W1C bits masked in multi-modify", test_w1c_modify_multi_field);

    umi::test::Suite::section("8-bit register");
    suite.run("write/read/modify/reset", test_8bit_register_ops);

    umi::test::Suite::section("flip() in W1C register");
    suite.run("W1C bits masked during flip", test_flip_in_w1c_register);
    suite.run("flip toggle back with W1C mask", test_flip_in_w1c_register_toggle_back);
    suite.run("flip 16-bit W1C register", test_flip_16bit_w1c_register);

    umi::test::Suite::section("clear() edge cases");
    suite.run("all-W1C register direct write", test_clear_all_w1c_register);
    suite.run("selective clear one of multiple W1C", test_clear_selective_w1c);
    suite.run("preserves all non-W1C fields", test_clear_preserves_all_non_w1c);
    suite.run("clear already-cleared W1C bit", test_clear_already_cleared_w1c);

    umi::test::Suite::section("write() semantics");
    suite.run("single field resets others to reset_value", test_write_single_field_resets_others);

    umi::test::Suite::section("DynamicValue boundary");
    suite.run("max boundary value", test_dynamic_value_max_boundary);
    suite.run("zero value preserves neighbors", test_dynamic_value_zero);

    umi::test::Suite::section("modify() with DynamicValue");
    suite.run("DynamicValue on W1C register", test_modify_dynamic_value_w1c_register);
    suite.run("mixed Value + DynamicValue", test_modify_mixed_value_dynamic);

    umi::test::Suite::section("RegionValue comparisons");
    suite.run("field == Value", test_region_value_field_eq_value);
    suite.run("field == DynamicValue", test_region_value_field_eq_dynamic);
    suite.run("is() out-of-range triggers handler", test_is_out_of_range_dynamic_value);

    umi::test::Suite::section("64-bit register (direct)");
    suite.run("write/read", test_64bit_register_write_read);
    suite.run("mask computation", test_64bit_register_mask);
    suite.run("field read", test_64bit_field_read);
    suite.run("modify (RMW)", test_64bit_modify);
    suite.run("reset", test_64bit_reset);

    umi::test::Suite::section("Non-zero base_address");
    suite.run("MMIO peripheral addresses", test_nonzero_base_address);
    suite.run("block within MMIO peripheral", test_nonzero_base_with_block);

    umi::test::Suite::section("RegionValue edge cases");
    suite.run("RegionValue == RegionValue", test_region_value_eq_region_value);
    suite.run("field == DynamicValue (operator)", test_region_value_field_eq_dynamic_operator);
    suite.run("is() with field DynamicValue", test_region_value_is_with_dynamic_value);
    suite.run("is() with register DynamicValue", test_region_value_is_with_register_dynamic);
    suite.run("field RegionValue == field RegionValue", test_region_value_field_eq_field);

    umi::test::Suite::section("Value::shifted_value");
    suite.run("shifted values match expected", test_value_shifted_value);

    umi::test::Suite::section("Multi-transport device");
    suite.run("dual transport allowed", test_multi_transport_device);
}

} // namespace umimmio::test
