// SPDX-License-Identifier: MIT
// UMI-OS Kernel Port: Cortex-M4 Context Switch Support
//
// Provides helper constants for context switch implementation.
// PendSV and SVC handlers should be implemented in application code
// as they require project-specific customization (TCB pointer names, etc).
#pragma once

#include <cstdint>
#include "context.hh"

namespace umi::kernel::port::cm4 {

// ============================================================================
// BASEPRI Values for Critical Sections
// ============================================================================

/// Default BASEPRI value for critical sections in PendSV.
/// Set to 0x50 (priority 5) to allow higher priority interrupts.
/// Adjust based on your interrupt priority scheme.
constexpr uint32_t DEFAULT_CRITICAL_BASEPRI = 0x50;

// ============================================================================
// Exception Priority Recommendations
// ============================================================================

/// Recommended exception priorities for kernel operation:
/// - SysTick: 0xF0 (low, but can preempt tasks)
/// - PendSV:  0xFF (lowest, context switch)
/// - SVC:     any (used only for first task start)

constexpr uint8_t RECOMMENDED_SYSTICK_PRIO = 0xF0;
constexpr uint8_t RECOMMENDED_PENDSV_PRIO  = 0xFF;

}  // namespace umi::kernel::port::cm4
