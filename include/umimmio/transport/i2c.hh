#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file i2c.hh
/// @brief I2C transport implementation for HAL-compatible drivers.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

#include "../register.hh"

namespace umi::mmio {

/// @brief I2C register transport for HAL-compatible I2C drivers.
///
/// Bridges register read/write operations to an I2C HAL driver using
/// ByteAdapter for endian conversion and address encoding.
///
/// @tparam I2C         HAL driver type providing write() and write_read().
/// @tparam CheckPolicy Enable runtime range checks (std::true_type or std::false_type).
/// @tparam ErrorPolicy Error handler (default: AssertOnError).
/// @tparam AddressType Register address type (uint8_t or uint16_t).
/// @tparam AddrEndian  Address byte order on the wire.
/// @tparam DataEndian  Data byte order on the wire.
/// @pre I2C driver must be initialized before use.
template <typename I2C,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressType = std::uint8_t,
          Endian AddrEndian = Endian::BIG,
          Endian DataEndian = Endian::LITTLE>
class I2cTransport
    : public ByteAdapter<I2cTransport<I2C, CheckPolicy, ErrorPolicy, AddressType, AddrEndian, DataEndian>,
                         CheckPolicy,
                         ErrorPolicy,
                         AddressType,
                         DataEndian> {
    I2C& i2c_driver;
    std::uint8_t device_addr;

  public:
    using TransportTag = I2CTransportTag;

    /// @brief Construct an I2C transport bound to a driver and device address.
    /// @param i2c  Reference to the HAL I2C driver.
    /// @param addr 7-bit device address (left-shifted by 1).
    explicit I2cTransport(I2C& i2c, std::uint8_t addr) : i2c_driver(i2c), device_addr(addr) {}

    /// @brief Write raw bytes to a register address over I2C.
    /// @param reg_addr Register address.
    /// @param data     Pointer to data bytes.
    /// @param size     Number of data bytes (max 8).
    void raw_write(AddressType reg_addr, const void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressType);
        static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");
        assert(size <= 8 && "Register size must be <= 64 bits");

        std::array<std::uint8_t, 2 + 8> buffer{};
        if constexpr (addr_size == 1) {
            buffer[0] = static_cast<std::uint8_t>(reg_addr);
        } else {
            if constexpr (AddrEndian == Endian::LITTLE) {
                buffer[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
                buffer[1] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
            } else {
                buffer[0] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
                buffer[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
            }
        }

        std::memcpy(buffer.data() + addr_size, data, size);
        auto payload = std::span<const std::uint8_t>(buffer.data(), addr_size + size);
        [[maybe_unused]] auto result = i2c_driver.write(device_addr >> 1, payload);
    }

    /// @brief Read raw bytes from a register address over I2C.
    /// @param reg_addr Register address.
    /// @param data     Pointer to receive buffer.
    /// @param size     Number of bytes to read (max 8).
    void raw_read(AddressType reg_addr, void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressType);
        static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");
        assert(size <= 8 && "Register size must be <= 64 bits");

        std::array<std::uint8_t, 2> addr_bytes{};
        if constexpr (addr_size == 1) {
            addr_bytes[0] = static_cast<std::uint8_t>(reg_addr);
        } else {
            if constexpr (AddrEndian == Endian::LITTLE) {
                addr_bytes[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
                addr_bytes[1] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
            } else {
                addr_bytes[0] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
                addr_bytes[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
            }
        }

        std::array<std::uint8_t, 8> buffer{};
        auto tx = std::span<const std::uint8_t>(addr_bytes.data(), addr_size);
        auto rx = std::span<std::uint8_t>(buffer.data(), size);
        auto result = i2c_driver.write_read(device_addr >> 1, tx, rx);
        if (!result) {
            std::memset(data, 0, size);
            return;
        }
        std::memcpy(data, buffer.data(), size);
    }
};

} // namespace umi::mmio
