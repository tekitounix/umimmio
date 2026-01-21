// SPDX-License-Identifier: MIT
// UMI-OS Syscall Handler
// Handles SVC exceptions and dispatches syscalls to kernel services

#pragma once

#include "syscall/syscall_numbers.hh"
#include "loader.hh"
#include "../core/event.hh"
#include <cstdint>

namespace umi::kernel {

// ============================================================================
// Syscall Numbers (Application ABI)
// ============================================================================
// These are the syscall numbers used by applications.
// They differ from internal kernel syscall numbers.

namespace app_syscall {
    // Process control
    inline constexpr uint32_t Exit          = 0;   ///< Terminate application
    inline constexpr uint32_t RegisterProc  = 1;   ///< Register processor
    
    // Event handling
    inline constexpr uint32_t WaitEvent     = 2;   ///< Wait for event (blocking)
    inline constexpr uint32_t SendEvent     = 3;   ///< Send event to kernel
    inline constexpr uint32_t PeekEvent     = 4;   ///< Check for event (non-blocking)
    
    // Time
    inline constexpr uint32_t GetTime       = 10;  ///< Get time in microseconds
    inline constexpr uint32_t Sleep         = 11;  ///< Sleep for duration
    
    // Debug/Log
    inline constexpr uint32_t Log           = 20;  ///< Debug log output
    inline constexpr uint32_t Panic         = 21;  ///< Application panic
    
    // Parameters
    inline constexpr uint32_t GetParam      = 30;  ///< Get parameter value
    inline constexpr uint32_t SetParam      = 31;  ///< Set parameter value
    
    // Shared memory
    inline constexpr uint32_t GetShared     = 40;  ///< Get shared memory pointer
}

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

/// Forward declaration of kernel type
template <size_t MaxTasks, size_t MaxTimers, class HW>
class Kernel;

/// Syscall handler - processes syscalls from applications
/// 
/// Template parameters allow access to the kernel instance
template <class KernelType>
class SyscallHandler {
public:
    /// Constructor
    /// @param kernel Reference to kernel instance
    /// @param loader Reference to application loader
    /// @param shared Reference to shared memory
    SyscallHandler(KernelType& kernel, AppLoader& loader, SharedMemory& shared) noexcept
        : kernel_(kernel), loader_(loader), shared_(shared) {}
    
    /// Handle a syscall
    /// @param ctx Syscall context (syscall number and arguments)
    /// @return Result value (written to ctx.result)
    int32_t handle(SyscallContext& ctx) noexcept {
        switch (ctx.syscall_nr) {
        // --- Process Control ---
        case app_syscall::Exit:
            return handle_exit(ctx.arg0);
            
        case app_syscall::RegisterProc:
            return handle_register_proc(
                reinterpret_cast<void*>(ctx.arg0),
                reinterpret_cast<ProcessFn>(ctx.arg1)
            );
        
        // --- Events ---
        case app_syscall::WaitEvent:
            return handle_wait_event(reinterpret_cast<Event*>(ctx.arg0));
            
        case app_syscall::SendEvent:
            return handle_send_event(reinterpret_cast<const Event*>(ctx.arg0));
            
        case app_syscall::PeekEvent:
            return handle_peek_event(reinterpret_cast<Event*>(ctx.arg0));
        
        // --- Time ---
        case app_syscall::GetTime:
            return handle_get_time();
            
        case app_syscall::Sleep:
            return handle_sleep(ctx.arg0);
        
        // --- Debug ---
        case app_syscall::Log:
            return handle_log(
                reinterpret_cast<const char*>(ctx.arg0),
                ctx.arg1
            );
            
        case app_syscall::Panic:
            return handle_panic(reinterpret_cast<const char*>(ctx.arg0));
        
        // --- Parameters ---
        case app_syscall::GetParam:
            return handle_get_param(ctx.arg0, reinterpret_cast<float*>(ctx.arg1));
            
        case app_syscall::SetParam:
            return handle_set_param(ctx.arg0, *reinterpret_cast<float*>(&ctx.arg1));
        
        // --- Shared Memory ---
        case app_syscall::GetShared:
            return handle_get_shared(reinterpret_cast<void**>(ctx.arg0));
        
        default:
            // Unknown syscall
            return -1;
        }
    }

private:
    // --- Handler Implementations ---
    
    int32_t handle_exit(int exit_code) noexcept {
        loader_.terminate(exit_code);
        // Request scheduler to switch to another task
        // This syscall does not return
        kernel_.yield();
        return 0;  // Never reached
    }
    
    int32_t handle_register_proc(void* processor, ProcessFn fn) noexcept {
        return loader_.register_processor(processor, fn);
    }
    
    int32_t handle_wait_event(Event* out_event) noexcept {
        if (out_event == nullptr) {
            return -1;
        }
        
        // Block until event is available
        while (true) {
            if (shared_.pop_event(*out_event)) {
                return 0;  // Got event
            }
            
            // No event, yield and wait
            kernel_.wait(KernelEvent::MidiReady | KernelEvent::VSync);
        }
    }
    
    int32_t handle_send_event(const Event* event) noexcept {
        if (event == nullptr) {
            return -1;
        }
        
        // TODO: Route event to appropriate handler
        // For now, just accept it
        return 0;
    }
    
    int32_t handle_peek_event(Event* out_event) noexcept {
        if (out_event == nullptr) {
            return -1;
        }
        
        if (shared_.pop_event(*out_event)) {
            return 1;  // Event available
        }
        return 0;  // No event
    }
    
    int32_t handle_get_time() noexcept {
        return static_cast<int32_t>(kernel_.time_usec() & 0x7FFFFFFF);
    }
    
    int32_t handle_sleep(uint32_t usec) noexcept {
        kernel_.sleep_usec(usec);
        return 0;
    }
    
    int32_t handle_log(const char* msg, uint32_t len) noexcept {
        if (msg == nullptr) {
            return -1;
        }
        
        // TODO: Validate that msg is within app's memory region
        // TODO: Output to debug console
        (void)len;
        return 0;
    }
    
    int32_t handle_panic(const char* msg) noexcept {
        // TODO: Log panic message and halt
        (void)msg;
        loader_.terminate(-1);
        kernel_.yield();
        return 0;  // Never reached
    }
    
    int32_t handle_get_param(uint32_t index, float* out_value) noexcept {
        if (index >= SharedMemory::MAX_PARAMS || out_value == nullptr) {
            return -1;
        }
        
        *out_value = shared_.params[index].load(std::memory_order_relaxed);
        return 0;
    }
    
    int32_t handle_set_param(uint32_t index, float value) noexcept {
        if (index >= SharedMemory::MAX_PARAMS) {
            return -1;
        }
        
        shared_.params[index].store(value, std::memory_order_relaxed);
        return 0;
    }
    
    int32_t handle_get_shared(void** out_ptr) noexcept {
        if (out_ptr == nullptr) {
            return -1;
        }
        
        *out_ptr = &shared_;
        return 0;
    }
    
    // --- Members ---
    
    KernelType& kernel_;
    AppLoader& loader_;
    SharedMemory& shared_;
};

// ============================================================================
// ARM Cortex-M SVC Handler
// ============================================================================

#if defined(__ARM_ARCH)

/// Global syscall handler instance (set by kernel during init)
/// This is accessed from the SVC_Handler assembly
extern void* g_syscall_handler;

/// SVC Handler entry point (called from assembly)
/// @param svc_args Pointer to stacked registers [r0, r1, r2, r3, r12, lr, pc, xpsr]
/// @return Result value to be placed in r0
extern "C" inline int32_t svc_handler_main(uint32_t* svc_args) {
    // Extract syscall context from stacked registers
    SyscallContext ctx;
    ctx.syscall_nr = svc_args[0];  // r0
    ctx.arg0 = svc_args[1];        // r1
    ctx.arg1 = svc_args[2];        // r2
    ctx.arg2 = svc_args[3];        // r3
    // Note: r4 would need special handling if needed
    
    // Dispatch to handler
    // The actual handler type depends on kernel configuration
    // For now, return the syscall number as a placeholder
    // Real implementation would call: handler->handle(ctx);
    
    return ctx.result;
}

/// ARM Cortex-M SVC Handler (naked function)
/// Determines which stack was used and extracts arguments
extern "C" __attribute__((naked)) void SVC_Handler() {
    __asm__ volatile(
        // Check which stack pointer was used (MSP or PSP)
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"     // Using MSP (should not happen from app)
        "mrsne r0, psp\n"     // Using PSP (app uses this)
        
        // Call C handler with stack pointer as argument
        "bl svc_handler_main\n"
        
        // Return value is in r0, it will be restored from stack
        // Store result back to stacked r0
        "str r0, [sp]\n"      // This may need adjustment based on stack frame
        
        // Return from exception
        "bx lr\n"
    );
}

#endif // __ARM_ARCH

} // namespace umi::kernel
