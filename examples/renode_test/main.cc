// SPDX-License-Identifier: MIT
// UMI-OS Renode Test - Kernel Main

#include <cstdint>

// Platform-specific includes (resolved by build system)
#include <platform/syscall.hh>
#include <platform/protection.hh>
#include <platform/privilege.hh>

// Drivers
#include <common/systick_driver.hh>
#include <common/uart_driver.hh>
#include <arch/svc_handler.hh>

// Force svc_dispatch symbol emission (called from startup.cc asm)
[[gnu::used]] static auto* const svc_dispatch_ptr = &umi::kernel::svc_dispatch;

// Linker symbols for shared memory regions
extern uint32_t _shared_audio;
extern uint32_t _shared_midi;
extern uint32_t _shared_hwstate;
extern uint32_t _user_ram_start;
extern uint32_t _estack;

// ============================================================================
// Kernel State
// ============================================================================

namespace umi::kernel {

// Syscall implementation state
namespace impl {
// Shared region table (generic, indexed by SharedRegionId)
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
void* shared_regions[max_shared_regions] = {};
uint64_t system_time_us = 0;
uint32_t current_task_id = 1;
}  // namespace impl

// Kernel state pointer (required by svc_handler.hh)
KernelState* g_kernel = nullptr;

}  // namespace umi::kernel

// ============================================================================
// User Application (runs in unprivileged mode)
// ============================================================================

namespace app {

// Audio buffer pointer (obtained once via syscall, then direct access)
float* audio_buffer = nullptr;
constexpr uint32_t BUFFER_SIZE = 64;

// Simple sine wave generator state
float phase = 0.0f;
constexpr float SAMPLE_RATE = 48000.0f;
constexpr float FREQUENCY = 440.0f;  // A4
constexpr float TWO_PI = 6.283185307f;

/// Initialize application (called from user mode)
void init() {
    // Get shared audio buffer via syscall (one-time)
    audio_buffer = static_cast<float*>(
        umi::syscall::sys_get_shared(umi::syscall::SharedRegionId::AUDIO));

    if (audio_buffer) {
        umi::syscall::sys_debug_print("App: Audio buffer acquired\n");
    } else {
        umi::syscall::sys_debug_print("App: Failed to get audio buffer!\n");
    }
}

/// Process audio (called from audio ISR context - no syscalls!)
/// This function directly writes to the shared audio buffer
void process_audio() {
    if (!audio_buffer) return;

    // Generate sine wave - NO SYSCALLS HERE
    float phase_inc = (TWO_PI * FREQUENCY) / SAMPLE_RATE;

    for (uint32_t i = 0; i < BUFFER_SIZE; ++i) {
        // Simple sine approximation (avoiding libm)
        float x = phase;
        // Taylor series: sin(x) ≈ x - x³/6 + x⁵/120
        float x2 = x * x;
        float x3 = x2 * x;
        float x5 = x3 * x2;
        float sample = x - x3 / 6.0f + x5 / 120.0f;

        // Stereo output
        audio_buffer[i * 2] = sample * 0.5f;      // Left
        audio_buffer[i * 2 + 1] = sample * 0.5f;  // Right

        phase += phase_inc;
        if (phase > TWO_PI) {
            phase -= TWO_PI;
        }
    }
}

/// Application main loop
[[noreturn]] void main_loop() {
    init();

    uint32_t counter = 0;
    while (true) {
        // Simulate audio processing at ~750Hz (every ~1.33ms for 64 samples @48kHz)
        // In real system, this would be triggered by DMA complete interrupt
        process_audio();

        // Periodic status via syscall (low frequency, OK to use syscall)
        if (++counter >= 1000) {
            counter = 0;
            umi::syscall::sys_debug_print("App: Running...\n");
        }

        // Yield to other tasks (syscall)
        umi::syscall::sys_yield();
    }
}

}  // namespace app

// ============================================================================
// SysTick Callback
// ============================================================================

void on_systick(void*) {
    // Update system time
    umi::kernel::impl::system_time_us += 1000;  // 1ms tick
}

// ============================================================================
// Kernel Main
// ============================================================================

extern "C" int main() {
    using namespace umi;

    // Initialize UART driver first
    driver::UartConfig uart_cfg = {
        .baud_rate = 115200, .data_bits = 8, .stop_bits = 1, .parity = 0};
    driver::uart::init(&uart_cfg);

    // Print banner
    driver::uart::puts("\n");
    driver::uart::puts("================================\n");
    driver::uart::puts("  UMI-OS v0.2.0 (Renode Test)\n");
    driver::uart::puts("================================\n");

    // Register shared memory regions (generic table-based)
    kernel::impl::register_shared(
        static_cast<uint8_t>(syscall::SharedRegionId::AUDIO), &_shared_audio);
    kernel::impl::register_shared(
        static_cast<uint8_t>(syscall::SharedRegionId::MIDI), &_shared_midi);
    kernel::impl::register_shared(
        static_cast<uint8_t>(syscall::SharedRegionId::HW_STATE), &_shared_hwstate);

    driver::uart::puts("Shared memory initialized\n");

    // Initialize MPU
    if (mpu::is_available()) {
        driver::uart::puts("MPU available, ");
        mpu::init_umi_regions();
        driver::uart::puts("regions configured\n");
    } else {
        driver::uart::puts("MPU not available\n");
    }

    // Initialize SysTick driver (1ms ticks)
    driver::TimerConfig timer_cfg = {.tick_hz = 1000};
    driver::systick::init(&timer_cfg);
    driver::systick::set_callback(on_systick, nullptr);
    driver::uart::puts("SysTick initialized (1ms)\n");

    // Set up vector table for SVC handler
    // (SVC_Handler is already in vector table from startup.cc)

    driver::uart::puts("Kernel ready, entering user mode\n");
    driver::uart::puts("--------------------------------\n");

    // Prepare user stack
    // User stack starts at _user_ram_start, grows down from top
    uint32_t user_stack_top = reinterpret_cast<uint32_t>(&_estack) - 0x1000;

    // Enter user mode and start application
    privilege::enter_user_mode(user_stack_top, app::main_loop);

    // Never reached
    return 0;
}
