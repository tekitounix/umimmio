// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022, The littlefs authors.
// Copyright (c) 2017, Arm Limited. All rights reserved.
// C++23 port for UMI framework — utility functions

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace umi::fs::lfs {

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

inline constexpr uint32_t lfs_max(uint32_t a, uint32_t b) {
    return (a > b) ? a : b;
}

inline constexpr uint32_t lfs_min(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

// ============================================================================
// Alignment
// ============================================================================

inline constexpr uint32_t lfs_aligndown(uint32_t a, uint32_t alignment) {
    return a - (a % alignment);
}

inline constexpr uint32_t lfs_alignup(uint32_t a, uint32_t alignment) {
    return lfs_aligndown(a + alignment - 1, alignment);
}

// ============================================================================
// Bit operations
// ============================================================================

/// Smallest power of 2 >= a
inline constexpr uint32_t lfs_npw2(uint32_t a) {
#if defined(__GNUC__) || defined(__CC_ARM)
    return 32 - __builtin_clz(a - 1);
#else
    uint32_t r = 0;
    uint32_t s;
    a -= 1;
    s = (a > 0xffff) << 4; a >>= s; r |= s;
    s = (a > 0xff  ) << 3; a >>= s; r |= s;
    s = (a > 0xf   ) << 2; a >>= s; r |= s;
    s = (a > 0x3   ) << 1; a >>= s; r |= s;
    return (r | (a >> 1)) + 1;
#endif
}

/// Count trailing zeros (undefined for 0)
inline constexpr uint32_t lfs_ctz(uint32_t a) {
#if defined(__GNUC__)
    return __builtin_ctz(a);
#else
    return lfs_npw2((a & -a) + 1) - 1;
#endif
}

/// Population count
inline constexpr uint32_t lfs_popc(uint32_t a) {
#if defined(__GNUC__) || defined(__CC_ARM)
    return __builtin_popcount(a);
#else
    a = a - ((a >> 1) & 0x55555555);
    a = (a & 0x33333333) + ((a >> 2) & 0x33333333);
    return (((a + (a >> 4)) & 0x0f0f0f0f) * 0x01010101) >> 24;
#endif
}

/// Sequence comparison ignoring overflow
inline constexpr int lfs_scmp(uint32_t a, uint32_t b) {
    return static_cast<int>(static_cast<unsigned>(a - b));
}

// ============================================================================
// Endian conversion
// ============================================================================

inline uint32_t lfs_fromle32(uint32_t a) {
#if (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return a;
#elif (defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return __builtin_bswap32(a);
#else
    return (static_cast<uint32_t>(reinterpret_cast<uint8_t*>(&a)[0]) << 0) |
           (static_cast<uint32_t>(reinterpret_cast<uint8_t*>(&a)[1]) << 8) |
           (static_cast<uint32_t>(reinterpret_cast<uint8_t*>(&a)[2]) << 16) |
           (static_cast<uint32_t>(reinterpret_cast<uint8_t*>(&a)[3]) << 24);
#endif
}

inline uint32_t lfs_tole32(uint32_t a) {
    return lfs_fromle32(a);
}

inline uint32_t lfs_frombe32(uint32_t a) {
#if (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return __builtin_bswap32(a);
#elif (defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return a;
#else
    return (static_cast<uint32_t>(reinterpret_cast<uint8_t*>(&a)[0]) << 24) |
           (static_cast<uint32_t>(reinterpret_cast<uint8_t*>(&a)[1]) << 16) |
           (static_cast<uint32_t>(reinterpret_cast<uint8_t*>(&a)[2]) << 8) |
           (static_cast<uint32_t>(reinterpret_cast<uint8_t*>(&a)[3]) << 0);
#endif
}

inline uint32_t lfs_tobe32(uint32_t a) {
    return lfs_frombe32(a);
}

// ============================================================================
// CRC-32 (polynomial = 0x04c11db7, small lookup table)
// ============================================================================

/// CRC-32 with 4-bit lookup table (declared here, defined in lfs.cc)
uint32_t lfs_crc(uint32_t crc, const void* buffer, size_t size);

// ============================================================================
// Memory allocation (disabled — no malloc)
// ============================================================================

inline void* lfs_malloc(size_t /*size*/) {
    return nullptr;
}

inline void lfs_free(void* /*p*/) {
}

} // namespace umi::fs::lfs
