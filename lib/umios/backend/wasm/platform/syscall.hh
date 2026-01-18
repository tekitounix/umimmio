// SPDX-License-Identifier: MIT
// UMI-OS Syscall Implementation for WASM (direct call)
#pragma once

#include <cstdint>
#include "../../../kernel/syscall/syscall_numbers.hh"

// Forward declarations - implemented by WASM kernel
namespace umi::kernel {
void* kernel_get_shared(uint32_t id);
uint32_t kernel_wait(uint32_t mask);
uint32_t kernel_wait_timeout(uint32_t mask, uint32_t timeout_us);
void kernel_yield();
void kernel_notify(uint32_t task_id, uint32_t events);
uint64_t kernel_get_time();
uint32_t kernel_get_task_id();
void kernel_debug_print(const char* msg);
[[noreturn]] void kernel_panic(const char* msg);
}  // namespace umi::kernel

namespace umi::syscall {

// ============================================================================
// WASM Syscall Implementation (direct function calls)
// ============================================================================
// In WASM, there's no hardware privilege separation. Syscalls are
// implemented as direct function calls to the kernel module.
// The WASM sandbox provides isolation.

inline void* sys_get_shared(SharedRegionId id) {
    return kernel::kernel_get_shared(static_cast<uint32_t>(id));
}

inline uint32_t sys_wait(uint32_t event_mask) {
    return kernel::kernel_wait(event_mask);
}

inline uint32_t sys_wait_timeout(uint32_t event_mask, uint32_t timeout_us) {
    return kernel::kernel_wait_timeout(event_mask, timeout_us);
}

inline void sys_yield() {
    kernel::kernel_yield();
}

inline void sys_notify(uint32_t task_id, uint32_t events) {
    kernel::kernel_notify(task_id, events);
}

inline uint64_t sys_get_time() {
    return kernel::kernel_get_time();
}

inline uint32_t sys_get_task_id() {
    return kernel::kernel_get_task_id();
}

inline void sys_debug_print(const char* msg) {
    kernel::kernel_debug_print(msg);
}

[[noreturn]] inline void sys_panic(const char* msg) {
    kernel::kernel_panic(msg);
}

}  // namespace umi::syscall
