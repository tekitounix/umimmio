#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file spi.hh
/// @brief SPI transport with runtime driver reference.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cstdint>
#include <cstring>

#include "../register.hh"

namespace umi::mmio {

/// @brief SPI transport with runtime driver reference.
///
/// @tparam SpiDevice   SPI device type providing transfer().
/// @tparam CheckPolicy Enable runtime range checks.
/// @tparam ErrorPolicy Error handler for range violations.
/// @tparam AddressWidth Register address width on the bus.
/// @tparam AddrEndian  Byte order for multi-byte addresses on the wire.
/// @tparam DataEndian  Byte order for data on the wire.
/// @tparam ReadBit     Bit ORed into address byte for reads (default: 0x80).
/// @tparam CmdMask     Mask applied to address byte before OR (default: 0x7F).
/// @tparam WriteBit    Bit ORed into address byte for writes (default: 0x00).
template <typename SpiDevice,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressWidth = std::uint8_t,
          std::endian AddrEndian = std::endian::big,
          std::endian DataEndian = std::endian::little,
          std::uint8_t ReadBit = 0x80,
          std::uint8_t CmdMask = 0x7F,
          std::uint8_t WriteBit = 0x00>
class SpiTransport : public ByteAdapter<CheckPolicy, ErrorPolicy, AddressWidth, DataEndian> {
    SpiDevice& spi;

  public:
    using TransportTag = SPITransportTag;

    /// @brief Construct a SPI transport.
    /// @param dev Reference to SPI device.
    explicit SpiTransport(SpiDevice& dev) noexcept : spi(dev) {}

    /// @brief Write register data over SPI.
    void raw_write(AddressWidth reg_addr, const void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressWidth);

        std::array<std::uint8_t, addr_size + 8> tx_buf{};
        if constexpr (addr_size == 1) {
            tx_buf[0] = (static_cast<std::uint8_t>(reg_addr) & CmdMask) | WriteBit;
        } else {
            if constexpr (AddrEndian == std::endian::little) {
                tx_buf[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
                tx_buf[1] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
            } else {
                tx_buf[0] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
                tx_buf[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
            }
            tx_buf[0] = (tx_buf[0] & CmdMask) | WriteBit;
        }
        std::memcpy(tx_buf.data() + addr_size, data, size);

        spi.transfer(tx_buf.data(), nullptr, addr_size + size);
    }

    /// @brief Read register data over SPI.
    void raw_read(AddressWidth reg_addr, void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressWidth);

        std::array<std::uint8_t, addr_size + 8> tx_buf{};
        std::array<std::uint8_t, addr_size + 8> rx_buf{};
        if constexpr (addr_size == 1) {
            tx_buf[0] = (static_cast<std::uint8_t>(reg_addr) & CmdMask) | ReadBit;
        } else {
            if constexpr (AddrEndian == std::endian::little) {
                tx_buf[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
                tx_buf[1] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
            } else {
                tx_buf[0] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
                tx_buf[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
            }
            tx_buf[0] = (tx_buf[0] & CmdMask) | ReadBit;
        }

        spi.transfer(tx_buf.data(), rx_buf.data(), addr_size + size);
        std::memcpy(data, rx_buf.data() + addr_size, size);
    }
};

} // namespace umi::mmio
