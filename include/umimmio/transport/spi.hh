#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file spi.hh
/// @brief SPI transport with runtime driver reference.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "../ops.hh"
#include "detail.hh"

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
    using TransportTag = Spi;

    /// @brief Construct a SPI transport.
    /// @param dev Reference to SPI device.
    explicit SpiTransport(SpiDevice& dev) noexcept : spi(dev) {}

    /// @brief Write register data over SPI.
    /// @note If the SPI driver's transfer() returns a type convertible to bool,
    ///       a failure triggers ErrorPolicy::on_transport_error().
    void raw_write(AddressWidth reg_addr, const void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressWidth);

        std::array<std::uint8_t, addr_size + max_reg_bytes> tx_buf{};
        detail::encode_spi_address<AddrEndian, AddressWidth, WriteBit, CmdMask>(reg_addr, tx_buf.data());
        std::memcpy(tx_buf.data() + addr_size, data, size);

        if constexpr (std::is_void_v<decltype(spi.transfer(tx_buf.data(), nullptr, addr_size + size))>) {
            spi.transfer(tx_buf.data(), nullptr, addr_size + size);
        } else {
            if (!spi.transfer(tx_buf.data(), nullptr, addr_size + size)) {
                ErrorPolicy::on_transport_error("SPI write failed");
            }
        }
    }

    /// @brief Read register data over SPI.
    /// @note If the SPI driver's transfer() returns a type convertible to bool,
    ///       a failure triggers ErrorPolicy::on_transport_error().
    void raw_read(AddressWidth reg_addr, void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressWidth);

        std::array<std::uint8_t, addr_size + max_reg_bytes> tx_buf{};
        std::array<std::uint8_t, addr_size + max_reg_bytes> rx_buf{};
        detail::encode_spi_address<AddrEndian, AddressWidth, ReadBit, CmdMask>(reg_addr, tx_buf.data());

        if constexpr (std::is_void_v<decltype(spi.transfer(tx_buf.data(), rx_buf.data(), addr_size + size))>) {
            spi.transfer(tx_buf.data(), rx_buf.data(), addr_size + size);
        } else {
            if (!spi.transfer(tx_buf.data(), rx_buf.data(), addr_size + size)) {
                ErrorPolicy::on_transport_error("SPI read failed");
            }
        }
        std::memcpy(data, rx_buf.data() + addr_size, size);
    }
};

} // namespace umi::mmio
