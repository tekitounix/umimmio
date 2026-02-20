#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file bitbang_spi.hh
/// @brief Bit-bang SPI transport implementation (mode 0, MSB first).
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "../register.hh"

namespace umi::mmio {

/// @brief Bit-bang SPI transport (mode 0, MSB first) via GPIO pins.
///
/// Implements software SPI by driving CS/SCK/MOSI and reading MISO through
/// a GPIO pin abstraction. Address bytes carry read/write command bits.
///
/// @tparam Pins        GPIO abstraction providing cs/sck/mosi/miso control.
/// @tparam CheckPolicy Enable runtime range checks.
/// @tparam ErrorPolicy Error handler (default: AssertOnError).
/// @tparam AddressType Register address type (uint8_t or uint16_t).
/// @tparam AddrEndian  Address byte order on the wire.
/// @tparam DataEndian  Data byte order on the wire.
/// @tparam ReadBit     Bit ORed into address byte for reads (default: 0x80).
/// @tparam CmdMask     Mask applied to address byte before OR (default: 0x7F).
/// @tparam WriteBit    Bit ORed into address byte for writes (default: 0x00).
/// @note Pins must provide: cs_low(), cs_high(), sck_low(), sck_high(),
///       mosi_high(), mosi_low(), miso_read() -> bool, delay().
template <typename Pins,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressType = std::uint8_t,
          Endian AddrEndian = Endian::BIG,
          Endian DataEndian = Endian::LITTLE,
          std::uint8_t ReadBit = 0x80,
          std::uint8_t CmdMask = 0x7F,
          std::uint8_t WriteBit = 0x00>
class BitBangSpiTransport : public ByteAdapter<BitBangSpiTransport<Pins,
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
    Pins& pins;

  public:
    using TransportTag = SPITransportTag;

    /// @brief Construct a bit-bang SPI transport.
    /// @param p Reference to GPIO pin abstraction.
    explicit BitBangSpiTransport(Pins& p) noexcept : pins(p) {}

    /// @brief Write raw bytes to a register address via bit-bang SPI.
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

        pins.cs_low();
        for (std::size_t i = 0; i < addr_size + size; ++i) {
            transfer_byte(tx_buf[i], nullptr);
        }
        pins.cs_high();
    }

    /// @brief Read raw bytes from a register address via bit-bang SPI.
    /// @param reg_addr Register address (ReadBit is ORed in).
    /// @param data     Pointer to receive buffer.
    /// @param size     Number of bytes to read (max 8).
    void raw_read(AddressType reg_addr, void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressType);
        static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");
        assert(size <= 8 && "Register size must be <= 64 bits");

        std::array<std::uint8_t, 2 + 8> tx_buf{};
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

        pins.cs_low();
        for (std::size_t i = 0; i < addr_size; ++i) {
            transfer_byte(tx_buf[i], nullptr);
        }
        auto* rx = static_cast<std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            rx[i] = transfer_byte(0x00, nullptr);
        }
        pins.cs_high();
    }

  private:
    /// @brief Transfer one byte full-duplex (mode 0, MSB first).
    /// @param tx Byte to transmit.
    /// @return Byte received from MISO.
    std::uint8_t transfer_byte(std::uint8_t tx, [[maybe_unused]] std::uint8_t* rx) const noexcept {
        std::uint8_t value = 0;
        for (int i = 7; i >= 0; --i) {
            if ((tx & static_cast<std::uint8_t>(1U << i)) != 0) {
                pins.mosi_high();
            } else {
                pins.mosi_low();
            }
            pins.sck_high();
            pins.delay();
            if (pins.miso_read()) {
                value |= static_cast<std::uint8_t>(1U << i);
            }
            pins.sck_low();
            pins.delay();
        }
        return value;
    }
};

} // namespace umi::mmio
