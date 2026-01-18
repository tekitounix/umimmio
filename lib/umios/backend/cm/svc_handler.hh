// SPDX-License-Identifier: MIT
// UMI-OS SVC Handler for Cortex-M4
#pragma once

#include <cstddef>
#include <cstdint>
#include "../../kernel/syscall/syscall_numbers.hh"

namespace umi::kernel {

// Forward declaration - kernel provides this
struct KernelState;
extern KernelState* g_kernel;

// ============================================================================
// SVC Handler Implementation
// ============================================================================

/// Exception frame pushed by hardware on exception entry
struct ExceptionFrame {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
};

/// SVC dispatcher - called from assembly handler
/// @param frame  Pointer to exception frame on stack
/// @param svc_num  Syscall number (from r12)
extern "C" void svc_dispatch(ExceptionFrame* frame, uint8_t svc_num);

/// SVC Handler entry point (naked function)
/// Determines stack (MSP/PSP), extracts syscall number from r12,
/// and calls svc_dispatch.
[[gnu::naked]] inline void SVC_Handler() {
    asm volatile(
        // Determine which stack was used (EXC_RETURN bit 2)
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"      // Using MSP
        "mrsne r0, psp\n"      // Using PSP

        // r0 = exception frame pointer
        // r12 contains syscall number (set by caller)
        // Pass r12 as second argument (r1)
        "mov r1, r12\n"

        // Call C++ dispatcher
        "b svc_dispatch\n"
    );
}

// ============================================================================
// Syscall Implementation
// ============================================================================

namespace impl {

// Use max_shared_regions from syscall_numbers.hh
using syscall::max_shared_regions;

// Shared memory region table (registered during kernel init)
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
extern void* shared_regions[max_shared_regions];

// System time counter
extern uint64_t system_time_us;

// Current task ID
extern uint32_t current_task_id;

/// Register a shared region
inline void register_shared(uint8_t region_id, void* ptr) {
    if (region_id < max_shared_regions) {
        shared_regions[region_id] = ptr;
    }
}

/// Handle SYS_GET_SHARED
inline uintptr_t handle_get_shared(uint32_t region_id) {
    if (region_id < max_shared_regions) {
        return reinterpret_cast<uintptr_t>(shared_regions[region_id]);
    }
    return 0;
}

/// Handle SYS_GET_TIME
inline void handle_get_time(ExceptionFrame* frame) {
    frame->r0 = static_cast<uint32_t>(system_time_us);
    frame->r1 = static_cast<uint32_t>(system_time_us >> 32);
}

/// Handle SYS_YIELD
inline void handle_yield() {
    // Trigger PendSV for context switch
    volatile uint32_t& ICSR = *reinterpret_cast<volatile uint32_t*>(0xE000ED04);
    ICSR = (1U << 28);  // Set PENDSVSET
}

// Forward declaration for UART output (defined in uart_driver.hh)
extern "C" void uart_puts(const char*);

/// Handle SYS_DEBUG_PRINT
inline void handle_debug_print(const char* msg) {
    uart_puts(msg);
}

/// Handle SYS_PANIC
[[noreturn]] inline void handle_panic(const char* msg) {
    handle_debug_print("PANIC: ");
    handle_debug_print(msg);
    handle_debug_print("\n");
    while (true) {
        asm volatile("bkpt #0");
    }
}

}  // namespace impl

/// Main syscall dispatcher
extern "C" inline void svc_dispatch(ExceptionFrame* frame, uint8_t svc_num) {
    using namespace syscall;

    switch (svc_num) {
        case SYS_GET_SHARED:
            frame->r0 = impl::handle_get_shared(frame->r0);
            break;

        case SYS_GET_TIME:
            impl::handle_get_time(frame);
            break;

        case SYS_GET_TASK_ID:
            frame->r0 = impl::current_task_id;
            break;

        case SYS_YIELD:
            impl::handle_yield();
            break;

        case SYS_DEBUG_PRINT:
            impl::handle_debug_print(reinterpret_cast<const char*>(frame->r0));
            break;

        case SYS_PANIC:
            impl::handle_panic(reinterpret_cast<const char*>(frame->r0));
            break;

        case SYS_WAIT:
        case SYS_WAIT_TIMEOUT:
        case SYS_NOTIFY:
            // TODO: Implement with task scheduler
            frame->r0 = static_cast<uint32_t>(SyscallError::Ok);
            break;

        default:
            frame->r0 = static_cast<uint32_t>(SyscallError::InvalidSyscall);
            break;
    }
}

}  // namespace umi::kernel
