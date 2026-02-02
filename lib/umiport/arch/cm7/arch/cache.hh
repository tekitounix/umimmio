// SPDX-License-Identifier: MIT
// Cortex-M7 Cache Control
#pragma once

#include <cstdint>

namespace umi::cm7 {

/// Cortex-M7 System Control Block cache registers
namespace scb {
constexpr std::uintptr_t SCB_BASE = 0xE000'ED00;

inline auto* CCSIDR  = reinterpret_cast<volatile std::uint32_t*>(0xE000'ED80);
inline auto* CSSELR  = reinterpret_cast<volatile std::uint32_t*>(0xE000'ED84);
inline auto* CCR     = reinterpret_cast<volatile std::uint32_t*>(SCB_BASE + 0x14);
inline auto* ICIALLU = reinterpret_cast<volatile std::uint32_t*>(0xE000'EF50);
inline auto* DCIMVAC = reinterpret_cast<volatile std::uint32_t*>(0xE000'EF5C);
inline auto* DCISW   = reinterpret_cast<volatile std::uint32_t*>(0xE000'EF60);
inline auto* DCCMVAC = reinterpret_cast<volatile std::uint32_t*>(0xE000'EF68);
inline auto* DCCSW   = reinterpret_cast<volatile std::uint32_t*>(0xE000'EF6C);

constexpr std::uint32_t CCR_IC = 1U << 17;  // I-Cache enable
constexpr std::uint32_t CCR_DC = 1U << 16;  // D-Cache enable
} // namespace scb

/// Enable instruction cache
inline void enable_icache() {
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");
    *scb::ICIALLU = 0;  // Invalidate entire I-Cache
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");
    *scb::CCR |= scb::CCR_IC;
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");
}

/// Enable data cache
inline void enable_dcache() {
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");

    // Select Level 1 Data cache
    *scb::CSSELR = 0;
    asm volatile("dsb sy" ::: "memory");

    // Invalidate entire D-Cache by set/way
    auto ccsidr = *scb::CCSIDR;
    auto sets = (ccsidr >> 13) & 0x7FFF;
    auto ways = (ccsidr >> 3) & 0x3FF;

    for (std::uint32_t set = 0; set <= sets; ++set) {
        for (std::uint32_t way = 0; way <= ways; ++way) {
            *scb::DCISW = (set << 5) | (way << 30);
        }
    }
    asm volatile("dsb sy" ::: "memory");

    *scb::CCR |= scb::CCR_DC;
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");
}

/// Enable FPU (single + double precision on CM7)
inline void enable_fpu() {
    // CPACR: enable CP10 and CP11 (FPU) full access
    auto* CPACR = reinterpret_cast<volatile std::uint32_t*>(0xE000'ED88);
    *CPACR |= (0xFU << 20);  // CP10 + CP11 full access
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");
}

// ============================================================================
// MPU (Memory Protection Unit)
// ============================================================================

// NOLINTBEGIN(readability-identifier-naming)
namespace mpu {
inline auto* TYPE = reinterpret_cast<volatile std::uint32_t*>(0xE000'ED90);
inline auto* CTRL = reinterpret_cast<volatile std::uint32_t*>(0xE000'ED94);
inline auto* RNR  = reinterpret_cast<volatile std::uint32_t*>(0xE000'ED98);
inline auto* RBAR = reinterpret_cast<volatile std::uint32_t*>(0xE000'ED9C);
inline auto* RASR = reinterpret_cast<volatile std::uint32_t*>(0xE000'EDA0);

constexpr std::uint32_t RASR_ENABLE  = 1U << 0;
constexpr std::uint32_t RASR_AP_FULL = 0b011U << 24;
constexpr std::uint32_t RASR_C       = 1U << 17;
constexpr std::uint32_t RASR_B       = 1U << 16;
constexpr std::uint32_t RASR_S       = 1U << 18;

constexpr std::uint32_t rasr_size(std::uint32_t n) { return (n & 0x1F) << 1; }
constexpr std::uint32_t rasr_tex(std::uint32_t n) { return (n & 0x7) << 19; }

constexpr std::uint32_t SIZE_4KB  = 11;
constexpr std::uint32_t SIZE_32KB = 14;
constexpr std::uint32_t SIZE_64MB = 25;

constexpr std::uint32_t CTRL_ENABLE     = 1U << 0;
constexpr std::uint32_t CTRL_PRIVDEFENA = 1U << 2;
} // namespace mpu
// NOLINTEND(readability-identifier-naming)

/// Configure MPU regions matching libDaisy's ConfigureMpu():
/// Region 0: D2 SRAM (0x30000000, 32KB) — Non-cacheable, Shareable
/// Region 1: SDRAM   (0xC0000000, 64MB) — Cacheable, Bufferable
/// Region 2: Backup SRAM (0x38800000, 4KB) — Non-cacheable, Shareable
inline void configure_mpu() {
    asm volatile("dsb sy" ::: "memory");

    // Disable MPU
    *mpu::CTRL = 0;

    // Region 0: D2 SRAM — Strongly-Ordered (TEX=0,C=0,B=0,S=1)
    // Guarantees no D-Cache caching, strict ordering for DMA buffers
    *mpu::RNR = 0;
    *mpu::RBAR = 0x3000'0000;
    *mpu::RASR = mpu::RASR_ENABLE | mpu::rasr_size(mpu::SIZE_32KB)
               | mpu::RASR_AP_FULL | mpu::rasr_tex(0) | mpu::RASR_S;

    // Region 1: SDRAM — cacheable + bufferable (normal memory, write-back)
    *mpu::RNR = 1;
    *mpu::RBAR = 0xC000'0000;
    *mpu::RASR = mpu::RASR_ENABLE | mpu::rasr_size(mpu::SIZE_64MB)
               | mpu::RASR_AP_FULL | mpu::rasr_tex(0) | mpu::RASR_C | mpu::RASR_B;

    // Region 2: Backup SRAM — non-cacheable, shareable
    *mpu::RNR = 2;
    *mpu::RBAR = 0x3880'0000;
    *mpu::RASR = mpu::RASR_ENABLE | mpu::rasr_size(mpu::SIZE_4KB)
               | mpu::RASR_AP_FULL | mpu::rasr_tex(1) | mpu::RASR_S;

    // Enable MPU with privileged default map
    *mpu::CTRL = mpu::CTRL_ENABLE | mpu::CTRL_PRIVDEFENA;
    asm volatile("dsb sy\nisb sy" ::: "memory");
}

} // namespace umi::cm7
