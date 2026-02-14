// SPDX-License-Identifier: MIT
// UMI-OS Block Device Abstraction
// Unified interface for Flash, SD, FRAM, etc.

#pragma once

#include <concepts>
#include <cstdint>

namespace umi::kernel {

/// Block device concept (19-storage-service.md)
///
/// Implementations: InternalFlash, SPI Flash (W25Qxx), SD (SPI/SDIO), FRAM/EEPROM
template <typename T>
concept BlockDeviceLike = requires(T& dev, uint32_t block, uint32_t offset, void* buf, const void* cbuf,
                                   uint32_t size) {
    { dev.read(block, offset, buf, size) } -> std::convertible_to<int>;
    { dev.write(block, offset, cbuf, size) } -> std::convertible_to<int>;
    { dev.erase(block) } -> std::convertible_to<int>;
    { dev.block_size() } -> std::convertible_to<uint32_t>;
    { dev.block_count() } -> std::convertible_to<uint32_t>;
};

/// Static block device info for compile-time configuration
struct BlockDeviceConfig {
    uint32_t block_size;
    uint32_t block_count;
    uint32_t read_size;   ///< Minimum read unit (bytes)
    uint32_t write_size;  ///< Minimum write/program unit (bytes)
};

} // namespace umi::kernel
