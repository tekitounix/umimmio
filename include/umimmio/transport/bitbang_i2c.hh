#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file bitbang_i2c.hh
/// @brief Bit-bang I2C transport implementation via GPIO.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "../register.hh"
#include "detail.hh"

namespace umi::mmio {

/// @brief Bit-bang I2C transport via GPIO pins.
///
/// Implements software I2C by driving SCL/SDA through a GPIO abstraction.
/// Supports configurable address/data endianness and error policies.
///
/// @tparam Gpio        GPIO abstraction providing scl/sda high/low, sda_read, delay.
/// @tparam CheckPolicy Enable runtime range checks.
/// @tparam ErrorPolicy Error handler (default: AssertOnError).
/// @tparam AddressType Register address type (uint8_t or uint16_t).
/// @tparam AddrEndian  Address byte order on the wire.
/// @tparam DataEndian  Data byte order on the wire.
/// @note Gpio must provide: scl_high(), scl_low(), sda_high(), sda_low(),
///       sda_read() -> bool, delay().
template <typename Gpio,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressType = std::uint8_t,
          std::endian AddrEndian = std::endian::big,
          std::endian DataEndian = std::endian::little>
class BitBangI2cTransport : public ByteAdapter<CheckPolicy, ErrorPolicy, AddressType, DataEndian> {
    Gpio& gpio;
    std::uint8_t device_addr;

  public:
    using TransportTag = I2CTransportTag;

    /// @brief Construct a bit-bang I2C transport.
    /// @param g    Reference to GPIO abstraction.
    /// @param addr 7-bit device address (left-shifted by 1).
    BitBangI2cTransport(Gpio& g, std::uint8_t addr) noexcept : gpio(g), device_addr(addr) {}

    /// @brief Write raw bytes to a register address via bit-bang I2C.
    /// @param reg_addr Register address.
    /// @param data     Pointer to data bytes.
    /// @param size     Number of data bytes (max 8).
    void raw_write(AddressType reg_addr, const void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressType);
        static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");
        assert(size <= 8 && "Register size must be <= 64 bits");

        std::array<std::uint8_t, 2> addr_bytes{};
        detail::encode_address<AddrEndian>(reg_addr, addr_bytes.data());

        start();
        if (!write_byte((device_addr >> 1) << 1)) {
            stop();
            ErrorPolicy::on_range_error("I2C NACK on address");
            return;
        }
        for (std::size_t i = 0; i < addr_size; ++i) {
            if (!write_byte(addr_bytes[i])) {
                stop();
                ErrorPolicy::on_range_error("I2C NACK on register address");
                return;
            }
        }
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            if (!write_byte(bytes[i])) {
                stop();
                ErrorPolicy::on_range_error("I2C NACK on data");
                return;
            }
        }
        stop();
    }

    /// @brief Read raw bytes from a register address via bit-bang I2C.
    /// @param reg_addr Register address.
    /// @param data     Pointer to receive buffer.
    /// @param size     Number of bytes to read (max 8).
    void raw_read(AddressType reg_addr, void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressType);
        static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");
        assert(size <= 8 && "Register size must be <= 64 bits");

        std::array<std::uint8_t, 2> addr_bytes{};
        detail::encode_address<AddrEndian>(reg_addr, addr_bytes.data());

        start();
        if (!write_byte((device_addr >> 1) << 1)) {
            stop();
            ErrorPolicy::on_range_error("I2C NACK on address");
            return;
        }
        for (std::size_t i = 0; i < addr_size; ++i) {
            if (!write_byte(addr_bytes[i])) {
                stop();
                ErrorPolicy::on_range_error("I2C NACK on register address");
                return;
            }
        }
        start();
        if (!write_byte(((device_addr >> 1) << 1) | 0x01)) {
            stop();
            ErrorPolicy::on_range_error("I2C NACK on read address");
            return;
        }

        auto* bytes = static_cast<std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            bytes[i] = read_byte(i + 1 < size);
        }
        stop();
    }

  private:
    /// @brief Generate I2C START condition (SDA falls while SCL is high).
    void start() const noexcept {
        gpio.sda_high();
        gpio.scl_high();
        gpio.delay();
        gpio.sda_low();
        gpio.delay();
        gpio.scl_low();
    }

    /// @brief Generate I2C STOP condition (SDA rises while SCL is high).
    void stop() const noexcept {
        gpio.sda_low();
        gpio.delay();
        gpio.scl_high();
        gpio.delay();
        gpio.sda_high();
        gpio.delay();
    }

    /// @brief Transmit one byte MSB-first and check ACK.
    /// @return true if ACK received, false on NACK.
    [[nodiscard]] bool write_byte(std::uint8_t byte) const noexcept {
        for (int i = 7; i >= 0; --i) {
            if ((byte & (1U << i)) != 0) {
                gpio.sda_high();
            } else {
                gpio.sda_low();
            }
            gpio.delay();
            gpio.scl_high();
            gpio.delay();
            gpio.scl_low();
        }
        gpio.sda_high(); // release
        gpio.delay();
        gpio.scl_high();
        gpio.delay();
        bool const ack = !gpio.sda_read();
        gpio.scl_low();
        return ack;
    }

    /// @brief Receive one byte MSB-first, sending ACK or NACK.
    /// @param ack true to send ACK (more bytes to follow), false for NACK (last byte).
    /// @return Received byte value.
    [[nodiscard]] std::uint8_t read_byte(bool ack) const noexcept {
        std::uint8_t byte = 0;
        gpio.sda_high(); // release
        for (int i = 7; i >= 0; --i) {
            gpio.scl_high();
            gpio.delay();
            if (gpio.sda_read()) {
                byte |= static_cast<std::uint8_t>(1U << i);
            }
            gpio.scl_low();
            gpio.delay();
        }
        if (ack) {
            gpio.sda_low();
        } else {
            gpio.sda_high();
        }
        gpio.delay();
        gpio.scl_high();
        gpio.delay();
        gpio.scl_low();
        gpio.sda_high();
        return byte;
    }
};

} // namespace umi::mmio
