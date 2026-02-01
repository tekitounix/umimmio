// SPDX-License-Identifier: MIT
// UMI-OS Storage Service
// Async file system access service running on SystemTask
// See: docs/umios-architecture/19-storage-service.md

#pragma once

#include "block_device.hh"

#include <cstddef>
#include <cstdint>

namespace umi::kernel {

// ============================================================================
// File System Request Types
// ============================================================================

/// FS operation type
enum class FsOp : uint8_t {
    Open,
    Read,
    Write,
    Close,
    Seek,
    Stat,
    DirOpen,
    DirRead,
    DirClose,
};

/// File open flags
enum class FileFlags : uint8_t {
    Read = 0x01,
    Write = 0x02,
    Create = 0x04,
    Append = 0x08,
    Truncate = 0x10,
};

/// Seek whence
enum class SeekWhence : uint8_t {
    Set = 0,
    Cur = 1,
    End = 2,
};

/// File stat info
struct FileStat {
    uint32_t size;
    uint8_t type;  ///< 0 = file, 1 = directory
};

/// Directory entry
struct DirEntry {
    char name[64];
    uint32_t size;
    uint8_t type;
};

// ============================================================================
// FS Request (queued from syscall handler)
// ============================================================================

/// FS request from app (enqueued by syscall handler)
struct FsRequest {
    FsOp op;
    int8_t fd;            ///< File descriptor (-1 for open/stat/dir_open)
    uint8_t flags;        ///< FileFlags for open
    uint8_t whence;       ///< SeekWhence for seek
    const char* path;     ///< Path for open/stat/dir_open (in app memory)
    void* buffer;         ///< Data buffer for read/write/stat/dir_read
    uint32_t size;        ///< Size for read/write
    int32_t offset;       ///< Offset for seek
};

/// FS response (written back after completion)
struct FsResponse {
    int32_t result;       ///< Bytes read/written, fd, or error code
};

// ============================================================================
// StorageService
// ============================================================================

/// Maximum simultaneously open file descriptors per app
inline constexpr size_t MAX_OPEN_FILES = 4;

/// Request queue capacity
inline constexpr size_t FS_REQUEST_QUEUE_SIZE = 4;

/// Storage service running on SystemTask
///
/// Receives FS requests from syscall handler, dispatches to
/// appropriate filesystem (littlefs for /flash/, FATfs for /sd/),
/// and notifies the requesting task on completion.
///
/// Template parameters allow injecting different FS backends.
template <BlockDeviceLike FlashDev, BlockDeviceLike SdDev>
class StorageService {
public:
    StorageService(FlashDev& flash, SdDev& sd) : flash_(flash), sd_(sd) {}

    /// Initialize filesystems
    /// Called during SystemTask init
    int init() noexcept {
        // Mount littlefs on flash device
        // Mount FATfs on SD device
        // Returns 0 on success, negative on error
        return 0;
    }

    /// Enqueue a request (called from syscall handler in SVC context)
    /// @return true if enqueued successfully
    bool enqueue(const FsRequest& req) noexcept {
        if (queue_count_ >= FS_REQUEST_QUEUE_SIZE) {
            return false;
        }
        queue_[(queue_write_) % FS_REQUEST_QUEUE_SIZE] = req;
        queue_write_ = (queue_write_ + 1) % FS_REQUEST_QUEUE_SIZE;
        queue_count_++;
        return true;
    }

    /// Process one pending request (called from SystemTask loop)
    /// @return response, or nullopt if no pending request
    bool process_one(FsResponse& out) noexcept {
        if (queue_count_ == 0) {
            return false;
        }

        const auto& req = queue_[queue_read_];
        queue_read_ = (queue_read_ + 1) % FS_REQUEST_QUEUE_SIZE;
        queue_count_--;

        out = dispatch(req);
        return true;
    }

    /// Check if there are pending requests
    [[nodiscard]] bool has_pending() const noexcept {
        return queue_count_ > 0;
    }

    /// Close all open files (called on app exit/fault)
    void close_all() noexcept {
        for (size_t i = 0; i < MAX_OPEN_FILES; ++i) {
            if (fd_open_[i]) {
                // Close file handle
                fd_open_[i] = false;
            }
        }
    }

private:
    FsResponse dispatch(const FsRequest& req) noexcept {
        switch (req.op) {
            case FsOp::Open: return do_open(req);
            case FsOp::Read: return do_read(req);
            case FsOp::Write: return do_write(req);
            case FsOp::Close: return do_close(req);
            case FsOp::Seek: return do_seek(req);
            case FsOp::Stat: return do_stat(req);
            case FsOp::DirOpen: return do_dir_open(req);
            case FsOp::DirRead: return do_dir_read(req);
            case FsOp::DirClose: return do_dir_close(req);
        }
        return {-1};
    }

    /// Check if path routes to flash (/flash/) or SD (/sd/)
    [[nodiscard]] static bool is_flash_path(const char* path) noexcept {
        return path[0] == '/' && path[1] == 'f';  // /flash/...
    }

    // --- FS operation stubs (to be implemented with littlefs/FATfs) ---

    FsResponse do_open(const FsRequest& /*req*/) noexcept { return {-1}; }
    FsResponse do_read(const FsRequest& /*req*/) noexcept { return {-1}; }
    FsResponse do_write(const FsRequest& /*req*/) noexcept { return {-1}; }
    FsResponse do_close(const FsRequest& /*req*/) noexcept { return {-1}; }
    FsResponse do_seek(const FsRequest& /*req*/) noexcept { return {-1}; }
    FsResponse do_stat(const FsRequest& /*req*/) noexcept { return {-1}; }
    FsResponse do_dir_open(const FsRequest& /*req*/) noexcept { return {-1}; }
    FsResponse do_dir_read(const FsRequest& /*req*/) noexcept { return {-1}; }
    FsResponse do_dir_close(const FsRequest& /*req*/) noexcept { return {-1}; }

    FlashDev& flash_;
    SdDev& sd_;
    FsRequest queue_[FS_REQUEST_QUEUE_SIZE]{};
    size_t queue_read_ = 0;
    size_t queue_write_ = 0;
    size_t queue_count_ = 0;
    bool fd_open_[MAX_OPEN_FILES]{};
};

} // namespace umi::kernel
