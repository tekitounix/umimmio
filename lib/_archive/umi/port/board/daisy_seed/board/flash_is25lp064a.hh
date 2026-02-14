// SPDX-License-Identifier: MIT
// IS25LP064A Flash Device Definitions
// Based on libDaisy flash_IS25LP064A.h
#pragma once

#include <cstdint>

namespace umi::flash {

/// IS25LP064A Flash Device Configuration
/// 64Mbit (8MB) Serial NOR Flash with Quad SPI
struct IS25LP064A {
    // ========================================================================
    // Memory Organization
    // ========================================================================
    static constexpr std::uint32_t FLASH_SIZE = 0x800000; // 8MB
    static constexpr std::uint32_t BLOCK_SIZE = 0x10000;  // 64KB block
    static constexpr std::uint32_t SECTOR_SIZE = 0x1000;  // 4KB sector
    static constexpr std::uint32_t PAGE_SIZE = 0x100;     // 256 bytes page

    // ========================================================================
    // Timing (dummy cycles)
    // ========================================================================
    static constexpr std::uint8_t DUMMY_CYCLES_READ = 8;
    static constexpr std::uint8_t DUMMY_CYCLES_READ_QUAD = 6; // For 0xEB at high speed
    static constexpr std::uint8_t DUMMY_CYCLES_READ_DTR = 6;
    static constexpr std::uint8_t DUMMY_CYCLES_FAST_READ = 8; // For 0x0B command

    // ========================================================================
    // Timing (max times in milliseconds)
    // ========================================================================
    static constexpr std::uint32_t DIE_ERASE_MAX_TIME_MS = 460000;
    static constexpr std::uint32_t BLOCK_ERASE_MAX_TIME_MS = 1000;
    static constexpr std::uint32_t SECTOR_ERASE_MAX_TIME_MS = 400;
    static constexpr std::uint32_t PAGE_PROGRAM_MAX_TIME_MS = 3;

    // ========================================================================
    // Commands - Reset Operations
    // ========================================================================
    static constexpr std::uint8_t RESET_ENABLE = 0x66;
    static constexpr std::uint8_t RESET_DEVICE = 0x99;

    // ========================================================================
    // Commands - Identification
    // ========================================================================
    static constexpr std::uint8_t READ_ID = 0x9F;     // JEDEC ID
    static constexpr std::uint8_t READ_ID_ALT = 0xAB; // Release from Deep Power Down / Device ID
    static constexpr std::uint8_t READ_UNIQUE_ID = 0x4B;
    static constexpr std::uint8_t READ_SFDP = 0x5A; // Serial Flash Discoverable Parameters

    // ========================================================================
    // Commands - Read Operations
    // ========================================================================
    static constexpr std::uint8_t READ = 0x03;      // Normal Read (no dummy)
    static constexpr std::uint8_t FAST_READ = 0x0B; // Fast Read (8 dummy cycles)
    static constexpr std::uint8_t FAST_READ_DTR = 0x0D;
    static constexpr std::uint8_t DUAL_OUT_FAST_READ = 0x3B;
    static constexpr std::uint8_t DUAL_IO_FAST_READ = 0xBB;
    static constexpr std::uint8_t DUAL_IO_FAST_READ_DTR = 0xBD;
    static constexpr std::uint8_t QUAD_OUT_FAST_READ = 0x6B;
    static constexpr std::uint8_t QUAD_IO_FAST_READ = 0xEB; // Quad I/O Fast Read
    static constexpr std::uint8_t QUAD_IO_FAST_READ_DTR = 0xED;

    // ========================================================================
    // Commands - Write Operations
    // ========================================================================
    static constexpr std::uint8_t WRITE_ENABLE = 0x06;
    static constexpr std::uint8_t WRITE_DISABLE = 0x04;

    // ========================================================================
    // Commands - Register Operations
    // ========================================================================
    static constexpr std::uint8_t READ_STATUS = 0x05;
    static constexpr std::uint8_t WRITE_STATUS = 0x01;
    static constexpr std::uint8_t READ_FUNCTION = 0x48;
    static constexpr std::uint8_t WRITE_FUNCTION = 0x42;
    static constexpr std::uint8_t READ_READ_PARAM = 0x61;     // Read Read Parameters (IS25LP080D only)
    static constexpr std::uint8_t WRITE_READ_PARAM = 0xC0;    // Write Read Parameters (volatile)
    static constexpr std::uint8_t WRITE_READ_PARAM_NV = 0x65; // Write Read Parameters (non-volatile)

    // ========================================================================
    // Commands - Program Operations
    // ========================================================================
    static constexpr std::uint8_t PAGE_PROGRAM = 0x02;
    static constexpr std::uint8_t QUAD_PAGE_PROGRAM = 0x32;
    static constexpr std::uint8_t QUAD_PAGE_PROGRAM_EXT = 0x38;

    // ========================================================================
    // Commands - Erase Operations
    // ========================================================================
    static constexpr std::uint8_t SECTOR_ERASE = 0xD7;     // 4KB Sector Erase (also 0x20)
    static constexpr std::uint8_t SECTOR_ERASE_ALT = 0x20; // Alternative command
    static constexpr std::uint8_t BLOCK_ERASE_32K = 0x52;  // 32KB Block Erase
    static constexpr std::uint8_t BLOCK_ERASE = 0xD8;      // 64KB Block Erase
    static constexpr std::uint8_t CHIP_ERASE = 0xC7;
    static constexpr std::uint8_t CHIP_ERASE_ALT = 0x60;

    // ========================================================================
    // Commands - Suspend/Resume
    // ========================================================================
    static constexpr std::uint8_t PROG_ERASE_SUSPEND = 0x75;
    static constexpr std::uint8_t PROG_ERASE_SUSPEND_ALT = 0xB0;
    static constexpr std::uint8_t PROG_ERASE_RESUME = 0x7A;
    static constexpr std::uint8_t PROG_ERASE_RESUME_ALT = 0x30;

    // ========================================================================
    // Commands - Power Management
    // ========================================================================
    static constexpr std::uint8_t ENTER_DEEP_POWER_DOWN = 0xB9;
    static constexpr std::uint8_t EXIT_DEEP_POWER_DOWN = 0xAB; // Same as READ_ID_ALT

    // ========================================================================
    // Commands - Quad Mode
    // ========================================================================
    static constexpr std::uint8_t ENTER_QPI = 0x35;
    static constexpr std::uint8_t EXIT_QPI = 0xF5;

    // ========================================================================
    // Commands - Security
    // ========================================================================
    static constexpr std::uint8_t SECTOR_LOCK = 0x24;
    static constexpr std::uint8_t SECTOR_UNLOCK = 0x26;
    static constexpr std::uint8_t INFO_ROW_READ = 0x68;
    static constexpr std::uint8_t INFO_ROW_PROGRAM = 0x62;
    static constexpr std::uint8_t INFO_ROW_ERASE = 0x64;

    // ========================================================================
    // Status Register Bits
    // ========================================================================
    static constexpr std::uint8_t SR_WIP = 0x01;  // Write In Progress
    static constexpr std::uint8_t SR_WEL = 0x02;  // Write Enable Latch
    static constexpr std::uint8_t SR_BP0 = 0x04;  // Block Protect bit 0
    static constexpr std::uint8_t SR_BP1 = 0x08;  // Block Protect bit 1
    static constexpr std::uint8_t SR_BP2 = 0x10;  // Block Protect bit 2
    static constexpr std::uint8_t SR_BP3 = 0x20;  // Block Protect bit 3
    static constexpr std::uint8_t SR_QE = 0x40;   // Quad Enable
    static constexpr std::uint8_t SR_SRWD = 0x80; // Status Register Write Disable

    // ========================================================================
    // Read Parameters Register (for 0xC0 command)
    // Bits [7:5] = Drive Strength (111 = 50%)
    // Bits [4:3] = Dummy Cycles Config (11 = 8 cycles for quad read)
    // Bits [2:1] = Wrap Enable
    // Bits [0]   = Burst Length
    // ========================================================================
    static constexpr std::uint8_t READ_PARAM_DEFAULT = 0xF0; // 50% drive, 8 dummy cycles

    // ========================================================================
    // JEDEC ID
    // ========================================================================
    static constexpr std::uint8_t MANUFACTURER_ID = 0x9D; // ISSI
    static constexpr std::uint16_t DEVICE_ID = 0x6017;    // IS25LP064A
};

// Alias for current flash device
using CurrentFlash = IS25LP064A;

} // namespace umi::flash
