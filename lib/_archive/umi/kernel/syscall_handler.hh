// SPDX-License-Identifier: MIT
// UMI-OS Syscall Handler
// Handles SVC exceptions and dispatches syscalls to kernel services

#pragma once

#include "loader.hh"
#include "storage_service.hh"
#include <cstdint>

namespace umi::kernel {

// Syscall numbers: use umi::syscall::nr::* from core/syscall_nr.hh
// (included transitively via storage_service.hh → core/syscall_nr.hh)

// ============================================================================
// Syscall Context
// ============================================================================

/// Context passed to syscall handler
struct SyscallContext {
    uint32_t syscall_nr;    ///< Syscall number (r0)
    uint32_t arg0;          ///< First argument (r1)
    uint32_t arg1;          ///< Second argument (r2)
    uint32_t arg2;          ///< Third argument (r3)
    uint32_t arg3;          ///< Fourth argument (r4, if needed)

    /// Set return value (written back to r0)
    int32_t result = 0;
};

// ============================================================================
// Syscall Handler Interface
// ============================================================================

/// Syscall handler - processes syscalls from applications
///
/// This is the generic/reference implementation.
/// Platform-specific kernels (e.g. stm32f4_kernel) may implement their
/// own handler directly in svc_handler_impl() for efficiency.
///
/// @tparam KernelType  Kernel with yield(), notify_event() methods
/// @tparam StorageType StorageService<FlashDev, SdDev> instance
template <class KernelType, class StorageType>
class SyscallHandler {
public:
    SyscallHandler(KernelType& kernel, AppLoader& loader, SharedMemory& shared,
                   StorageType& storage) noexcept
        : kernel_(kernel), loader_(loader), shared_(shared), storage_(storage) {}

    int32_t handle(SyscallContext& ctx) noexcept {
        // FS request (60–83): enqueue to StorageService
        if (syscall::is_fs_request(ctx.syscall_nr)) {
            FsRequest req{
                static_cast<uint8_t>(ctx.syscall_nr),
                ctx.arg0, ctx.arg1, ctx.arg2, ctx.arg3
            };
            return storage_.enqueue(req) ? 0 : fs_errno::EBUSY;
        }

        // FS result (84): consume result slot
        if (ctx.syscall_nr == syscall::nr::fs_result) {
            return storage_.consume_result();
        }

        switch (ctx.syscall_nr) {
        case syscall::nr::exit:
            storage_.close_all();
            loader_.terminate(static_cast<int>(ctx.arg0));
            kernel_.yield();
            return 0;

        case syscall::nr::yield:
            kernel_.yield();
            return 0;

        case syscall::nr::register_proc:
            if (ctx.arg1 != 0) {
                return loader_.register_processor(
                    reinterpret_cast<void*>(ctx.arg0),
                    reinterpret_cast<ProcessFn>(ctx.arg1));
            }
            return loader_.register_processor(reinterpret_cast<void*>(ctx.arg0));

        default:
            return -1;
        }
    }

private:
    KernelType& kernel_;
    AppLoader& loader_;
    SharedMemory& shared_;
    StorageType& storage_;
};

} // namespace umi::kernel
