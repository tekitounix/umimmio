// SPDX-License-Identifier: MIT
// Copyright (c) 2025, tekitounix

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace umi::fs {

// ============================================================================
// Logging (disabled — no stdio in embedded)
// ============================================================================

#define LFS_TRACE(...)
#define LFS_DEBUG(...)
#define LFS_WARN(...)
#define LFS_ERROR(...)

// ============================================================================
// Assertions (disabled for production)
// ============================================================================

#define LFS_ASSERT(test) ((void)(test))

// ============================================================================
// Min/Max
// ============================================================================

constexpr uint32_t lfs_max(uint32_t a, uint32_t b) {
    return (a > b) ? a : b;
}

constexpr uint32_t lfs_min(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

// ============================================================================
// Alignment
// ============================================================================

constexpr uint32_t lfs_aligndown(uint32_t a, uint32_t alignment) {
    return a - (a % alignment);
}

constexpr uint32_t lfs_alignup(uint32_t a, uint32_t alignment) {
    return lfs_aligndown(a + alignment - 1, alignment);
}

// ============================================================================
// Bit operations (C++23: std::bit_ceil, std::countr_zero, std::popcount)
// ============================================================================

/// Smallest power of 2 >= a (log2)
constexpr uint32_t lfs_npw2(uint32_t a) {
    if (a <= 1) {
        return 0;
    }
    return static_cast<uint32_t>(std::bit_width(a - 1));
}

/// Count trailing zeros (undefined for 0)
constexpr uint32_t lfs_ctz(uint32_t a) {
    return static_cast<uint32_t>(std::countr_zero(a));
}

/// Population count
constexpr uint32_t lfs_popc(uint32_t a) {
    return static_cast<uint32_t>(std::popcount(a));
}

/// Sequence comparison ignoring overflow
constexpr int lfs_scmp(uint32_t a, uint32_t b) {
    return static_cast<int>(static_cast<unsigned>(a - b));
}

// ============================================================================
// Endian conversion (C++23: std::endian, std::byteswap)
// ============================================================================

constexpr uint32_t lfs_fromle32(uint32_t a) {
    if constexpr (std::endian::native == std::endian::little) {
        return a;
    } else {
        return std::byteswap(a);
    }
}

constexpr uint32_t lfs_tole32(uint32_t a) {
    return lfs_fromle32(a);
}

constexpr uint32_t lfs_frombe32(uint32_t a) {
    if constexpr (std::endian::native == std::endian::big) {
        return a;
    } else {
        return std::byteswap(a);
    }
}

constexpr uint32_t lfs_tobe32(uint32_t a) {
    return lfs_frombe32(a);
}

// ============================================================================
// CRC-32 (polynomial = 0x04c11db7, small lookup table)
// ============================================================================

/// CRC-32 with 4-bit lookup table (defined in lfs_core.cc)
uint32_t lfs_crc(uint32_t crc, const void* buffer, size_t size);

// ============================================================================
// Memory allocation (disabled — no malloc)
// ============================================================================

inline void* lfs_malloc(size_t /*size*/) {
    return nullptr;
}

inline void lfs_free(void* /*p*/) {
}

} // namespace umi::fs
