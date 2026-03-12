// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for SPI and I2C transports, ByteAdapter endian, and error policies.
/// @author Shota Moriguchi @tekitounix
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include <umimmio/transport/i2c.hh>
#include <umimmio/transport/spi.hh>

#include "test_mock.hh"

namespace umimmio::test {
namespace {

using umi::test::TestContext;
using namespace umi::mmio;

// =============================================================================
// Mock SPI driver (HAL-level, void-returning)
// =============================================================================

/// @brief RAM-backed SPI mock with full-duplex transfer().
struct MockSpi {
    mutable std::array<std::uint8_t, 256> memory{};

    /// Full-duplex transfer. First byte(s) = address, remaining = data.
    /// For reads, response data comes from memory[addr].
    /// For writes, tx data is stored at memory[addr].
    void transfer(std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx) const {
        if (tx.size() < 2) {
            return;
        }
        // Address is first byte (mask off command bits)
        std::uint8_t const addr = tx[0] & 0x7F;
        bool const is_read = (tx[0] & 0x80) != 0;

        if (!rx.empty() && is_read) {
            // Read: fill rx with data from memory starting at addr
            std::memset(rx.data(), 0, rx.size());
            std::memcpy(rx.data() + 1, &memory[addr], rx.size() - 1);
        } else {
            // Write: store tx data bytes into memory
            std::memcpy(&memory[addr], tx.data() + 1, tx.size() - 1);
        }
    }
};

// SPI device/register definitions
struct SPIDevice : Device<RW, Spi> {};
struct SPIReg32 : Register<SPIDevice, 0x10, bits32, RW, 0> {};
struct SPIField8 : Field<SPIReg32, 0, 8, Numeric> {};
struct SPIFieldHigh : Field<SPIReg32, 24, 8, Numeric> {};

// =============================================================================
// SPI transport tests
// =============================================================================

void test_spi_write_read(TestContext& t) {
    MockSpi spi;
    const SpiTransport<MockSpi> transport(spi);

    transport.write(SPIReg32::value(0xCAFE'BABEU));
    auto val = transport.read(SPIReg32{});
    t.eq(val.bits(), 0xCAFE'BABEU);
}

void test_spi_field_read(TestContext& t) {
    MockSpi spi;
    const SpiTransport<MockSpi> transport(spi);

    transport.write(SPIReg32::value(0x000000ABU));
    auto val = transport.read(SPIField8{});
    t.eq(val.bits(), static_cast<uint8_t>(0xAB));
}

void test_spi_modify_rmw(TestContext& t) {
    MockSpi spi;
    const SpiTransport<MockSpi> transport(spi);

    transport.write(SPIReg32::value(0xFF000000U));
    transport.modify(SPIField8::value(static_cast<uint8_t>(0x42)));
    auto val = transport.read(SPIReg32{});
    t.eq(val.bits() & 0xFFU, 0x42U);
    t.eq(val.bits() & 0xFF000000U, 0xFF000000U);
}

void test_spi_high_field_read(TestContext& t) {
    MockSpi spi;
    const SpiTransport<MockSpi> transport(spi);

    transport.write(SPIReg32::value(0xAB000000U));
    auto val = transport.read(SPIFieldHigh{});
    t.eq(val.bits(), static_cast<uint8_t>(0xAB));
}

// =============================================================================
// I2C mock (shared by endian, multi-width, and error policy tests)
// =============================================================================

/// @brief Minimal I2C driver mock (RAM-backed, returns Result).
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
struct EndianDevice : Device<RW, I2c> {};
struct EndianReg32 : Register<EndianDevice, 0x10, bits32, RW, 0> {};

// Type aliases for long template names
using I2cLE =
    I2cTransport<LocalMockI2C, std::true_type, AssertOnError, std::uint8_t, std::endian::big, std::endian::little>;
using I2cBE =
    I2cTransport<LocalMockI2C, std::true_type, AssertOnError, std::uint8_t, std::endian::big, std::endian::big>;

// =============================================================================
// ByteAdapter endian tests
// =============================================================================

void test_i2c_endian_little_wire_format(TestContext& t) {
    LocalMockI2C i2c;
    const I2cLE transport(i2c, 0x50);

    transport.write(EndianReg32::value(0x04030201U));

    // In little-endian: byte[0]=0x01, byte[1]=0x02, byte[2]=0x03, byte[3]=0x04
    t.eq(i2c.memory[0x10], static_cast<uint8_t>(0x01));
    t.eq(i2c.memory[0x11], static_cast<uint8_t>(0x02));
    t.eq(i2c.memory[0x12], static_cast<uint8_t>(0x03));
    t.eq(i2c.memory[0x13], static_cast<uint8_t>(0x04));
}

void test_i2c_endian_big_wire_format(TestContext& t) {
    LocalMockI2C i2c;
    const I2cBE transport(i2c, 0x50);

    transport.write(EndianReg32::value(0x04030201U));

    // In big-endian: byte[0]=0x04, byte[1]=0x03, byte[2]=0x02, byte[3]=0x01
    t.eq(i2c.memory[0x10], static_cast<uint8_t>(0x04));
    t.eq(i2c.memory[0x11], static_cast<uint8_t>(0x03));
    t.eq(i2c.memory[0x12], static_cast<uint8_t>(0x02));
    t.eq(i2c.memory[0x13], static_cast<uint8_t>(0x01));
}

void test_i2c_endian_big_roundtrip(TestContext& t) {
    LocalMockI2C i2c;
    const I2cBE transport(i2c, 0x50);

    transport.write(EndianReg32::value(0xDEAD'BEEFU));
    auto val = transport.read(EndianReg32{});
    t.eq(val.bits(), 0xDEAD'BEEFU);
}

// =============================================================================
// 16-bit register tests
// =============================================================================

struct Reg16 : Register<EndianDevice, 0x20, bits16, RW, 0> {};
struct Field16High : Field<Reg16, 8, 8, Numeric> {};

void test_i2c_16bit_register_roundtrip(TestContext& t) {
    LocalMockI2C i2c;
    const I2cTransport<LocalMockI2C> transport(i2c, 0x50);

    transport.write(Reg16::value(static_cast<uint16_t>(0xABCD)));
    auto val = transport.read(Reg16{});
    t.eq(val.bits(), static_cast<uint16_t>(0xABCD));
}

void test_i2c_16bit_field_high_byte(TestContext& t) {
    LocalMockI2C i2c;
    const I2cTransport<LocalMockI2C> transport(i2c, 0x50);

    transport.write(Reg16::value(static_cast<uint16_t>(0xAB00)));
    auto val = transport.read(Field16High{});
    t.eq(val.bits(), static_cast<uint8_t>(0xAB));
}

// =============================================================================
// 64-bit register tests
// =============================================================================

struct Reg64 : Register<EndianDevice, 0x30, bits64, RW, 0> {};
struct Field64Low : Field<Reg64, 0, 32, Numeric> {};

void test_i2c_64bit_register_roundtrip(TestContext& t) {
    LocalMockI2C i2c;
    const I2cTransport<LocalMockI2C> transport(i2c, 0x50);

    transport.write(Reg64::value(0x0102030405060708ULL));
    auto val = transport.read(Reg64{});
    t.eq(val.bits(), 0x0102030405060708ULL);
}

void test_i2c_64bit_low_field(TestContext& t) {
    LocalMockI2C i2c;
    const I2cTransport<LocalMockI2C> transport(i2c, 0x50);

    transport.write(Reg64::value(0xAAAABBBBCCCCDDDDULL));
    auto val = transport.read(Field64Low{});
    t.eq(val.bits(), 0xCCCCDDDDU);
}

// =============================================================================
// Transport error policy tests
// =============================================================================

struct IgnoreReg : Register<EndianDevice, 0x00, bits32, RW, 0> {};

void test_ignore_error_policy(TestContext& t) {
    LocalMockI2C i2c;
    const I2cTransport<LocalMockI2C, std::false_type, IgnoreError> transport(i2c, 0x50);

    transport.write(IgnoreReg::value(0x12345678U));
    auto val = transport.read(IgnoreReg{});
    t.eq(val.bits(), 0x12345678U);
}

/// @brief I2C mock that always fails (returns false from write/write_read).
struct FailingI2C {
    struct Result {
        explicit operator bool() const { return success; }
        bool success;
    };

    [[nodiscard]] Result write(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> /*data*/) const {
        return {false};
    }

    [[nodiscard]] Result
    write_read(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> /*tx*/, std::span<std::uint8_t> /*rx*/) const {
        return {false};
    }
};

/// @brief Error counter for transport error tests.
int transport_error_count = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) test-only counter
void count_transport_errors(const char* /*msg*/) {
    transport_error_count++;
}

using CountingErrorPolicy = CustomErrorHandler<count_transport_errors>;

void test_i2c_transport_error_on_write(TestContext& t) {
    FailingI2C i2c;
    const I2cTransport<FailingI2C, std::false_type, CountingErrorPolicy> transport(i2c, 0x50);

    transport_error_count = 0;
    transport.write(IgnoreReg::value(0x12345678U));

    t.eq(transport_error_count, 1);
}

void test_i2c_transport_error_on_read(TestContext& t) {
    FailingI2C i2c;
    const I2cTransport<FailingI2C, std::false_type, CountingErrorPolicy> transport(i2c, 0x50);

    transport_error_count = 0;
    [[maybe_unused]] auto val = transport.read(IgnoreReg{});

    t.eq(transport_error_count, 1);
}

/// @brief SPI mock that returns failure from transfer().
struct FailingSpi {
    struct Result {
        explicit operator bool() const { return success; }
        bool success;
    };

    [[nodiscard]] Result transfer(std::span<const std::uint8_t> /*tx*/, std::span<std::uint8_t> /*rx*/) const {
        return {false};
    }
};

void test_spi_transport_error_on_write(TestContext& t) {
    FailingSpi spi;
    const SpiTransport<FailingSpi, std::false_type, CountingErrorPolicy> transport(spi);

    transport_error_count = 0;
    transport.write(SPIReg32::value(0xDEADBEEFU));

    t.eq(transport_error_count, 1);
}

void test_spi_transport_error_on_read(TestContext& t) {
    FailingSpi spi;
    const SpiTransport<FailingSpi, std::false_type, CountingErrorPolicy> transport(spi);

    transport_error_count = 0;
    [[maybe_unused]] auto val = transport.read(SPIReg32{});

    t.eq(transport_error_count, 1);
}

/// @brief I2C mock with void-returning write/write_read (no error checking possible).
struct VoidI2C {
    mutable std::array<std::uint8_t, 256> memory{};

    void write(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> data) const {
        if (data.size() < 2) {
            return;
        }
        std::uint8_t const reg_addr = data[0];
        std::memcpy(&memory[reg_addr], data.data() + 1, data.size() - 1);
    }

    void write_read(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx) const {
        if (tx.empty()) {
            return;
        }
        std::uint8_t const reg_addr = tx[0];
        std::memcpy(rx.data(), &memory[reg_addr], rx.size());
    }
};

void test_i2c_void_hal_roundtrip(TestContext& t) {
    VoidI2C i2c;
    const I2cTransport<VoidI2C> transport(i2c, 0x50);

    transport.write(EndianReg32::value(0xCAFEBABEU));
    auto val = transport.read(EndianReg32{});
    t.eq(val.bits(), 0xCAFEBABEU);
}

// =============================================================================
// SPI with non-default command bits
// =============================================================================

/// @brief SPI mock for custom command bit testing.
struct CustomCmdSpi {
    mutable std::array<std::uint8_t, 256> memory{};
    mutable std::uint8_t last_cmd_byte = 0;

    void transfer(std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx) const {
        if (tx.size() < 2) {
            return;
        }
        last_cmd_byte = tx[0];
        // Custom: ReadBit=0x40, CmdMask=0x3F, WriteBit=0x00
        std::uint8_t const addr = tx[0] & 0x3F;
        bool const is_read = (tx[0] & 0x40) != 0;

        if (!rx.empty() && is_read) {
            std::memset(rx.data(), 0, rx.size());
            std::memcpy(rx.data() + 1, &memory[addr], rx.size() - 1);
        } else {
            std::memcpy(&memory[addr], tx.data() + 1, tx.size() - 1);
        }
    }
};

struct CustomSpiDevice : Device<RW, Spi> {};
struct CustomSpiReg : Register<CustomSpiDevice, 0x10, bits32, RW, 0> {};

using CustomSpiTransport = SpiTransport<CustomCmdSpi,
                                        std::true_type,
                                        AssertOnError,
                                        std::uint8_t,
                                        std::endian::big,
                                        std::endian::little,
                                        /*ReadBit=*/0x40,
                                        /*CmdMask=*/0x3F,
                                        /*WriteBit=*/0x00>;

void test_spi_custom_command_bits(TestContext& t) {
    CustomCmdSpi spi;
    const CustomSpiTransport transport(spi);

    transport.write(CustomSpiReg::value(0xDEAD'BEEFU));
    auto val = transport.read(CustomSpiReg{});
    t.eq(val.bits(), 0xDEAD'BEEFU);
    // Verify the read command byte had custom read bit (0x40) applied
    t.is_true((spi.last_cmd_byte & 0x40) != 0);
}

// =============================================================================
// SPI with big-endian data
// =============================================================================

using SpiBE = SpiTransport<MockSpi, std::true_type, AssertOnError, std::uint8_t, std::endian::big, std::endian::big>;

void test_spi_big_endian_data(TestContext& t) {
    MockSpi spi;
    const SpiBE transport(spi);

    transport.write(SPIReg32::value(0x04030201U));
    auto val = transport.read(SPIReg32{});
    t.eq(val.bits(), 0x04030201U);
}

// =============================================================================
// SPI 16-bit register
// =============================================================================

struct SPIReg16 : Register<SPIDevice, 0x20, bits16, RW, 0> {};
struct SPIField16High : Field<SPIReg16, 8, 8, Numeric> {};

void test_spi_16bit_register(TestContext& t) {
    MockSpi spi;
    const SpiTransport<MockSpi> transport(spi);

    transport.write(SPIReg16::value(static_cast<uint16_t>(0xABCD)));
    auto val = transport.read(SPIReg16{});
    t.eq(val.bits(), static_cast<uint16_t>(0xABCD));
}

// =============================================================================
// I2C with 8-bit register
// =============================================================================

struct I2CReg8 : Register<EndianDevice, 0x40, bits8, RW, 0xAA> {};
struct I2CField8Low : Field<I2CReg8, 0, 4, Numeric> {};

void test_i2c_8bit_register(TestContext& t) {
    LocalMockI2C i2c;
    const I2cTransport<LocalMockI2C> transport(i2c, 0x50);

    transport.write(I2CReg8::value(static_cast<uint8_t>(0x5A)));
    t.eq(transport.read(I2CReg8{}).bits(), static_cast<uint8_t>(0x5A));
    t.eq(transport.read(I2CField8Low{}).bits(), static_cast<uint8_t>(0x0A));
}

// =============================================================================
// I2C with 16-bit address width
// =============================================================================

/// @brief I2C mock supporting 16-bit register addresses.
struct MockI2C16Addr {
    mutable std::array<std::uint8_t, 65536> memory{};

    struct Result {
        explicit operator bool() const { return success; }
        bool success = true;
    };

    Result write(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> data) const {
        if (data.size() < 3) {
            return {false};
        }
        // 16-bit address (big-endian)
        auto const reg_addr = static_cast<uint16_t>((data[0] << 8) | data[1]);
        std::memcpy(&memory[reg_addr], data.data() + 2, data.size() - 2);
        return {true};
    }

    Result write_read(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx) const {
        if (tx.size() < 2) {
            return {false};
        }
        auto const reg_addr = static_cast<uint16_t>((tx[0] << 8) | tx[1]);
        std::memcpy(rx.data(), &memory[reg_addr], rx.size());
        return {true};
    }
};

struct I2C16Device : Device<RW, I2c> {};
struct I2C16Reg : Register<I2C16Device, 0x0100, bits32, RW, 0> {};

using I2c16Transport =
    I2cTransport<MockI2C16Addr, std::true_type, AssertOnError, std::uint16_t, std::endian::big, std::endian::little>;

void test_i2c_16bit_address(TestContext& t) {
    MockI2C16Addr i2c;
    const I2c16Transport transport(i2c, 0x50);

    transport.write(I2C16Reg::value(0xCAFE'BABEU));
    auto val = transport.read(I2C16Reg{});
    t.eq(val.bits(), 0xCAFE'BABEU);
}

/// @brief I2C mock supporting 16-bit register addresses in little-endian order.
struct MockI2C16AddrLE {
    mutable std::array<std::uint8_t, 65536> memory{};

    struct Result {
        explicit operator bool() const { return success; }
        bool success = true;
    };

    Result write(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> data) const {
        if (data.size() < 3) {
            return {false};
        }
        // 16-bit address (little-endian): low byte first
        auto const reg_addr = static_cast<uint16_t>(data[0] | (data[1] << 8));
        std::memcpy(&memory[reg_addr], data.data() + 2, data.size() - 2);
        return {true};
    }

    Result write_read(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx) const {
        if (tx.size() < 2) {
            return {false};
        }
        auto const reg_addr = static_cast<uint16_t>(tx[0] | (tx[1] << 8));
        std::memcpy(rx.data(), &memory[reg_addr], rx.size());
        return {true};
    }
};

struct I2C16LEDevice : Device<RW, I2c> {};
struct I2C16LEReg : Register<I2C16LEDevice, 0x0100, bits32, RW, 0> {};

using I2c16LETransport = I2cTransport<MockI2C16AddrLE,
                                      std::true_type,
                                      AssertOnError,
                                      std::uint16_t,
                                      std::endian::little,
                                      std::endian::little>;

void test_i2c_16bit_address_little_endian(TestContext& t) {
    MockI2C16AddrLE i2c;
    const I2c16LETransport transport(i2c, 0x50);

    transport.write(I2C16LEReg::value(0xDEAD'BEEFU));
    auto val = transport.read(I2C16LEReg{});
    t.eq(val.bits(), 0xDEAD'BEEFU);

    // Verify address bytes are in little-endian order: low byte first
    // Register address is 0x0100 → buf[0]=0x00 (low), buf[1]=0x01 (high)
    t.eq(i2c.memory[0x0100], static_cast<uint8_t>(0xEF)); // byte[0] of 0xDEADBEEF LE
    t.eq(i2c.memory[0x0101], static_cast<uint8_t>(0xBE)); // byte[1]
    t.eq(i2c.memory[0x0102], static_cast<uint8_t>(0xAD)); // byte[2]
    t.eq(i2c.memory[0x0103], static_cast<uint8_t>(0xDE)); // byte[3]
}

// =============================================================================
// SPI void-returning HAL
// =============================================================================

void test_spi_void_hal_roundtrip(TestContext& t) {
    MockSpi spi;
    const SpiTransport<MockSpi> transport(spi);

    transport.write(SPIReg32::value(0x12345678U));
    auto val = transport.read(SPIReg32{});
    t.eq(val.bits(), 0x12345678U);
}

} // namespace

inline void run_byte_transport_tests(umi::test::Suite& suite) {
    suite.section("SPI transport (mock)");
    suite.run("write/read", test_spi_write_read);
    suite.run("field read", test_spi_field_read);
    suite.run("modify (RMW)", test_spi_modify_rmw);
    suite.run("high field read", test_spi_high_field_read);

    suite.section("ByteAdapter endian");
    suite.run("little-endian wire format", test_i2c_endian_little_wire_format);
    suite.run("big-endian wire format", test_i2c_endian_big_wire_format);
    suite.run("big-endian roundtrip", test_i2c_endian_big_roundtrip);

    suite.section("16-bit / 64-bit registers");
    suite.run("16-bit register", test_i2c_16bit_register_roundtrip);
    suite.run("16-bit high-byte field", test_i2c_16bit_field_high_byte);
    suite.run("64-bit register", test_i2c_64bit_register_roundtrip);
    suite.run("64-bit low field", test_i2c_64bit_low_field);

    suite.section("Transport error policy");
    suite.run("IgnoreError policy", test_ignore_error_policy);
    suite.run("I2C transport error on write", test_i2c_transport_error_on_write);
    suite.run("I2C transport error on read", test_i2c_transport_error_on_read);
    suite.run("SPI transport error on write", test_spi_transport_error_on_write);
    suite.run("SPI transport error on read", test_spi_transport_error_on_read);
    suite.run("I2C void HAL roundtrip", test_i2c_void_hal_roundtrip);

    suite.section("SPI extended");
    suite.run("custom command bits", test_spi_custom_command_bits);
    suite.run("big-endian data", test_spi_big_endian_data);
    suite.run("16-bit register", test_spi_16bit_register);
    suite.run("void HAL roundtrip", test_spi_void_hal_roundtrip);

    suite.section("I2C extended");
    suite.run("8-bit register", test_i2c_8bit_register);
    suite.run("16-bit address width", test_i2c_16bit_address);
    suite.run("16-bit address width (little-endian)", test_i2c_16bit_address_little_endian);
}

} // namespace umimmio::test
