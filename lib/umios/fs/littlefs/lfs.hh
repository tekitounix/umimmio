// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022, The littlefs authors.
// Copyright (c) 2017, Arm Limited. All rights reserved.
// C++23 port for UMI framework — Lfs class declaration

#pragma once

#include "lfs_config.hh"
#include "lfs_types.hh"
#include "lfs_util.hh"

namespace umi::fs::lfs {

/// The littlefs filesystem (replaces lfs_t).
///
/// Internal state is intentionally public to allow the ported C-style static
/// functions in lfs.cc to access members directly (matching original lfs_t
/// which was a plain C struct). Only the public API methods below constitute
/// the user-facing interface.
class Lfs {
public:
    // =================================================================
    // Filesystem operations
    // =================================================================

    /// Format a block device with littlefs
    int format(const LfsConfig* config) noexcept;

    /// Mount a littlefs filesystem
    int mount(const LfsConfig* config) noexcept;

    /// Unmount the filesystem
    int unmount() noexcept;

    // =================================================================
    // General operations
    // =================================================================

    /// Remove a file or directory
    int remove(const char* path) noexcept;

    /// Rename or move a file or directory
    int rename(const char* oldpath, const char* newpath) noexcept;

    /// Get info about a file or directory
    int stat(const char* path, LfsInfo* info) noexcept;

    /// Get a custom attribute
    lfs_ssize_t getattr(const char* path, uint8_t type, void* buffer, lfs_size_t size) noexcept;

    /// Set a custom attribute
    int setattr(const char* path, uint8_t type, const void* buffer, lfs_size_t size) noexcept;

    /// Remove a custom attribute
    int removeattr(const char* path, uint8_t type) noexcept;

    // =================================================================
    // File operations
    // =================================================================

    /// Open a file (requires malloc or use opencfg with buffer)
    int file_open(LfsFile* file, const char* path, int flags) noexcept;

    /// Open a file with extra configuration
    int file_opencfg(LfsFile* file, const char* path, int flags, const LfsFileConfig* config) noexcept;

    /// Close a file
    int file_close(LfsFile* file) noexcept;

    /// Synchronize a file on storage
    int file_sync(LfsFile* file) noexcept;

    /// Read data from file
    lfs_ssize_t file_read(LfsFile* file, void* buffer, lfs_size_t size) noexcept;

    /// Write data to file
    lfs_ssize_t file_write(LfsFile* file, const void* buffer, lfs_size_t size) noexcept;

    /// Seek within a file
    lfs_soff_t file_seek(LfsFile* file, lfs_soff_t off, int whence) noexcept;

    /// Truncate a file
    int file_truncate(LfsFile* file, lfs_off_t size) noexcept;

    /// Return current file position
    lfs_soff_t file_tell(LfsFile* file) noexcept;

    /// Rewind file to beginning
    int file_rewind(LfsFile* file) noexcept;

    /// Return file size
    lfs_soff_t file_size(LfsFile* file) noexcept;

    // =================================================================
    // Directory operations
    // =================================================================

    /// Create a directory
    int mkdir(const char* path) noexcept;

    /// Open a directory for reading
    int dir_open(LfsDir* dir, const char* path) noexcept;

    /// Close a directory
    int dir_close(LfsDir* dir) noexcept;

    /// Read next directory entry
    int dir_read(LfsDir* dir, LfsInfo* info) noexcept;

    /// Seek to a directory position
    int dir_seek(LfsDir* dir, lfs_off_t off) noexcept;

    /// Return current directory position
    lfs_soff_t dir_tell(LfsDir* dir) noexcept;

    /// Rewind directory to beginning
    int dir_rewind(LfsDir* dir) noexcept;

    // =================================================================
    // Filesystem-level operations
    // =================================================================

    /// Get on-disk filesystem info
    int fs_stat(LfsFsInfo* fsinfo) noexcept;

    /// Get filesystem size in allocated blocks
    lfs_ssize_t fs_size() noexcept;

    /// Traverse all blocks in use
    int fs_traverse(int (*cb)(void*, lfs_block_t), void* data) noexcept;

    /// Make filesystem consistent and ready for writing
    int fs_mkconsistent() noexcept;

    /// Perform janitorial work (compaction, GC)
    int fs_gc() noexcept;

    /// Grow filesystem to new size
    int fs_grow(lfs_size_t block_count) noexcept;

    // =================================================================
    // Internal state — public for C-style static function access in lfs.cc
    // Users should not access these directly.
    // =================================================================

    LfsCache rcache{};
    LfsCache pcache{};

    lfs_block_t root[2]{};
    LfsMlist* mlist = nullptr;
    uint32_t seed = 0;

    LfsGstate gstate{};
    LfsGstate gdisk{};
    LfsGstate gdelta{};

    LfsLookahead lookahead{};

    const LfsConfig* cfg = nullptr;
    lfs_size_t block_count = 0;
    lfs_size_t name_max = 0;
    lfs_size_t file_max = 0;
    lfs_size_t attr_max = 0;
    lfs_size_t inline_max = 0;
};

} // namespace umi::fs::lfs
