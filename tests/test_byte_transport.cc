// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for SPI and I2C transports, ByteAdapter endian, and error policies.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include <umimmio/transport/i2c.hh>
#include <umimmio/transport/spi.hh>

#include "test_fixture.hh"

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
struct SPIDevice : Device<RW, Spi> {};
struct SPIReg32 : Register<SPIDevice, 0x10, bits32, RW, 0> {};
struct SPIField8 : Field<SPIReg32, 0, 8, Numeric> {};
struct SPIFieldHigh : Field<SPIReg32, 24, 8> {};

// =============================================================================
// SPI transport tests
// =============================================================================

bool test_spi_write_read(TestContext& t) {
    MockSpi spi;
    SpiTransport<MockSpi> transport(spi);

    transport.write(SPIReg32::value(0xCAFE'BABEU));
    auto val = transport.read(SPIReg32{});
    return t.assert_eq(val.bits(), 0xCAFE'BABEU);
}

bool test_spi_field_read(TestContext& t) {
    MockSpi spi;
    SpiTransport<MockSpi> transport(spi);

    transport.write(SPIReg32::value(0x000000ABU));
    auto val = transport.read(SPIField8{});
    return t.assert_eq(val.bits(), static_cast<uint8_t>(0xAB));
}

bool test_spi_modify_rmw(TestContext& t) {
    MockSpi spi;
    SpiTransport<MockSpi> transport(spi);

    transport.write(SPIReg32::value(0xFF000000U));
    transport.modify(SPIField8::value(static_cast<uint8_t>(0x42)));

    bool ok = true;
    auto val = transport.read(SPIReg32{});
    ok &= t.assert_eq(val.bits() & 0xFFU, 0x42U);
    ok &= t.assert_eq(val.bits() & 0xFF000000U, 0xFF000000U);
    return ok;
}

bool test_spi_high_field_read(TestContext& t) {
    MockSpi spi;
    SpiTransport<MockSpi> transport(spi);

    transport.write(SPIReg32::value(0xAB000000U));
    auto val = transport.read(SPIFieldHigh{});
    return t.assert_eq(val.bits(), static_cast<uint8_t>(0xAB));
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

bool test_i2c_endian_little_wire_format(TestContext& t) {
    LocalMockI2C i2c;
    I2cLE transport(i2c, 0x50);

    transport.write(EndianReg32::value(0x04030201U));

    // In little-endian: byte[0]=0x01, byte[1]=0x02, byte[2]=0x03, byte[3]=0x04
    bool ok = true;
    ok &= t.assert_eq(i2c.memory[0x10], static_cast<uint8_t>(0x01));
    ok &= t.assert_eq(i2c.memory[0x11], static_cast<uint8_t>(0x02));
    ok &= t.assert_eq(i2c.memory[0x12], static_cast<uint8_t>(0x03));
    ok &= t.assert_eq(i2c.memory[0x13], static_cast<uint8_t>(0x04));
    return ok;
}

bool test_i2c_endian_big_wire_format(TestContext& t) {
    LocalMockI2C i2c;
    I2cBE transport(i2c, 0x50);

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
    I2cBE transport(i2c, 0x50);

    transport.write(EndianReg32::value(0xDEAD'BEEFU));
    auto val = transport.read(EndianReg32{});
    return t.assert_eq(val.bits(), 0xDEAD'BEEFU);
}

// =============================================================================
// 16-bit register tests
// =============================================================================

struct Reg16 : Register<EndianDevice, 0x20, bits16, RW, 0> {};
struct Field16High : Field<Reg16, 8, 8> {};

bool test_i2c_16bit_register_roundtrip(TestContext& t) {
    LocalMockI2C i2c;
    I2cTransport<LocalMockI2C> transport(i2c, 0x50);

    transport.write(Reg16::value(static_cast<uint16_t>(0xABCD)));
    auto val = transport.read(Reg16{});
    return t.assert_eq(val.bits(), static_cast<uint16_t>(0xABCD));
}

bool test_i2c_16bit_field_high_byte(TestContext& t) {
    LocalMockI2C i2c;
    I2cTransport<LocalMockI2C> transport(i2c, 0x50);

    transport.write(Reg16::value(static_cast<uint16_t>(0xAB00)));
    auto val = transport.read(Field16High{});
    return t.assert_eq(val.bits(), static_cast<uint8_t>(0xAB));
}

// =============================================================================
// 64-bit register tests
// =============================================================================

struct Reg64 : Register<EndianDevice, 0x30, bits64, RW, 0> {};
struct Field64Low : Field<Reg64, 0, 32> {};

bool test_i2c_64bit_register_roundtrip(TestContext& t) {
    LocalMockI2C i2c;
    I2cTransport<LocalMockI2C> transport(i2c, 0x50);

    transport.write(Reg64::value(0x0102030405060708ULL));
    auto val = transport.read(Reg64{});
    return t.assert_eq(val.bits(), 0x0102030405060708ULL);
}

bool test_i2c_64bit_low_field(TestContext& t) {
    LocalMockI2C i2c;
    I2cTransport<LocalMockI2C> transport(i2c, 0x50);

    transport.write(Reg64::value(0xAAAABBBBCCCCDDDDULL));
    auto val = transport.read(Field64Low{});
    return t.assert_eq(val.bits(), 0xCCCCDDDDU);
}

// =============================================================================
// Transport error policy tests
// =============================================================================

struct IgnoreReg : Register<EndianDevice, 0x00, bits32, RW, 0> {};

bool test_ignore_error_policy(TestContext& t) {
    LocalMockI2C i2c;
    I2cTransport<LocalMockI2C, std::false_type, IgnoreError> transport(i2c, 0x50);

    transport.write(IgnoreReg::value(0x12345678U));
    auto val = transport.read(IgnoreReg{});
    return t.assert_eq(val.bits(), 0x12345678U);
}

/// @brief I2C mock that always fails (returns false from write/write_read).
struct FailingI2C {
    struct Result {
        explicit operator bool() const { return success; }
        bool success;
    };

    Result write(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> /*data*/) const { return {false}; }

    Result write_read(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> /*tx*/,
                      std::span<std::uint8_t> /*rx*/) const {
        return {false};
    }
};

/// @brief Error counter for transport error tests.
static int transport_error_count = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) test-only counter
static void count_transport_errors(const char* /*msg*/) { transport_error_count++; }

using CountingErrorPolicy = CustomErrorHandler<count_transport_errors>;

bool test_i2c_transport_error_on_write(TestContext& t) {
    FailingI2C i2c;
    I2cTransport<FailingI2C, std::false_type, CountingErrorPolicy> transport(i2c, 0x50);

    transport_error_count = 0;
    transport.write(IgnoreReg::value(0x12345678U));

    return t.assert_eq(transport_error_count, 1);
}

bool test_i2c_transport_error_on_read(TestContext& t) {
    FailingI2C i2c;
    I2cTransport<FailingI2C, std::false_type, CountingErrorPolicy> transport(i2c, 0x50);

    transport_error_count = 0;
    [[maybe_unused]] auto val = transport.read(IgnoreReg{});

    return t.assert_eq(transport_error_count, 1);
}

/// @brief SPI mock that returns failure from transfer().
struct FailingSpi {
    struct Result {
        explicit operator bool() const { return success; }
        bool success;
    };

    Result transfer(const std::uint8_t* /*tx*/, std::uint8_t* /*rx*/, std::size_t /*size*/) const { return {false}; }
};

bool test_spi_transport_error_on_write(TestContext& t) {
    FailingSpi spi;
    SpiTransport<FailingSpi, std::false_type, CountingErrorPolicy> transport(spi);

    transport_error_count = 0;
    transport.write(SPIReg32::value(0xDEADBEEFU));

    return t.assert_eq(transport_error_count, 1);
}

bool test_spi_transport_error_on_read(TestContext& t) {
    FailingSpi spi;
    SpiTransport<FailingSpi, std::false_type, CountingErrorPolicy> transport(spi);

    transport_error_count = 0;
    [[maybe_unused]] auto val = transport.read(SPIReg32{});

    return t.assert_eq(transport_error_count, 1);
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

bool test_i2c_void_hal_roundtrip(TestContext& t) {
    VoidI2C i2c;
    I2cTransport<VoidI2C> transport(i2c, 0x50);

    transport.write(EndianReg32::value(0xCAFEBABEU));
    auto val = transport.read(EndianReg32{});
    return t.assert_eq(val.bits(), 0xCAFEBABEU);
}

} // namespace

void run_byte_transport_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("SPI transport (mock)");
    suite.run("write/read", test_spi_write_read);
    suite.run("field read", test_spi_field_read);
    suite.run("modify (RMW)", test_spi_modify_rmw);
    suite.run("high field read", test_spi_high_field_read);

    umi::test::Suite::section("ByteAdapter endian");
    suite.run("little-endian wire format", test_i2c_endian_little_wire_format);
    suite.run("big-endian wire format", test_i2c_endian_big_wire_format);
    suite.run("big-endian roundtrip", test_i2c_endian_big_roundtrip);

    umi::test::Suite::section("16-bit / 64-bit registers");
    suite.run("16-bit register", test_i2c_16bit_register_roundtrip);
    suite.run("16-bit high-byte field", test_i2c_16bit_field_high_byte);
    suite.run("64-bit register", test_i2c_64bit_register_roundtrip);
    suite.run("64-bit low field", test_i2c_64bit_low_field);

    umi::test::Suite::section("Transport error policy");
    suite.run("IgnoreError policy", test_ignore_error_policy);
    suite.run("I2C transport error on write", test_i2c_transport_error_on_write);
    suite.run("I2C transport error on read", test_i2c_transport_error_on_read);
    suite.run("SPI transport error on write", test_spi_transport_error_on_write);
    suite.run("SPI transport error on read", test_spi_transport_error_on_read);
    suite.run("I2C void HAL roundtrip", test_i2c_void_hal_roundtrip);
}

} // namespace umimmio::test
