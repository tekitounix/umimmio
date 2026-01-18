// SPDX-License-Identifier: MIT
// UMI-OS Syscall Implementation for Cortex-M (SVC-based)
#pragma once

#include <cstdint>
#include "../../../kernel/syscall/syscall_numbers.hh"

namespace umi::syscall {

// ============================================================================
// Low-level SVC wrappers (inline assembly)
// ============================================================================
// These functions issue SVC instructions with the syscall number encoded
// in the immediate field. Arguments are passed via r0-r3 per AAPCS.

namespace detail {

[[gnu::always_inline]] inline uint32_t svc0(uint8_t num) {
    register uint32_t r0 asm("r0");
    asm volatile("svc %[num]" : "=r"(r0) : [num] "I"(0) : "memory");
    // Note: immediate must be compile-time constant, so we use a dispatcher
    (void)num;
    return r0;
}

[[gnu::always_inline]] inline uint32_t svc1(uint8_t num, uint32_t arg0) {
    register uint32_t r0 asm("r0") = arg0;
    asm volatile("svc %[num]" : "+r"(r0) : [num] "I"(0) : "memory");
    (void)num;
    return r0;
}

[[gnu::always_inline]] inline uint32_t svc2(uint8_t num, uint32_t arg0, uint32_t arg1) {
    register uint32_t r0 asm("r0") = arg0;
    register uint32_t r1 asm("r1") = arg1;
    asm volatile("svc %[num]" : "+r"(r0) : [num] "I"(0), "r"(r1) : "memory");
    (void)num;
    return r0;
}

}  // namespace detail

// ============================================================================
// Generic SVC caller with runtime syscall number
// ============================================================================
// Since SVC immediate must be compile-time, we use a fixed SVC #0 and
// pass the actual syscall number in r12. The SVC handler extracts it.

[[gnu::always_inline]] inline uint32_t syscall0(uint8_t num) {
    register uint32_t r0 asm("r0");
    register uint32_t r12 asm("r12") = num;
    asm volatile("svc #0" : "=r"(r0) : "r"(r12) : "memory");
    return r0;
}

[[gnu::always_inline]] inline uint32_t syscall1(uint8_t num, uint32_t arg0) {
    register uint32_t r0 asm("r0") = arg0;
    register uint32_t r12 asm("r12") = num;
    asm volatile("svc #0" : "+r"(r0) : "r"(r12) : "memory");
    return r0;
}

[[gnu::always_inline]] inline uint32_t syscall2(uint8_t num, uint32_t arg0, uint32_t arg1) {
    register uint32_t r0 asm("r0") = arg0;
    register uint32_t r1 asm("r1") = arg1;
    register uint32_t r12 asm("r12") = num;
    asm volatile("svc #0" : "+r"(r0) : "r"(r12), "r"(r1) : "memory");
    return r0;
}

[[gnu::always_inline]] inline uint32_t syscall3(uint8_t num, uint32_t arg0, uint32_t arg1,
                                                 uint32_t arg2) {
    register uint32_t r0 asm("r0") = arg0;
    register uint32_t r1 asm("r1") = arg1;
    register uint32_t r2 asm("r2") = arg2;
    register uint32_t r12 asm("r12") = num;
    asm volatile("svc #0" : "+r"(r0) : "r"(r12), "r"(r1), "r"(r2) : "memory");
    return r0;
}

[[gnu::always_inline]] inline uint32_t syscall4(uint8_t num, uint32_t arg0, uint32_t arg1,
                                                 uint32_t arg2, uint32_t arg3) {
    register uint32_t r0 asm("r0") = arg0;
    register uint32_t r1 asm("r1") = arg1;
    register uint32_t r2 asm("r2") = arg2;
    register uint32_t r3 asm("r3") = arg3;
    register uint32_t r12 asm("r12") = num;
    asm volatile("svc #0" : "+r"(r0) : "r"(r12), "r"(r1), "r"(r2), "r"(r3) : "memory");
    return r0;
}

// ============================================================================
// Type-safe Syscall API
// ============================================================================

/// Get shared memory region pointer
/// Returns: pointer to shared region, or nullptr on error
inline void* sys_get_shared(SharedRegionId id) {
    uint32_t result = syscall1(SYS_GET_SHARED, static_cast<uint32_t>(id));
    return reinterpret_cast<void*>(result);
}

/// Wait for events (blocking)
/// Returns: event mask that was triggered
inline uint32_t sys_wait(uint32_t event_mask) {
    return syscall1(SYS_WAIT, event_mask);
}

/// Wait for events with timeout
/// Returns: event mask, or 0 on timeout (check SyscallError::Timeout)
inline uint32_t sys_wait_timeout(uint32_t event_mask, uint32_t timeout_us) {
    return syscall2(SYS_WAIT_TIMEOUT, event_mask, timeout_us);
}

/// Yield to scheduler
inline void sys_yield() {
    syscall0(SYS_YIELD);
}

/// Notify task with events
inline void sys_notify(uint32_t task_id, uint32_t events) {
    syscall2(SYS_NOTIFY, task_id, events);
}

/// Get system time in microseconds
inline uint64_t sys_get_time() {
    // Low 32 bits in r0, high 32 bits in r1
    register uint32_t r0 asm("r0");
    register uint32_t r1 asm("r1");
    register uint32_t r12 asm("r12") = SYS_GET_TIME;
    asm volatile("svc #0" : "=r"(r0), "=r"(r1) : "r"(r12) : "memory");
    return (static_cast<uint64_t>(r1) << 32) | r0;
}

/// Get current task ID
inline uint32_t sys_get_task_id() {
    return syscall0(SYS_GET_TASK_ID);
}

/// Debug print (DEBUG builds only)
inline void sys_debug_print(const char* msg) {
    syscall1(SYS_DEBUG_PRINT, reinterpret_cast<uint32_t>(msg));
}

/// Trigger kernel panic
[[noreturn]] inline void sys_panic(const char* msg) {
    syscall1(SYS_PANIC, reinterpret_cast<uint32_t>(msg));
    __builtin_unreachable();
}

}  // namespace umi::syscall
