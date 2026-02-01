#!/bin/bash
# Transform lfs.c to lfs.cc mechanically
# Usage: bash transform_lfs.sh > lfs.cc

SRC="/Users/tekitou/work/umi/.refs/littlefs/lfs.c"

cat << 'HEADER'
// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022, The littlefs authors.
// Copyright (c) 2017, Arm Limited. All rights reserved.
//
// C++23 port of lfs.c for the UMI framework.
// This is a FAITHFUL port — same algorithms, data structures, and logic.
// The original static functions are preserved as-is (not class methods),
// operating on an lfs_t struct that is aliased from the Lfs class.
// Public API methods on Lfs delegate to these static functions.

#include "lfs.hh"

#include <cinttypes>
#include <cstring>

namespace umi::fs::lfs {

// ============================================================================
// CRC implementation (from lfs_util.c)
// ============================================================================

uint32_t lfs_crc(uint32_t crc, const void* buffer, size_t size) {
    static const uint32_t rtable[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };
    const auto* data = static_cast<const uint8_t*>(buffer);
    for (size_t i = 0; i < size; i++) {
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 0)) & 0xf];
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 4)) & 0xf];
    }
    return crc;
}

// ============================================================================
// Type bridge: type aliases so the original C-style static functions compile
// unchanged, operating on Lfs (which has public members matching lfs_t).
// ============================================================================

using lfs_t = Lfs;
using lfs_cache_t = LfsCache;
using lfs_mdir_t = LfsMdir;
using lfs_dir_t = LfsDir;
using lfs_file_t = LfsFile;
using lfs_superblock_t = LfsSuperblock;
using lfs_gstate_t = LfsGstate;
// Note: struct lfs_config, struct lfs_info etc are handled by sed below

HEADER

# Extract lines from after the opening comment+includes to before the public
# API wrappers section. The original lfs.c structure:
#   Lines 1-6: copyright comment + includes
#   Lines 7+: the comment block end " */" then #include lines
#   The actual code starts around line 19 (after #include "lfs_util.h")
#   Line ~5305: #ifdef LFS_MIGRATE
#   Line ~5953: #endif for LFS_MIGRATE
#   Line ~5955: /// Public API wrappers ///
#   Line ~6549: end

# We want: lines 19 to 5954 (everything including LFS_MIGRATE but not public wrappers)
# Actually, let's skip the original includes and defines that we already handle,
# and start from the first real code.

# Find the line number of "/// Caching block device operations ///"
START=$(grep -n '/// Caching block device operations ///' "$SRC" | head -1 | cut -d: -f1)
# Find the line of "/// Public API wrappers ///"
END=$(grep -n '/// Public API wrappers ///' "$SRC" | head -1 | cut -d: -f1)
# Go one line before the public API comment
END=$((END - 3))

# Also we need the constants/enums that come before the caching section
# Find "// some constants used throughout the code"
CONST_START=$(grep -n 'some constants used throughout the code' "$SRC" | head -1 | cut -d: -f1)

# Emit constant definitions (lines CONST_START to START-1)
sed -n "${CONST_START},$(($START - 1))p" "$SRC" | \
    sed \
        -e '/^#define LFS_BLOCK_NULL/d' \
        -e '/^#define LFS_BLOCK_INLINE/d' \
        -e 's/NULL\b/nullptr/g' \
        -e 's/\bstruct lfs_config\b/LfsConfig/g' \
        -e 's/\bstruct lfs_info\b/LfsInfo/g' \
        -e 's/\bstruct lfs_fsinfo\b/LfsFsInfo/g' \
        -e 's/\bstruct lfs_file_config\b/LfsFileConfig/g' \
        -e 's/\bstruct lfs_mlist\b/LfsMlist/g' \
        -e 's/\bstruct lfs_ctz\b/LfsCtz/g'

# Emit the core implementation
sed -n "${START},${END}p" "$SRC" | \
    sed \
        -e 's/NULL\b/nullptr/g' \
        -e 's/\bstruct lfs_config\b/LfsConfig/g' \
        -e 's/\bstruct lfs_info\b/LfsInfo/g' \
        -e 's/\bstruct lfs_fsinfo\b/LfsFsInfo/g' \
        -e 's/\bstruct lfs_file_config\b/LfsFileConfig/g' \
        -e 's/\bstruct lfs_mlist\b/LfsMlist/g' \
        -e 's/\bstruct lfs_ctz\b/LfsCtz/g'

# Now emit the public API methods
cat << 'PUBLIC_API'


// ============================================================================
// Public API: Lfs class methods delegating to internal static functions
// ============================================================================

#ifndef LFS_READONLY
int Lfs::format(const LfsConfig* config) noexcept {
    return lfs_format_(this, config);
}
#endif

int Lfs::mount(const LfsConfig* config) noexcept {
    return lfs_mount_(this, config);
}

int Lfs::unmount() noexcept {
    return lfs_unmount_(this);
}

#ifndef LFS_READONLY
int Lfs::remove(const char* path) noexcept {
    return lfs_remove_(this, path);
}
#endif

#ifndef LFS_READONLY
int Lfs::rename(const char* oldpath, const char* newpath) noexcept {
    return lfs_rename_(this, oldpath, newpath);
}
#endif

int Lfs::stat(const char* path, LfsInfo* info) noexcept {
    return lfs_stat_(this, path, info);
}

lfs_ssize_t Lfs::getattr(const char* path, uint8_t type, void* buffer, lfs_size_t size) noexcept {
    return lfs_getattr_(this, path, type, buffer, size);
}

#ifndef LFS_READONLY
int Lfs::setattr(const char* path, uint8_t type, const void* buffer, lfs_size_t size) noexcept {
    return lfs_setattr_(this, path, type, buffer, size);
}
#endif

#ifndef LFS_READONLY
int Lfs::removeattr(const char* path, uint8_t type) noexcept {
    return lfs_removeattr_(this, path, type);
}
#endif

#ifndef LFS_NO_MALLOC
int Lfs::file_open(LfsFile* file, const char* path, int flags) noexcept {
    return lfs_file_open_(this, file, path, flags);
}
#endif

int Lfs::file_opencfg(LfsFile* file, const char* path, int flags,
                      const LfsFileConfig* config) noexcept {
    return lfs_file_opencfg_(this, file, path, flags, config);
}

int Lfs::file_close(LfsFile* file) noexcept {
    return lfs_file_close_(this, file);
}

#ifndef LFS_READONLY
int Lfs::file_sync(LfsFile* file) noexcept {
    return lfs_file_sync_(this, file);
}
#endif

lfs_ssize_t Lfs::file_read(LfsFile* file, void* buffer, lfs_size_t size) noexcept {
    return lfs_file_read_(this, file, buffer, size);
}

#ifndef LFS_READONLY
lfs_ssize_t Lfs::file_write(LfsFile* file, const void* buffer, lfs_size_t size) noexcept {
    return lfs_file_write_(this, file, buffer, size);
}
#endif

lfs_soff_t Lfs::file_seek(LfsFile* file, lfs_soff_t off, int whence) noexcept {
    return lfs_file_seek_(this, file, off, whence);
}

#ifndef LFS_READONLY
int Lfs::file_truncate(LfsFile* file, lfs_off_t size) noexcept {
    return lfs_file_truncate_(this, file, size);
}
#endif

lfs_soff_t Lfs::file_tell(LfsFile* file) noexcept {
    return lfs_file_tell_(this, file);
}

int Lfs::file_rewind(LfsFile* file) noexcept {
    return lfs_file_rewind_(this, file);
}

lfs_soff_t Lfs::file_size(LfsFile* file) noexcept {
    return lfs_file_size_(this, file);
}

#ifndef LFS_READONLY
int Lfs::mkdir(const char* path) noexcept {
    return lfs_mkdir_(this, path);
}
#endif

int Lfs::dir_open(LfsDir* dir, const char* path) noexcept {
    return lfs_dir_open_(this, dir, path);
}

int Lfs::dir_close(LfsDir* dir) noexcept {
    return lfs_dir_close_(this, dir);
}

int Lfs::dir_read(LfsDir* dir, LfsInfo* info) noexcept {
    return lfs_dir_read_(this, dir, info);
}

int Lfs::dir_seek(LfsDir* dir, lfs_off_t off) noexcept {
    return lfs_dir_seek_(this, dir, off);
}

lfs_soff_t Lfs::dir_tell(LfsDir* dir) noexcept {
    return lfs_dir_tell_(this, dir);
}

int Lfs::dir_rewind(LfsDir* dir) noexcept {
    return lfs_dir_rewind_(this, dir);
}

int Lfs::fs_stat(LfsFsInfo* fsinfo) noexcept {
    return lfs_fs_stat_(this, fsinfo);
}

lfs_ssize_t Lfs::fs_size() noexcept {
    return lfs_fs_size_(this);
}

int Lfs::fs_traverse(int (*cb)(void*, lfs_block_t), void* data) noexcept {
    return lfs_fs_traverse_(this, cb, data, true);
}

#ifndef LFS_READONLY
int Lfs::fs_mkconsistent() noexcept {
    return lfs_fs_mkconsistent_(this);
}
#endif

#ifndef LFS_READONLY
int Lfs::fs_gc() noexcept {
    return lfs_fs_gc_(this);
}
#endif

#ifndef LFS_READONLY
int Lfs::fs_grow(lfs_size_t block_count) noexcept {
    return lfs_fs_grow_(this, block_count);
}
#endif

} // namespace umi::fs::lfs
PUBLIC_API
