// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file spi.hh
/// @brief SPI transport implementation for byte-addressed devices.
/// @author Shota Moriguchi @tekitounix

#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "../register.hh"

namespace umi::mmio {

/// @brief SPI register transport for byte-addressed devices.
///
/// Uses a HAL SPI driver with full-duplex transfer(). Address bytes carry
/// read/write command bits controlled by ReadBit, WriteBit, and CmdMask.
///
/// @tparam SpiDevice   HAL driver type providing transfer(tx, rx, size).
/// @tparam CheckPolicy Enable runtime range checks.
/// @tparam ErrorPolicy Error handler (default: AssertOnError).
/// @tparam AddressType Register address type (uint8_t or uint16_t).
/// @tparam AddrEndian  Address byte order on the wire.
/// @tparam DataEndian  Data byte order on the wire.
/// @tparam ReadBit     Bit ORed into address byte for reads (default: 0x80).
/// @tparam CmdMask     Mask applied to address byte before OR (default: 0x7F).
/// @tparam WriteBit    Bit ORed into address byte for writes (default: 0x00).
template <typename SpiDevice,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressType = std::uint8_t,
          Endian AddrEndian = Endian::BIG,
          Endian DataEndian = Endian::LITTLE,
          std::uint8_t ReadBit = 0x80,
          std::uint8_t CmdMask = 0x7F,
          std::uint8_t WriteBit = 0x00>
class SpiTransport : public ByteAdapter<SpiTransport<SpiDevice,
                                                     CheckPolicy,
                                                     ErrorPolicy,
                                                     AddressType,
                                                     AddrEndian,
                                                     DataEndian,
                                                     ReadBit,
                                                     CmdMask,
                                                     WriteBit>,
                                        CheckPolicy,
                                        ErrorPolicy,
                                        AddressType,
                                        DataEndian> {
    SpiDevice& device;

  public:
    using TransportTag = SPITransportTag;

    /// @brief Construct an SPI transport bound to a HAL device.
    /// @param dev Reference to the HAL SPI driver.
    explicit SpiTransport(SpiDevice& dev) noexcept : device(dev) {}

    /// @brief Write raw bytes to a register address over SPI.
    /// @param reg_addr Register address (WriteBit is ORed in).
    /// @param data     Pointer to data bytes.
    /// @param size     Number of data bytes (max 8).
    void raw_write(AddressType reg_addr, const void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressType);
        static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");
        assert(size <= 8 && "Register size must be <= 64 bits");

        std::array<std::uint8_t, 2 + 8> tx_buf{};
        if constexpr (addr_size == 1) {
            tx_buf[0] = (static_cast<std::uint8_t>(reg_addr) & CmdMask) | WriteBit;
        } else {
            if constexpr (AddrEndian == Endian::LITTLE) {
                tx_buf[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
                tx_buf[1] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
            } else {
                tx_buf[0] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
                tx_buf[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
            }
            tx_buf[0] = (tx_buf[0] & CmdMask) | WriteBit;
        }

        std::memcpy(tx_buf.data() + addr_size, data, size);
        device.transfer(tx_buf.data(), nullptr, addr_size + size);
    }

    /// @brief Read raw bytes from a register address over SPI.
    /// @param reg_addr Register address (ReadBit is ORed in).
    /// @param data     Pointer to receive buffer.
    /// @param size     Number of bytes to read (max 8).
    void raw_read(AddressType reg_addr, void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressType);
        static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");
        assert(size <= 8 && "Register size must be <= 64 bits");

        std::array<std::uint8_t, 2 + 8> tx_buf{};
        std::array<std::uint8_t, 2 + 8> rx_buf{};
        if constexpr (addr_size == 1) {
            tx_buf[0] = (static_cast<std::uint8_t>(reg_addr) & CmdMask) | ReadBit;
        } else {
            if constexpr (AddrEndian == Endian::LITTLE) {
                tx_buf[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
                tx_buf[1] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
            } else {
                tx_buf[0] = static_cast<std::uint8_t>((reg_addr >> 8) & 0xFF);
                tx_buf[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
            }
            tx_buf[0] = (tx_buf[0] & CmdMask) | ReadBit;
        }

        device.transfer(tx_buf.data(), rx_buf.data(), addr_size + size);
        std::memcpy(data, rx_buf.data() + addr_size, size);
    }
};

} // namespace umi::mmio
