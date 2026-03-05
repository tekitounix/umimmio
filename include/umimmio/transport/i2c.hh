#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file i2c.hh
/// @brief I2C transport with runtime driver reference.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "../ops.hh"
#include "detail.hh"

namespace umi::mmio {

/// @brief I2C transport with runtime driver reference.
///
/// @tparam I2C         I2C driver type providing write() and write_read().
/// @tparam CheckPolicy Enable runtime range checks.
/// @tparam ErrorPolicy Error handler for range violations.
/// @tparam AddressWidth Register address width on the bus (uint8_t or uint16_t).
/// @tparam AddrEndian  Byte order for register addresses on the wire.
/// @tparam DataEndian  Byte order for data on the wire.
template <typename I2C,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressWidth = std::uint8_t,
          std::endian AddrEndian = std::endian::big,
          std::endian DataEndian = std::endian::little>
class I2cTransport : public ByteAdapter<CheckPolicy, ErrorPolicy, AddressWidth, DataEndian> {
    I2C& i2c;
    std::uint8_t device_addr;

  public:
    using TransportTag = I2CTransportTag;

    /// @brief Construct an I2C transport.
    /// @param bus  Reference to I2C driver.
    /// @param addr Device address (7-bit, left-shifted by 1).
    I2cTransport(I2C& bus, std::uint8_t addr) noexcept : i2c(bus), device_addr(addr) {}

    /// @brief Write register data over I2C.
    void raw_write(AddressWidth reg_addr, const void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressWidth);

        std::array<std::uint8_t, addr_size + max_reg_bytes> buf{};
        detail::encode_address<AddrEndian>(reg_addr, buf.data());
        std::memcpy(&buf[addr_size], data, size);

        i2c.write(device_addr, std::span<const std::uint8_t>{buf.data(), addr_size + size});
    }

    /// @brief Read register data over I2C.
    void raw_read(AddressWidth reg_addr, void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressWidth);

        std::array<std::uint8_t, addr_size> addr_buf{};
        detail::encode_address<AddrEndian>(reg_addr, addr_buf.data());

        i2c.write_read(device_addr,
                       std::span<const std::uint8_t>{addr_buf.data(), addr_size},
                       std::span<std::uint8_t>{static_cast<std::uint8_t*>(data), size});
    }
};

} // namespace umi::mmio
