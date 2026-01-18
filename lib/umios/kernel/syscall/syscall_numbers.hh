// SPDX-License-Identifier: MIT
// UMI-OS Syscall Numbers (platform-independent)
#pragma once

#include <cstdint>

namespace umi::syscall {

// ============================================================================
// Syscall Numbers
// ============================================================================
// Grouped by functionality, each group has 16 numbers.
// This header is shared across all platforms.

// Group 0: Shared Memory Management (0x00-0x0F)
inline constexpr uint8_t SYS_GET_SHARED = 0x00;       // Get shared region pointer
inline constexpr uint8_t SYS_REGISTER_SHARED = 0x01;  // Register shared region (privileged)

// Group 1: Task Control (0x10-0x1F)
inline constexpr uint8_t SYS_CREATE_TASK = 0x10;
inline constexpr uint8_t SYS_DELETE_TASK = 0x11;
inline constexpr uint8_t SYS_SUSPEND_TASK = 0x12;
inline constexpr uint8_t SYS_RESUME_TASK = 0x13;
inline constexpr uint8_t SYS_YIELD = 0x14;
inline constexpr uint8_t SYS_GET_TASK_ID = 0x15;

// Group 2: Notification / Synchronization (0x20-0x2F)
inline constexpr uint8_t SYS_NOTIFY = 0x20;
inline constexpr uint8_t SYS_WAIT = 0x21;
inline constexpr uint8_t SYS_WAIT_TIMEOUT = 0x22;

// Group 3: Protection / Privilege (0x30-0x3F)
inline constexpr uint8_t SYS_CONFIGURE_MPU = 0x30;  // Configure MPU (privileged)

// Group 4: System Information (0x40-0x4F)
inline constexpr uint8_t SYS_GET_TIME = 0x40;
inline constexpr uint8_t SYS_GET_LOAD = 0x41;
inline constexpr uint8_t SYS_GET_VERSION = 0x42;

// Group 5: Debug (0x50-0x5F) - DEBUG builds only
inline constexpr uint8_t SYS_DEBUG_PRINT = 0x50;
inline constexpr uint8_t SYS_PANIC = 0x51;

// ============================================================================
// Shared Region IDs
// ============================================================================

/// Well-known shared region IDs (0-7)
/// Application can define custom regions in the range 8-15.
enum class SharedRegionId : uint8_t {
    // Core regions (0-3)
    AUDIO = 0,        // Audio I/O buffer (RW)
    MIDI = 1,         // MIDI event buffer (RW)
    HW_STATE = 2,     // Hardware state - buttons, encoders, sensors (RO for user)
    DISPLAY = 3,      // Display buffer - LCD/OLED/LED matrix (RW)

    // Reserved for future core use (4-7)
    RESERVED_4 = 4,
    RESERVED_5 = 5,
    RESERVED_6 = 6,
    RESERVED_7 = 7,

    // Application-defined regions (8-15)
    // Use these for custom buffers like:
    //   - Gate/trigger output
    //   - CV output
    //   - Custom sensor data
    //   - Inter-module communication
    APP_0 = 8,
    APP_1 = 9,
    APP_2 = 10,
    APP_3 = 11,
    APP_4 = 12,
    APP_5 = 13,
    APP_6 = 14,
    APP_7 = 15,
};

/// Maximum number of shared regions
inline constexpr uint8_t max_shared_regions = 16;

// ============================================================================
// Syscall Error Codes
// ============================================================================

enum class SyscallError : int32_t {
    Ok = 0,
    InvalidSyscall = -1,
    InvalidParam = -2,
    AccessDenied = -3,
    NotFound = -4,
    Timeout = -5,
    Busy = -6,
};

// ============================================================================
// Kernel Event Flags
// ============================================================================

namespace KernelEvent {
inline constexpr uint32_t AudioReady = (1U << 0);
inline constexpr uint32_t MidiReady = (1U << 1);
inline constexpr uint32_t TimerTick = (1U << 2);
inline constexpr uint32_t UartRx = (1U << 3);
inline constexpr uint32_t UserEvent = (1U << 16);
}  // namespace KernelEvent

}  // namespace umi::syscall
