// SPDX-License-Identifier: MIT
// UMI-OS Application Syscall Interface
// Low-level syscall wrappers for ARM Cortex-M

#pragma once

#include <cstdint>

namespace umi::syscall {

// ============================================================================
// Syscall Numbers
// ============================================================================
// Number layout:
//   0–15:   Core API (process control, scheduling, info)
//   16–31:  Reserved (core API expansion)
//   32–47:  Filesystem
//   48–63:  Reserved (storage expansion)
//   64–255: Reserved

namespace nr {
    // --- Core API (0–15) ---
    inline constexpr uint32_t exit             = 0;   ///< Terminate application (unload trigger)
    inline constexpr uint32_t yield            = 1;   ///< Return control to kernel
    inline constexpr uint32_t wait_event       = 2;   ///< Wait for event with optional timeout
    inline constexpr uint32_t get_time         = 3;   ///< Get monotonic time in microseconds
    inline constexpr uint32_t get_shared       = 4;   ///< Get SharedMemory pointer
    inline constexpr uint32_t register_proc    = 5;   ///< Register audio processor
    inline constexpr uint32_t unregister_proc  = 6;   ///< Unregister audio processor (future)
    // 7–15: reserved

    // --- Filesystem (32–47) ---
    inline constexpr uint32_t file_open        = 32;  ///< Open file (future)
    inline constexpr uint32_t file_read        = 33;  ///< Read from file (future)
    inline constexpr uint32_t file_write       = 34;  ///< Write to file (future)
    inline constexpr uint32_t file_close       = 35;  ///< Close file (future)
    inline constexpr uint32_t file_seek        = 36;  ///< Seek within file (future)
    inline constexpr uint32_t file_stat        = 37;  ///< Get file info (future)
    inline constexpr uint32_t dir_open         = 38;  ///< Open directory (future)
    inline constexpr uint32_t dir_read         = 39;  ///< Read directory entry (future)
    inline constexpr uint32_t dir_close        = 40;  ///< Close directory (future)
    // 41–47: reserved
}

// ============================================================================
// Event Bit Definitions
// ============================================================================

namespace event {
    inline constexpr uint32_t Audio       = (1 << 0);  ///< Audio buffer ready
    inline constexpr uint32_t Midi        = (1 << 1);  ///< MIDI data available
    inline constexpr uint32_t VSync       = (1 << 2);  ///< Display refresh
    inline constexpr uint32_t Timer       = (1 << 3);  ///< Timer tick
    inline constexpr uint32_t Button      = (1 << 4);  ///< Button press
    inline constexpr uint32_t Shutdown    = (1 << 31); ///< Shutdown requested
}

// ============================================================================
// Low-level Syscall Invocation
// ============================================================================

#if defined(__ARM_ARCH)

/// Invoke syscall with 0-4 arguments
/// @param nr Syscall number (in r0)
/// @param a0-a3 Arguments (in r1-r4)
/// @return Result (from r0)
inline int32_t call(uint32_t nr, uint32_t a0 = 0, uint32_t a1 = 0,
                    uint32_t a2 = 0, uint32_t a3 = 0) noexcept {
    register uint32_t r0 __asm__("r0") = nr;
    register uint32_t r1 __asm__("r1") = a0;
    register uint32_t r2 __asm__("r2") = a1;
    register uint32_t r3 __asm__("r3") = a2;
    register uint32_t r4 __asm__("r4") = a3;

    __asm__ volatile(
        "svc #0"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3), "r"(r4)
        : "memory"
    );

    return static_cast<int32_t>(r0);
}

#else

// Host/simulation stub - syscalls are no-ops or simulated
inline int32_t call(uint32_t nr, uint32_t a0 = 0, uint32_t a1 = 0,
                    uint32_t a2 = 0, uint32_t a3 = 0) noexcept {
    (void)nr; (void)a0; (void)a1; (void)a2; (void)a3;
    return 0;
}

#endif

// ============================================================================
// Typed Syscall Wrappers
// ============================================================================

/// Terminate application with exit code
[[noreturn]] inline void exit(int code) noexcept {
    call(nr::exit, static_cast<uint32_t>(code));
    while (true) {
        __asm__ volatile("");
    }
}

/// Yield control back to kernel
inline void yield() noexcept {
    call(nr::yield);
}

/// Wait for events with timeout
/// @param mask Event mask to wait for (OR of event:: bits)
/// @param timeout_usec Timeout in microseconds (0 = wait indefinitely)
/// @return Events that occurred (bitmask)
inline uint32_t wait_event(uint32_t mask, uint32_t timeout_usec = 0) noexcept {
    return static_cast<uint32_t>(call(nr::wait_event, mask, timeout_usec));
}

/// Get current time in microseconds (lower 32 bits)
inline uint32_t get_time_usec() noexcept {
    return static_cast<uint32_t>(call(nr::get_time));
}

/// Get shared memory pointer
inline void* get_shared() noexcept {
    return reinterpret_cast<void*>(call(nr::get_shared));
}

// ============================================================================
// Coroutine Scheduler Adapters
// ============================================================================
// Adapters to connect syscalls with umi::coro::Scheduler

namespace coro_adapter {

/// Wait function for Scheduler (WaitFn signature)
inline uint32_t wait(uint32_t mask, uint64_t timeout_us) noexcept {
    return wait_event(mask, static_cast<uint32_t>(timeout_us));
}

/// Time function for Scheduler (TimeFn signature)
inline uint64_t get_time() noexcept {
    return static_cast<uint64_t>(get_time_usec());
}

} // namespace coro_adapter

} // namespace umi::syscall
