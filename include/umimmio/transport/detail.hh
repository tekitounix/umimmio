#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file detail.hh
/// @brief Shared helpers for transport implementations.
/// @author Shota Moriguchi @tekitounix

#include <bit>
#include <cstddef>
#include <cstdint>

namespace umi::mmio::detail {

/// @brief Encode a multi-byte register address into a buffer.
/// @tparam AddrEndian Byte order for multi-byte addresses on the wire.
/// @tparam AddressType Address type (uint8_t or uint16_t).
/// @param reg_addr Register address.
/// @param buf Output buffer (must be >= sizeof(AddressType) bytes).
/// @return Number of bytes written (sizeof(AddressType)).
template <std::endian AddrEndian, typename AddressType>
constexpr std::size_t encode_address(AddressType reg_addr, std::uint8_t* buf) noexcept {
    constexpr std::size_t addr_size = sizeof(AddressType);
    static_assert(addr_size == 1 || addr_size == 2, "AddressType must be 8 or 16 bit");

    if constexpr (addr_size == 1) {
        buf[0] = static_cast<std::uint8_t>(reg_addr);
    } else if constexpr (AddrEndian == std::endian::big) {
        buf[0] = static_cast<std::uint8_t>(reg_addr >> 8);
        buf[1] = static_cast<std::uint8_t>(reg_addr & 0xFF);
    } else {
        buf[0] = static_cast<std::uint8_t>(reg_addr & 0xFF);
        buf[1] = static_cast<std::uint8_t>(reg_addr >> 8);
    }
    return addr_size;
}

/// @brief Encode a register address with SPI command bit applied to first byte.
/// @tparam AddrEndian Byte order for multi-byte addresses on the wire.
/// @tparam AddressType Address type (uint8_t or uint16_t).
/// @tparam RwBit Bit ORed into the first address byte (ReadBit or WriteBit).
/// @tparam CmdMask Mask applied to the first address byte before ORing RwBit.
/// @param reg_addr Register address.
/// @param buf Output buffer (must be >= sizeof(AddressType) bytes).
/// @return Number of bytes written (sizeof(AddressType)).
template <std::endian AddrEndian, typename AddressType, std::uint8_t RwBit, std::uint8_t CmdMask>
constexpr std::size_t encode_spi_address(AddressType reg_addr, std::uint8_t* buf) noexcept {
    auto n = encode_address<AddrEndian>(reg_addr, buf);
    buf[0] = (buf[0] & CmdMask) | RwBit;
    return n;
}

} // namespace umi::mmio::detail
