// SPDX-License-Identifier: MIT
// Copyright (c) 2025, tekitounix

#pragma once

#include <cstdint>
#include <type_traits>

namespace umi::fs {

/// Version info
constexpr uint32_t VERSION = 0x0002000b;
constexpr uint16_t VERSION_MAJOR = 0xffff & (VERSION >> 16);
constexpr uint16_t VERSION_MINOR = 0xffff & (VERSION >> 0);

constexpr uint32_t DISK_VERSION = 0x00020001;
constexpr uint16_t DISK_VERSION_MAJOR = 0xffff & (DISK_VERSION >> 16);
constexpr uint16_t DISK_VERSION_MINOR = 0xffff & (DISK_VERSION >> 0);

/// Type aliases matching original littlefs types
using lfs_size_t = uint32_t;
using lfs_off_t = uint32_t;
using lfs_ssize_t = int32_t;
using lfs_soff_t = int32_t;
using lfs_block_t = uint32_t;

/// Limits
constexpr lfs_size_t NAME_MAX = 255;
constexpr lfs_ssize_t FILE_MAX = 2147483647;
constexpr lfs_size_t ATTR_MAX = 1022;

/// Error codes (negative values, matching POSIX errno)
enum class LfsError : int {
    OK = 0,
    IO = -5,
    CORRUPT = -84,
    NOENT = -2,
    EXIST = -17,
    NOTDIR = -20,
    ISDIR = -21,
    NOTEMPTY = -39,
    BADF = -9,
    FBIG = -27,
    INVAL = -22,
    NOSPC = -28,
    NOMEM = -12,
    NOATTR = -61,
    NAMETOOLONG = -36,
};

/// File types and internal type tags
enum class LfsType : uint16_t {
    // File types
    REG = 0x001,
    DIR = 0x002,

    // Internally used types
    SPLICE = 0x400,
    NAME = 0x000,
    STRUCT = 0x200,
    USERATTR = 0x300,
    FROM = 0x100,
    TAIL = 0x600,
    GLOBALS = 0x700,
    CRC = 0x500,

    // Internally used type specializations
    CREATE = 0x401,
    DELETE = 0x4ff,
    SUPERBLOCK = 0x0ff,
    DIRSTRUCT = 0x200,
    CTZSTRUCT = 0x202,
    INLINESTRUCT = 0x201,
    SOFTTAIL = 0x600,
    HARDTAIL = 0x601,
    MOVESTATE = 0x7ff,
    CCRC = 0x500,
    FCRC = 0x5ff,

    // Internal chip sources
    FROM_NOOP = 0x000,
    FROM_MOVE = 0x101,
    FROM_USERATTRS = 0x102,
};

/// File open flags
enum class LfsOpenFlags : uint32_t {
    RDONLY = 1,
    WRONLY = 2,
    RDWR = 3,
    CREAT = 0x0100,
    EXCL = 0x0200,
    TRUNC = 0x0400,
    APPEND = 0x0800,

    // Internally used flags
    F_DIRTY = 0x010000,
    F_WRITING = 0x020000,
    F_READING = 0x040000,
    F_ERRED = 0x080000,
    F_INLINE = 0x100000,
};

/// Bitwise operators for enum class flags
template <typename E>
    requires std::is_enum_v<E>
constexpr E operator|(E a, E b) {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}
template <typename E>
    requires std::is_enum_v<E>
constexpr E operator&(E a, E b) {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
}
template <typename E>
    requires std::is_enum_v<E>
constexpr E operator~(E a) {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(~static_cast<U>(a));
}
template <typename E>
    requires std::is_enum_v<E>
constexpr E& operator|=(E& a, E b) {
    a = a | b;
    return a;
}
template <typename E>
    requires std::is_enum_v<E>
constexpr E& operator&=(E& a, E b) {
    a = a & b;
    return a;
}

/// File seek flags
enum class LfsWhence : int {
    SET = 0,
    CUR = 1,
    END = 2,
};

/// File info structure
struct LfsInfo {
    uint8_t type;
    lfs_size_t size;
    char name[NAME_MAX + 1];
};

/// Filesystem info structure
struct LfsFsInfo {
    uint32_t disk_version;
    lfs_size_t block_size;
    lfs_size_t block_count;
    lfs_size_t name_max;
    lfs_size_t file_max;
    lfs_size_t attr_max;
};

/// Custom attribute structure
struct LfsAttr {
    uint8_t type;
    void* buffer;
    lfs_size_t size;
};

/// Optional configuration for file open
struct LfsFileConfig {
    void* buffer;
    LfsAttr* attrs;
    lfs_size_t attr_count;
};

/// Internal: block cache
struct LfsCache {
    lfs_block_t block;
    lfs_off_t off;
    lfs_size_t size;
    uint8_t* buffer;
};

/// Internal: metadata directory
struct LfsMdir {
    lfs_block_t pair[2];
    uint32_t rev;
    lfs_off_t off;
    uint32_t etag;
    uint16_t count;
    bool erased;
    bool split;
    lfs_block_t tail[2];
};

/// Internal: metadata list entry
struct LfsMlist {
    LfsMlist* next;
    uint16_t id;
    uint8_t type;
    LfsMdir m;
};

/// Directory type
struct LfsDir {
    LfsDir* next;
    uint16_t id;
    uint8_t type;
    LfsMdir m;

    lfs_off_t pos;
    lfs_block_t head[2];
};

/// CTZ skip list structure
struct LfsCtz {
    lfs_block_t head;
    lfs_size_t size;
};

/// File type
struct LfsFile {
    LfsFile* next;
    uint16_t id;
    uint8_t type;
    LfsMdir m;

    LfsCtz ctz;

    uint32_t flags;
    lfs_off_t pos;
    lfs_block_t block;
    lfs_off_t off;
    LfsCache cache;

    const LfsFileConfig* cfg;
};

/// Internal: superblock
struct LfsSuperblock {
    uint32_t version;
    lfs_size_t block_size;
    lfs_size_t block_count;
    lfs_size_t name_max;
    lfs_size_t file_max;
    lfs_size_t attr_max;
};

/// Internal: global state
struct LfsGstate {
    uint32_t tag;
    lfs_block_t pair[2];
};

/// Internal: lookahead allocator state
struct LfsLookahead {
    lfs_block_t start;
    lfs_block_t size;
    lfs_block_t next;
    lfs_block_t ckpoint;
    uint8_t* buffer;
};

} // namespace umi::fs
