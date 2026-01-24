// SPDX-License-Identifier: MIT
// UMI-OS Kernel: Performance Metrics and Measurement
//
// Provides cycle-accurate measurement of kernel operations
// for performance monitoring and optimization.
//
// Enable metrics by defining UMI_ENABLE_METRICS before including this header.
#pragma once

#include <cstdint>
#include <limits>

namespace umi::kernel {

// ============================================================================
// DWT (Data Watchpoint and Trace) Interface
// ============================================================================

/// DWT registers for cycle counting (Cortex-M3/M4/M7)
namespace dwt {

constexpr std::uint32_t BASE = 0xE0001000;
constexpr std::uint32_t DEMCR_BASE = 0xE000EDFC;

// Register offsets
constexpr std::uint32_t CTRL = 0x00;
constexpr std::uint32_t CYCCNT = 0x04;
constexpr std::uint32_t CPICNT = 0x08;
constexpr std::uint32_t EXCCNT = 0x0C;
constexpr std::uint32_t SLEEPCNT = 0x10;
constexpr std::uint32_t LSUCNT = 0x14;
constexpr std::uint32_t FOLDCNT = 0x18;

// CTRL bits
constexpr std::uint32_t CTRL_CYCCNTENA = 1U << 0;

// DEMCR bits
constexpr std::uint32_t DEMCR_TRCENA = 1U << 24;

inline volatile std::uint32_t& reg(std::uint32_t offset) {
    return *reinterpret_cast<volatile std::uint32_t*>(BASE + offset);
}

inline volatile std::uint32_t& demcr() {
    return *reinterpret_cast<volatile std::uint32_t*>(DEMCR_BASE);
}

/// Enable DWT cycle counter
inline void enable() {
    // Enable trace
    demcr() |= DEMCR_TRCENA;
    // Reset cycle counter
    reg(CYCCNT) = 0;
    // Enable cycle counter
    reg(CTRL) |= CTRL_CYCCNTENA;
}

/// Disable DWT cycle counter
inline void disable() {
    reg(CTRL) &= ~CTRL_CYCCNTENA;
}

/// Read cycle count
inline std::uint32_t cycles() {
    return reg(CYCCNT);
}

/// Reset cycle count to zero
inline void reset() {
    reg(CYCCNT) = 0;
}

/// Check if DWT is available
inline bool is_available() {
    // Try to enable and read - if not available, read will be 0
    demcr() |= DEMCR_TRCENA;
    reg(CTRL) |= CTRL_CYCCNTENA;
    reg(CYCCNT) = 0xFFFFFFFF;
    return reg(CYCCNT) != 0;
}

}  // namespace dwt

// ============================================================================
// Kernel Metrics Structure
// ============================================================================

#if defined(UMI_ENABLE_METRICS)

/// Kernel performance metrics
struct KernelMetrics {
    /// Context switch statistics
    struct ContextSwitch {
        std::uint32_t count {0};
        std::uint32_t cycles_min {std::numeric_limits<std::uint32_t>::max()};
        std::uint32_t cycles_max {0};
        std::uint64_t cycles_sum {0};

        /// Record a context switch measurement
        void record(std::uint32_t cycles) noexcept {
            count++;
            cycles_sum += cycles;
            if (cycles < cycles_min) cycles_min = cycles;
            if (cycles > cycles_max) cycles_max = cycles;
        }

        /// Get average cycles per context switch
        std::uint32_t average() const noexcept {
            if (count == 0) return 0;
            return static_cast<std::uint32_t>(cycles_sum / count);
        }
    } context_switch;

    /// ISR latency statistics
    struct IsrLatency {
        std::uint32_t audio_dma_max {0};
        std::uint32_t usb_max {0};
        std::uint32_t systick_max {0};
        std::uint32_t timer_max {0};

        void record_audio_dma(std::uint32_t cycles) noexcept {
            if (cycles > audio_dma_max) audio_dma_max = cycles;
        }
        void record_usb(std::uint32_t cycles) noexcept {
            if (cycles > usb_max) usb_max = cycles;
        }
        void record_systick(std::uint32_t cycles) noexcept {
            if (cycles > systick_max) systick_max = cycles;
        }
        void record_timer(std::uint32_t cycles) noexcept {
            if (cycles > timer_max) timer_max = cycles;
        }
    } isr_latency;

    /// Audio processing statistics
    struct Audio {
        std::uint32_t cycles_last {0};
        std::uint32_t cycles_max {0};
        std::uint32_t overruns {0};   // Buffer wasn't ready in time
        std::uint32_t underruns {0};  // Processing took too long

        void record_processing(std::uint32_t cycles) noexcept {
            cycles_last = cycles;
            if (cycles > cycles_max) cycles_max = cycles;
        }
        void record_overrun() noexcept { overruns++; }
        void record_underrun() noexcept { underruns++; }
    } audio;

    /// Scheduler statistics
    struct Scheduler {
        std::uint32_t preemptions {0};   // Higher priority preempted lower
        std::uint32_t yields {0};         // Voluntary yields
        std::uint32_t idle_entries {0};   // Entered idle/sleep
        std::uint32_t task_switches {0};  // Total task switches

        void record_preemption() noexcept { preemptions++; task_switches++; }
        void record_yield() noexcept { yields++; task_switches++; }
        void record_idle() noexcept { idle_entries++; }
    } scheduler;

    /// Power management statistics
    struct Power {
        std::uint32_t wfi_count {0};
        std::uint32_t stop_mode_count {0};
        std::uint64_t idle_cycles {0};

        void record_wfi() noexcept { wfi_count++; }
        void record_stop() noexcept { stop_mode_count++; }
        void add_idle_cycles(std::uint32_t cycles) noexcept { idle_cycles += cycles; }
    } power;

    /// Reset all statistics
    void reset() noexcept {
        *this = KernelMetrics{};
    }
};

/// Global metrics instance
inline KernelMetrics g_metrics;

// ============================================================================
// RAII Measurement Helpers
// ============================================================================

/// RAII cycle measurement
class ScopedCycles {
public:
    explicit ScopedCycles(std::uint32_t* target) noexcept
        : start_(dwt::cycles()), target_(target) {}

    ~ScopedCycles() noexcept {
        if (target_) {
            *target_ = dwt::cycles() - start_;
        }
    }

    /// Get elapsed cycles without storing
    std::uint32_t elapsed() const noexcept {
        return dwt::cycles() - start_;
    }

    /// Cancel measurement (don't store result)
    void cancel() noexcept {
        target_ = nullptr;
    }

    ScopedCycles(const ScopedCycles&) = delete;
    ScopedCycles& operator=(const ScopedCycles&) = delete;

private:
    std::uint32_t start_;
    std::uint32_t* target_;
};

/// RAII context switch measurement
class ScopedSwitchMeasure {
public:
    ScopedSwitchMeasure() noexcept : start_(dwt::cycles()) {}

    ~ScopedSwitchMeasure() noexcept {
        g_metrics.context_switch.record(dwt::cycles() - start_);
    }

    ScopedSwitchMeasure(const ScopedSwitchMeasure&) = delete;
    ScopedSwitchMeasure& operator=(const ScopedSwitchMeasure&) = delete;

private:
    std::uint32_t start_;
};

// ============================================================================
// Convenience Macros
// ============================================================================

#define UMI_MEASURE_START(name) \
    std::uint32_t _umi_measure_##name##_start = umi::kernel::dwt::cycles()

#define UMI_MEASURE_END(name, target) \
    (target) = umi::kernel::dwt::cycles() - _umi_measure_##name##_start

#define UMI_MEASURE_SWITCH() \
    umi::kernel::ScopedSwitchMeasure _umi_switch_measure

#define UMI_RECORD_SWITCH_CYCLES(cycles) \
    umi::kernel::g_metrics.context_switch.record(cycles)

#define UMI_RECORD_AUDIO_CYCLES(cycles) \
    umi::kernel::g_metrics.audio.record_processing(cycles)

#define UMI_RECORD_ISR_LATENCY(type, cycles) \
    umi::kernel::g_metrics.isr_latency.record_##type(cycles)

#else  // !UMI_ENABLE_METRICS

// Stub implementations when metrics are disabled
struct KernelMetrics {
    void reset() noexcept {}
};
inline KernelMetrics g_metrics;

class ScopedCycles {
public:
    explicit ScopedCycles(std::uint32_t*) noexcept {}
    std::uint32_t elapsed() const noexcept { return 0; }
    void cancel() noexcept {}
};

class ScopedSwitchMeasure {
public:
    ScopedSwitchMeasure() noexcept {}
};

#define UMI_MEASURE_START(name) ((void)0)
#define UMI_MEASURE_END(name, target) ((void)0)
#define UMI_MEASURE_SWITCH() ((void)0)
#define UMI_RECORD_SWITCH_CYCLES(cycles) ((void)0)
#define UMI_RECORD_AUDIO_CYCLES(cycles) ((void)0)
#define UMI_RECORD_ISR_LATENCY(type, cycles) ((void)0)

#endif  // UMI_ENABLE_METRICS

// ============================================================================
// Metrics Initialization
// ============================================================================

/// Initialize metrics system (call once at startup)
inline void metrics_init() noexcept {
#if defined(UMI_ENABLE_METRICS)
    dwt::enable();
    g_metrics.reset();
#endif
}

}  // namespace umi::kernel
