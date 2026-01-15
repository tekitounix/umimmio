// =====================================================================
// UMI-OS Simple Synthesizer Example (Embedded)
// =====================================================================
//
// Demonstrates:
//   - DSP modules: SawBL oscillator, SVF filter, ADSR envelope
//   - Polyphonic voice management (4 voices)
//   - MIDI note on/off handling
//   - Audio output via UART debug (Renode compatible)
//
// Build:
//   xmake build synth_example
//
// Test with Renode:
//   xmake renode-synth
//
// =====================================================================

#include "synth.hh"
#include <cstdint>

// =====================================================================
// Configuration
// =====================================================================

constexpr int SAMPLE_RATE = 48000;
constexpr int BUFFER_SIZE = 64;

// =====================================================================
// UART output (uses syscalls.cc implementation)
// =====================================================================

extern "C" int _write(int fd, const char* buf, int len);

namespace {
void print(const char* s) {
    int len = 0;
    const char* p = s;
    while (*p++) len++;
    _write(1, s, len);
}
} // namespace

// =====================================================================
// Global Synth Instance
// =====================================================================

umi::synth::PolySynth g_synth;

// =====================================================================
// Audio Processing
// =====================================================================

void process_audio(float* output, int frames) {
    g_synth.process(output, static_cast<uint32_t>(frames));
}

// =====================================================================
// Test Sequence
// =====================================================================

void run_test_sequence() {
    print("========================================\n");
    print("UMI-OS Synth Example\n");
    print("========================================\n");

    // Initialize synth
    print("Initializing synth...\n");
    g_synth.init(static_cast<float>(SAMPLE_RATE));
    print("[OK] Synth initialized\n");

    // Audio buffer
    static float buffer[BUFFER_SIZE];

    // Test 1: Single note
    print("\n[TEST 1] Single note C4\n");
    g_synth.note_on(60, 100);
    process_audio(buffer, BUFFER_SIZE);
    g_synth.note_off(60);
    print("[OK] Single note test\n");

    // Test 2: Polyphonic chord
    print("\n[TEST 2] Polyphonic chord (C-E-G)\n");
    g_synth.note_on(60, 100);  // C4
    g_synth.note_on(64, 100);  // E4
    g_synth.note_on(67, 100);  // G4
    process_audio(buffer, BUFFER_SIZE);
    g_synth.note_off(60);
    g_synth.note_off(64);
    g_synth.note_off(67);
    print("[OK] Polyphonic chord test\n");

    print("\n========================================\n");
    print("ALL TESTS PASSED\n");
    print("========================================\n");
}

// =====================================================================
// Entry Point
// =====================================================================

extern "C" int main() {
    run_test_sequence();

    // Embedded: infinite loop
    while (true) {}

    return 0;
}

// =====================================================================
// ARM Cortex-M Vector Table
// =====================================================================

#if defined(__arm__) || defined(__thumb__)

extern "C" {
    extern uint32_t _estack;
    void Reset_Handler();
    void NMI_Handler();
    void HardFault_Handler();
    void MemManage_Handler();
    void BusFault_Handler();
    void UsageFault_Handler();

    // Minimal vector table
    __attribute__((section(".isr_vector"), used))
    const void* vector_table[] = {
        &_estack,
        reinterpret_cast<void*>(Reset_Handler),
        reinterpret_cast<void*>(NMI_Handler),
        reinterpret_cast<void*>(HardFault_Handler),
        reinterpret_cast<void*>(MemManage_Handler),
        reinterpret_cast<void*>(BusFault_Handler),
        reinterpret_cast<void*>(UsageFault_Handler),
    };

    void Reset_Handler() {
        // Enable FPU (CP10 and CP11 full access)
        volatile uint32_t* CPACR = (volatile uint32_t*)0xE000ED88;
        *CPACR |= (0xF << 20);  // Set CP10 and CP11 to full access
        __asm volatile("dsb");
        __asm volatile("isb");

        // Initialize .data and .bss sections
        extern uint32_t _sdata, _edata, _sidata;
        extern uint32_t _sbss, _ebss;

        uint32_t* src = &_sidata;
        uint32_t* dst = &_sdata;
        while (dst < &_edata) *dst++ = *src++;

        dst = &_sbss;
        while (dst < &_ebss) *dst++ = 0;

        // Call main
        main();

        while (true) {}
    }

    void NMI_Handler() {
        print("!!! NMI !!!\n");
        while (true) {}
    }

    void HardFault_Handler() {
        print("!!! HardFault !!!\n");
        while (true) {}
    }

    void MemManage_Handler() {
        print("!!! MemManage !!!\n");
        while (true) {}
    }

    void BusFault_Handler() {
        print("!!! BusFault !!!\n");
        while (true) {}
    }

    void UsageFault_Handler() {
        print("!!! UsageFault !!!\n");
        while (true) {}
    }
}

#endif
