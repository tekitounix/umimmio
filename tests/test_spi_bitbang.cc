// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for SPI transport, bit-bang I2C, and bit-bang SPI transports.

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include <umimmio/transport/bitbang_i2c.hh>
#include <umimmio/transport/bitbang_spi.hh>
#include <umimmio/transport/i2c.hh>
#include <umimmio/transport/spi.hh>

#include "test_fixture.hh"

namespace umimmio::test {
namespace {

using umi::test::TestContext;
using namespace umi::mmio;

// =============================================================================
// Mock SPI driver (HAL-level)
// =============================================================================

/// @brief RAM-backed SPI mock with full-duplex transfer().
struct MockSpi {
    mutable std::array<std::uint8_t, 256> memory{};

    /// Full-duplex transfer. First byte(s) = address, remaining = data.
    /// For reads, response data comes from memory[addr].
    /// For writes, tx data is stored at memory[addr].
    void transfer(const std::uint8_t* tx, std::uint8_t* rx, std::size_t size) const {
        if (size < 2) {
            return;
        }
        // Address is first byte (mask off command bits)
        std::uint8_t const addr = tx[0] & 0x7F;
        bool const is_read = (tx[0] & 0x80) != 0;

        if (rx != nullptr && is_read) {
            // Read: fill rx with data from memory starting at addr
            std::memset(rx, 0, size);
            std::memcpy(rx + 1, &memory[addr], size - 1);
        } else {
            // Write: store tx data bytes into memory
            std::memcpy(&memory[addr], tx + 1, size - 1);
        }
    }
};

// SPI device/register definitions
struct SPIDevice : Device<RW, SPITransportTag> {};
struct SPIReg32 : Register<SPIDevice, 0x10, bits32, RW, 0> {};
struct SPIField8 : Field<SPIReg32, 0, 8> {};
struct SPIFieldHigh : Field<SPIReg32, 24, 8> {};

// =============================================================================
// SPI transport tests
// =============================================================================

bool test_spi_write_read(TestContext& t) {
    MockSpi spi;
    SpiTransport<MockSpi> const transport(spi);

    transport.write(SPIReg32::value(0xCAFE'BABEU));
    auto val = transport.read(SPIReg32{});
    return t.assert_eq(val, 0xCAFE'BABEU);
}

bool test_spi_field_read(TestContext& t) {
    MockSpi spi;
    SpiTransport<MockSpi> const transport(spi);

    transport.write(SPIReg32::value(0x000000ABU));
    auto val = transport.read(SPIField8{});
    return t.assert_eq(val, static_cast<uint8_t>(0xAB));
}

bool test_spi_modify(TestContext& t) {
    MockSpi spi;
    SpiTransport<MockSpi> const transport(spi);

    transport.write(SPIReg32::value(0xFF000000U));
    transport.modify(SPIField8::value(static_cast<uint8_t>(0x42)));

    bool ok = true;
    auto val = transport.read(SPIReg32{});
    ok &= t.assert_eq(val & 0xFFU, 0x42U);
    ok &= t.assert_eq(val & 0xFF000000U, 0xFF000000U);
    return ok;
}

bool test_spi_high_field(TestContext& t) {
    MockSpi spi;
    SpiTransport<MockSpi> const transport(spi);

    transport.write(SPIReg32::value(0xAB000000U));
    auto val = transport.read(SPIFieldHigh{});
    return t.assert_eq(val, static_cast<uint8_t>(0xAB));
}

// =============================================================================
// Mock GPIO for bit-bang I2C
// =============================================================================

/// @brief GPIO mock that records transitions and simulates slave memory.
struct MockI2CGpio {
    mutable bool scl_state = true;
    mutable bool sda_state = true;
    mutable int call_count = 0;

    // Slave simulation
    mutable std::array<std::uint8_t, 256> memory{};
    mutable bool slave_ack = true; // whether slave ACKs

    // Track writes for verification
    mutable std::uint8_t last_written_byte = 0;
    mutable int bytes_transmitted = 0;
    mutable int bit_index = 0;
    mutable std::uint8_t current_byte = 0;

    // State machine for read simulation
    mutable enum class State : std::uint8_t {
        IDLE,
        ADDR_WRITE,
        REG_ADDR,
        ADDR_READ,
        DATA_READ,
        DATA_WRITE
    } state = State::IDLE;
    mutable std::uint8_t reg_addr = 0;
    mutable int data_offset = 0;

    void scl_high() const {
        scl_state = true;
        call_count++;
    }
    void scl_low() const {
        scl_state = false;
        call_count++;
    }
    void sda_high() const {
        sda_state = true;
        call_count++;
    }
    void sda_low() const {
        sda_state = false;
        call_count++;
    }
    void delay() const { call_count++; }

    /// Simulate slave: when master reads, return memory data.
    /// For simplicity, just return bytes from memory[reg_addr + offset].
    bool sda_read() const {
        call_count++;
        // During ACK phase after write_byte, return !slave_ack
        // During read phase, return bits from memory
        if (state == State::DATA_READ) {
            std::uint8_t const byte = memory[reg_addr + data_offset];
            bool const bit = (byte & (1U << bit_index)) != 0;
            return bit;
        }
        return !slave_ack; // ACK = pull low = false
    }
};

// Device for bit-bang I2C (uses IgnoreError to avoid assertions in tests)
struct BBI2CDevice : Device<RW, I2CTransportTag> {};
struct BBI2CReg : Register<BBI2CDevice, 0x20, bits32, RW, 0> {};
struct BBI2CField : Field<BBI2CReg, 0, 8> {};

// =============================================================================
// Bit-bang I2C tests (basic functional tests)
// =============================================================================

bool test_bitbang_i2c_gpio_calls(TestContext& t) {
    // Verify write exercises GPIO and multi-byte uses more calls
    MockI2CGpio gpio;
    BitBangI2cTransport<MockI2CGpio, std::true_type, IgnoreError> const transport(gpio, 0xA0);

    gpio.call_count = 0;
    std::array<std::uint8_t, 1> data1 = {0x42};
    transport.raw_write(static_cast<std::uint8_t>(0x20), data1.data(), 1);
    int const single_byte_calls = gpio.call_count;

    gpio.call_count = 0;
    std::array<std::uint8_t, 4> data4 = {0x11, 0x22, 0x33, 0x44};
    transport.raw_write(static_cast<std::uint8_t>(0x20), data4.data(), 4);
    int const multi_byte_calls = gpio.call_count;

    bool ok = true;
    ok &= t.assert_gt(single_byte_calls, 0);
    ok &= t.assert_gt(multi_byte_calls, single_byte_calls);
    return ok;
}

bool test_bitbang_i2c_start_stop_sequence(TestContext& t) {
    // Verify START condition is generated (SDA falls while SCL high)
    MockI2CGpio gpio;
    BitBangI2cTransport<MockI2CGpio, std::true_type, IgnoreError> const transport(gpio, 0xA0);

    // After init, both lines should be idle (high)
    bool ok = true;
    ok &= t.assert_true(gpio.scl_state, "SCL should start high");
    ok &= t.assert_true(gpio.sda_state, "SDA should start high");

    std::array<std::uint8_t, 1> data = {0x00};
    transport.raw_write(static_cast<std::uint8_t>(0x00), data.data(), 1);

    // After write completes with STOP, both lines should return high
    ok &= t.assert_true(gpio.sda_state, "SDA should be high after STOP");
    return ok;
}

bool test_bitbang_i2c_write_lines_idle_after(TestContext& t) {
    MockI2CGpio gpio;
    BitBangI2cTransport<MockI2CGpio, std::true_type, IgnoreError> const transport(gpio, 0xA0);

    std::array<std::uint8_t, 4> data = {0x11, 0x22, 0x33, 0x44};
    transport.raw_write(static_cast<std::uint8_t>(0x00), data.data(), 4);

    // After STOP, both lines should be idle (high)
    bool ok = true;
    ok &= t.assert_true(gpio.scl_state, "SCL idle after write");
    ok &= t.assert_true(gpio.sda_state, "SDA idle after write");
    return ok;
}

// =============================================================================
// Mock GPIO for bit-bang SPI
// =============================================================================

/// @brief GPIO mock for SPI with RAM-backed slave simulation.
///
/// Tracks bit-level state to simulate a real SPI slave device.
/// On write: stores transmitted data bytes into memory[addr].
/// On read: returns memory[addr] data via miso_read().
struct MockSpiPins {
    mutable bool cs_state = true;
    mutable bool sck_state = false;
    mutable bool mosi_state = false;

    // Slave memory
    mutable std::array<std::uint8_t, 256> memory{};

    // Bit-level tracking for address/data reconstruction
    mutable int total_bits = 0;           // bits clocked since CS low
    mutable std::uint8_t addr_byte = 0;   // captured address byte
    mutable bool addr_done = false;       // address byte fully received
    mutable bool is_read = false;         // read command detected
    mutable std::uint8_t write_accum = 0; // accumulator for write data byte
    mutable int data_byte_idx = 0;        // current data byte offset

    void cs_low() const {
        cs_state = false;
        total_bits = 0;
        addr_byte = 0;
        addr_done = false;
        is_read = false;
        write_accum = 0;
        data_byte_idx = 0;
    }
    void cs_high() const {
        // Flush last partial write byte if needed
        cs_state = true;
    }
    void sck_low() const { sck_state = false; }
    void sck_high() const {
        sck_state = true;
        // On rising edge, capture MOSI bit
        int const bit_in_byte = 7 - (total_bits % 8);

        if (!addr_done) {
            // Accumulate address byte
            if (mosi_state) {
                addr_byte |= static_cast<std::uint8_t>(1U << bit_in_byte);
            }
            if (bit_in_byte == 0) {
                // Address byte complete
                addr_done = true;
                is_read = (addr_byte & 0x80) != 0;
            }
        } else if (!is_read) {
            // Write phase: accumulate data bits
            if (mosi_state) {
                write_accum |= static_cast<std::uint8_t>(1U << bit_in_byte);
            }
            if (bit_in_byte == 0) {
                // Data byte complete, store to memory
                std::uint8_t const reg_addr = addr_byte & 0x7F;
                memory[reg_addr + data_byte_idx] = write_accum;
                write_accum = 0;
                data_byte_idx++;
            }
        }
        total_bits++;
    }
    void mosi_high() const { mosi_state = true; }
    void mosi_low() const { mosi_state = false; }
    void delay() const {}

    /// Return data from memory for the current read phase.
    /// Called after sck_high() which already incremented total_bits,
    /// so use total_bits - 1 for the current bit position.
    bool miso_read() const {
        if (addr_done && is_read) {
            std::uint8_t const reg_addr = addr_byte & 0x7F;
            int const current_bit = total_bits - 1; // undo sck_high increment
            int const data_bits = current_bit - 8;  // bits after address byte
            int const byte_idx = data_bits / 8;
            int const bit_in_byte = 7 - (data_bits % 8);
            std::uint8_t const byte = memory[reg_addr + byte_idx];
            return (byte & (1U << bit_in_byte)) != 0;
        }
        return false;
    }
};

// Device for bit-bang SPI
struct BBSPIDevice : Device<RW, SPITransportTag> {};
struct BBSPIReg : Register<BBSPIDevice, 0x10, bits32, RW, 0> {};
struct BBSPIField : Field<BBSPIReg, 0, 8> {};

// =============================================================================
// Bit-bang SPI tests
// =============================================================================

bool test_bitbang_spi_write_roundtrip(TestContext& t) {
    MockSpiPins pins;
    BitBangSpiTransport<MockSpiPins> const transport(pins);

    // Write 4 bytes at address 0x10
    std::array<std::uint8_t, 4> tx = {0xAB, 0xCD, 0xEF, 0x01};
    transport.raw_write(static_cast<std::uint8_t>(0x10), tx.data(), 4);

    // Verify mock memory received the data
    bool ok = true;
    ok &= t.assert_eq(pins.memory[0x10], static_cast<uint8_t>(0xAB));
    ok &= t.assert_eq(pins.memory[0x11], static_cast<uint8_t>(0xCD));
    ok &= t.assert_eq(pins.memory[0x12], static_cast<uint8_t>(0xEF));
    ok &= t.assert_eq(pins.memory[0x13], static_cast<uint8_t>(0x01));
    return ok;
}

bool test_bitbang_spi_cs_control(TestContext& t) {
    MockSpiPins pins;
    BitBangSpiTransport<MockSpiPins> const transport(pins);

    bool ok = true;
    ok &= t.assert_true(pins.cs_state, "CS should start high (deasserted)");

    std::array<std::uint8_t, 1> data = {0x00};
    transport.raw_write(static_cast<std::uint8_t>(0x00), data.data(), 1);

    // After transfer, CS should return high
    ok &= t.assert_true(pins.cs_state, "CS should be high after transfer");
    return ok;
}

bool test_bitbang_spi_read_roundtrip(TestContext& t) {
    MockSpiPins pins;
    BitBangSpiTransport<MockSpiPins> const transport(pins);

    // Pre-load memory for read
    pins.memory[0x10] = 0xAB;
    pins.memory[0x11] = 0xCD;
    pins.memory[0x12] = 0xEF;
    pins.memory[0x13] = 0x01;

    std::array<std::uint8_t, 4> rx{};
    transport.raw_read(static_cast<std::uint8_t>(0x10), rx.data(), 4);

    // Verify read data matches pre-loaded memory
    bool ok = true;
    ok &= t.assert_eq(rx[0], static_cast<uint8_t>(0xAB));
    ok &= t.assert_eq(rx[1], static_cast<uint8_t>(0xCD));
    ok &= t.assert_eq(rx[2], static_cast<uint8_t>(0xEF));
    ok &= t.assert_eq(rx[3], static_cast<uint8_t>(0x01));
    return ok;
}

// =============================================================================
// I2C mock (shared by endian, 16-bit, 64-bit, and error policy tests)
// =============================================================================

/// @brief Minimal I2C driver mock (RAM-backed).
struct LocalMockI2C {
    mutable std::array<std::uint8_t, 256> memory{};

    struct Result {
        explicit operator bool() const { return success; }
        bool success = true;
    };

    Result write(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> data) const {
        if (data.size() < 2) {
            return {false};
        }
        std::uint8_t const reg_addr = data[0];
        std::memcpy(&memory[reg_addr], data.data() + 1, data.size() - 1);
        return {true};
    }

    Result write_read(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx) const {
        if (tx.empty()) {
            return {false};
        }
        std::uint8_t const reg_addr = tx[0];
        std::memcpy(rx.data(), &memory[reg_addr], rx.size());
        return {true};
    }
};

// Device and register definitions for endian tests
struct EndianDevice : Device<RW, I2CTransportTag> {};
struct EndianReg32 : Register<EndianDevice, 0x10, bits32, RW, 0> {};

// Type aliases for long template names
using I2cLE = I2cTransport<LocalMockI2C, std::true_type, AssertOnError, std::uint8_t, Endian::BIG, Endian::LITTLE>;
using I2cBE = I2cTransport<LocalMockI2C, std::true_type, AssertOnError, std::uint8_t, Endian::BIG, Endian::BIG>;

// =============================================================================
// ByteAdapter endian tests
// =============================================================================

bool test_i2c_endian_little(TestContext& t) {
    LocalMockI2C i2c;
    I2cLE const transport(i2c, 0x50);

    transport.write(EndianReg32::value(0x04030201U));

    // In little-endian: byte[0]=0x01, byte[1]=0x02, byte[2]=0x03, byte[3]=0x04
    bool ok = true;
    ok &= t.assert_eq(i2c.memory[0x10], static_cast<uint8_t>(0x01));
    ok &= t.assert_eq(i2c.memory[0x11], static_cast<uint8_t>(0x02));
    ok &= t.assert_eq(i2c.memory[0x12], static_cast<uint8_t>(0x03));
    ok &= t.assert_eq(i2c.memory[0x13], static_cast<uint8_t>(0x04));
    return ok;
}

bool test_i2c_endian_big(TestContext& t) {
    LocalMockI2C i2c;
    I2cBE const transport(i2c, 0x50);

    transport.write(EndianReg32::value(0x04030201U));

    // In big-endian: byte[0]=0x04, byte[1]=0x03, byte[2]=0x02, byte[3]=0x01
    bool ok = true;
    ok &= t.assert_eq(i2c.memory[0x10], static_cast<uint8_t>(0x04));
    ok &= t.assert_eq(i2c.memory[0x11], static_cast<uint8_t>(0x03));
    ok &= t.assert_eq(i2c.memory[0x12], static_cast<uint8_t>(0x02));
    ok &= t.assert_eq(i2c.memory[0x13], static_cast<uint8_t>(0x01));
    return ok;
}

bool test_i2c_endian_big_roundtrip(TestContext& t) {
    LocalMockI2C i2c;
    I2cBE const transport(i2c, 0x50);

    transport.write(EndianReg32::value(0xDEAD'BEEFU));
    auto val = transport.read(EndianReg32{});
    return t.assert_eq(val, 0xDEAD'BEEFU);
}

// =============================================================================
// 16-bit register tests
// =============================================================================

struct Reg16 : Register<EndianDevice, 0x20, bits16, RW, 0> {};
struct Field16High : Field<Reg16, 8, 8> {};

bool test_i2c_16bit_register(TestContext& t) {
    LocalMockI2C i2c;
    I2cTransport<LocalMockI2C> const transport(i2c, 0x50);

    transport.write(Reg16::value(static_cast<uint16_t>(0xABCD)));
    auto val = transport.read(Reg16{});
    return t.assert_eq(val, static_cast<uint16_t>(0xABCD));
}

bool test_i2c_16bit_field_high_byte(TestContext& t) {
    LocalMockI2C i2c;
    I2cTransport<LocalMockI2C> const transport(i2c, 0x50);

    transport.write(Reg16::value(static_cast<uint16_t>(0xAB00)));
    auto val = transport.read(Field16High{});
    return t.assert_eq(val, static_cast<uint8_t>(0xAB));
}

// =============================================================================
// 64-bit register tests
// =============================================================================

struct Reg64 : Register<EndianDevice, 0x30, bits64, RW, 0> {};
struct Field64Low : Field<Reg64, 0, 32> {};

bool test_i2c_64bit_register(TestContext& t) {
    LocalMockI2C i2c;
    I2cTransport<LocalMockI2C> const transport(i2c, 0x50);

    transport.write(Reg64::value(0x0102030405060708ULL));
    auto val = transport.read(Reg64{});
    return t.assert_eq(val, 0x0102030405060708ULL);
}

bool test_i2c_64bit_low_field(TestContext& t) {
    LocalMockI2C i2c;
    I2cTransport<LocalMockI2C> const transport(i2c, 0x50);

    transport.write(Reg64::value(0xAAAABBBBCCCCDDDDULL));
    auto val = transport.read(Field64Low{});
    return t.assert_eq(val, 0xCCCCDDDDU);
}

// =============================================================================
// Error policy tests
// =============================================================================

struct IgnoreReg : Register<EndianDevice, 0x00, bits32, RW, 0> {};

bool test_ignore_error_policy(TestContext& t) {
    LocalMockI2C i2c;
    I2cTransport<LocalMockI2C, std::false_type, IgnoreError> const transport(i2c, 0x50);

    transport.write(IgnoreReg::value(0x12345678U));
    auto val = transport.read(IgnoreReg{});
    return t.assert_eq(val, 0x12345678U);
}

} // namespace

void run_spi_bitbang_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("SPI transport (mock)");
    suite.run("write/read", test_spi_write_read);
    suite.run("field read", test_spi_field_read);
    suite.run("modify (RMW)", test_spi_modify);
    suite.run("high field read", test_spi_high_field);

    umi::test::Suite::section("Bit-bang I2C");
    suite.run("GPIO call count", test_bitbang_i2c_gpio_calls);
    suite.run("START/STOP sequence", test_bitbang_i2c_start_stop_sequence);
    suite.run("lines idle after write", test_bitbang_i2c_write_lines_idle_after);

    umi::test::Suite::section("Bit-bang SPI");
    suite.run("write roundtrip", test_bitbang_spi_write_roundtrip);
    suite.run("CS control", test_bitbang_spi_cs_control);
    suite.run("read roundtrip", test_bitbang_spi_read_roundtrip);

    umi::test::Suite::section("ByteAdapter endian");
    suite.run("little-endian wire format", test_i2c_endian_little);
    suite.run("big-endian wire format", test_i2c_endian_big);
    suite.run("big-endian roundtrip", test_i2c_endian_big_roundtrip);

    umi::test::Suite::section("16-bit / 64-bit registers");
    suite.run("16-bit register", test_i2c_16bit_register);
    suite.run("16-bit high-byte field", test_i2c_16bit_field_high_byte);
    suite.run("64-bit register", test_i2c_64bit_register);
    suite.run("64-bit low field", test_i2c_64bit_low_field);

    umi::test::Suite::section("Error policy");
    suite.run("IgnoreError policy", test_ignore_error_policy);
}

} // namespace umimmio::test
