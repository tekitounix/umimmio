// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022, The littlefs authors.
// Copyright (c) 2017, Arm Limited. All rights reserved.
// C++23 port for UMI framework — configuration structure

#pragma once

#include "lfs_types.hh"
#include <umios/kernel/block_device.hh>

namespace umi::fs::lfs {

/// Configuration provided during initialization of the littlefs.
/// Function pointers are preserved from the original C design.
struct LfsConfig {
    /// Opaque user-provided context passed to block device operations
    void* context = nullptr;

    /// Read a region in a block. Negative error codes are propagated.
    int (*read)(const LfsConfig* c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size) = nullptr;

    /// Program a region in a block. Block must have been erased.
    int (*prog)(const LfsConfig* c, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size) = nullptr;

    /// Erase a block. Block must be erased before being programmed.
    int (*erase)(const LfsConfig* c, lfs_block_t block) = nullptr;

    /// Sync the state of the underlying block device.
    int (*sync)(const LfsConfig* c) = nullptr;

    /// Minimum size of a block read in bytes.
    lfs_size_t read_size = 0;

    /// Minimum size of a block program in bytes.
    lfs_size_t prog_size = 0;

    /// Size of an erasable block in bytes.
    lfs_size_t block_size = 0;

    /// Number of erasable blocks on the device.
    lfs_size_t block_count = 0;

    /// Number of erase cycles before metadata eviction. Set to -1 to disable.
    int32_t block_cycles = -1;

    /// Size of block caches in bytes.
    lfs_size_t cache_size = 0;

    /// Size of the lookahead buffer in bytes.
    lfs_size_t lookahead_size = 0;

    /// Threshold for metadata compaction during lfs_fs_gc. 0 = default (~88% block_size).
    lfs_size_t compact_thresh = 0;

    /// Optional statically allocated read buffer. Must be cache_size.
    void* read_buffer = nullptr;

    /// Optional statically allocated program buffer. Must be cache_size.
    void* prog_buffer = nullptr;

    /// Optional statically allocated lookahead buffer. Must be lookahead_size.
    void* lookahead_buffer = nullptr;

    /// Optional upper limit on file name length.
    lfs_size_t name_max = 0;

    /// Optional upper limit on file size.
    lfs_size_t file_max = 0;

    /// Optional upper limit on custom attributes.
    lfs_size_t attr_max = 0;

    /// Optional upper limit on metadata pair size.
    lfs_size_t metadata_max = 0;

    /// Optional upper limit on inlined files. Set to -1 to disable.
    lfs_size_t inline_max = 0;
};

/// Helper to create LfsConfig from a BlockDeviceLike device.
/// The device pointer is stored in context and callbacks delegate to it.
/// Device must outlive the returned config.
template <umi::kernel::BlockDeviceLike Dev>
LfsConfig make_lfs_config(Dev& dev, lfs_size_t cache_size, lfs_size_t lookahead_size,
                          void* read_buf = nullptr, void* prog_buf = nullptr,
                          void* lookahead_buf = nullptr) {
    LfsConfig cfg{};
    cfg.context = &dev;

    cfg.read = [](const LfsConfig* c, lfs_block_t block, lfs_off_t off, void* buffer,
                  lfs_size_t size) -> int {
        return static_cast<Dev*>(c->context)->read(block, off, buffer, size);
    };

    cfg.prog = [](const LfsConfig* c, lfs_block_t block, lfs_off_t off, const void* buffer,
                  lfs_size_t size) -> int {
        return static_cast<Dev*>(c->context)->write(block, off, buffer, size);
    };

    cfg.erase = [](const LfsConfig* c, lfs_block_t block) -> int {
        return static_cast<Dev*>(c->context)->erase(block);
    };

    cfg.sync = [](const LfsConfig* /*c*/) -> int { return 0; };

    cfg.read_size = dev.block_size();   // Can be overridden by caller
    cfg.prog_size = dev.block_size();   // Can be overridden by caller
    cfg.block_size = dev.block_size();
    cfg.block_count = dev.block_count();
    cfg.cache_size = cache_size;
    cfg.lookahead_size = lookahead_size;
    cfg.read_buffer = read_buf;
    cfg.prog_buffer = prog_buf;
    cfg.lookahead_buffer = lookahead_buf;

    return cfg;
}

} // namespace umi::fs::lfs
