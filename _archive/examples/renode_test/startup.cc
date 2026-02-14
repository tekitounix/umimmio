// SPDX-License-Identifier: MIT
// UMI-OS Startup Code for STM32F4 (Renode test)

#include <cstdint>
#include <cstring>

// Forward declaration of svc_dispatch (defined in main.cc via svc_handler.hh)
namespace umi::kernel {
struct ExceptionFrame;
}  // namespace umi::kernel
extern "C" void svc_dispatch(umi::kernel::ExceptionFrame* frame, uint8_t svc_num);

// Linker symbols
extern uint32_t _sidata;      // Start of .data in Flash
extern uint32_t _sdata;       // Start of .data in RAM
extern uint32_t _edata;       // End of .data in RAM
extern uint32_t _sbss;        // Start of .bss
extern uint32_t _ebss;        // End of .bss
extern uint32_t _estack;      // Initial stack pointer

// Forward declarations
extern "C" void Reset_Handler();
extern "C" void Default_Handler();
extern "C" void NMI_Handler();
extern "C" void HardFault_Handler();
extern "C" void MemManage_Handler();
extern "C" void BusFault_Handler();
extern "C" void UsageFault_Handler();
extern "C" void SVC_Handler();
extern "C" void PendSV_Handler();
extern "C" void SysTick_Handler();

// Main entry (kernel init)
extern "C" int main();

// ============================================================================
// Vector Table (placed at 0x08000000)
// ============================================================================

__attribute__((section(".isr_vector"), used))
const void* const g_vector_table[] = {
    reinterpret_cast<const void*>(&_estack),  // Initial SP
    reinterpret_cast<const void*>(Reset_Handler),
    reinterpret_cast<const void*>(NMI_Handler),
    reinterpret_cast<const void*>(HardFault_Handler),
    reinterpret_cast<const void*>(MemManage_Handler),
    reinterpret_cast<const void*>(BusFault_Handler),
    reinterpret_cast<const void*>(UsageFault_Handler),
    nullptr,  // Reserved
    nullptr,  // Reserved
    nullptr,  // Reserved
    nullptr,  // Reserved
    reinterpret_cast<const void*>(SVC_Handler),
    nullptr,  // Debug Monitor
    nullptr,  // Reserved
    reinterpret_cast<const void*>(PendSV_Handler),
    reinterpret_cast<const void*>(SysTick_Handler),
    // External IRQs (82 for STM32F4)
    // ... filled with Default_Handler or specific handlers
};

// ============================================================================
// Reset Handler
// ============================================================================

extern "C" __attribute__((noreturn)) void Reset_Handler() {
    // Copy .data from Flash to RAM
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    // Zero .bss
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    // Enable FPU (Cortex-M4F)
    // CPACR: CP10, CP11 = Full access
    volatile uint32_t& CPACR = *reinterpret_cast<volatile uint32_t*>(0xE000ED88);
    CPACR |= (0xFU << 20);
    asm volatile("dsb\n isb" ::: "memory");

    // Call global constructors
    extern void (*__init_array_start[])();
    extern void (*__init_array_end[])();
    for (auto fn = __init_array_start; fn < __init_array_end; ++fn) {
        (*fn)();
    }

    // Jump to main
    main();

    // Should never return
    while (true) {
        asm volatile("wfi");
    }
}

// ============================================================================
// Default Handlers
// ============================================================================

extern "C" __attribute__((weak)) void Default_Handler() {
    while (true) {
        asm volatile("bkpt #0");
    }
}

extern "C" __attribute__((weak, alias("Default_Handler"))) void NMI_Handler();

extern "C" void HardFault_Handler() {
    // Get fault status
    volatile uint32_t& HFSR = *reinterpret_cast<volatile uint32_t*>(0xE000ED2C);
    volatile uint32_t& CFSR = *reinterpret_cast<volatile uint32_t*>(0xE000ED28);
    (void)HFSR;
    (void)CFSR;

    while (true) {
        asm volatile("bkpt #1");
    }
}

extern "C" void MemManage_Handler() {
    // Memory Management Fault - likely MPU violation
    volatile uint32_t& MMFAR = *reinterpret_cast<volatile uint32_t*>(0xE000ED34);
    volatile uint32_t& CFSR = *reinterpret_cast<volatile uint32_t*>(0xE000ED28);
    (void)MMFAR;
    (void)CFSR;

    while (true) {
        asm volatile("bkpt #2");
    }
}

extern "C" void BusFault_Handler() {
    while (true) {
        asm volatile("bkpt #3");
    }
}

extern "C" void UsageFault_Handler() {
    while (true) {
        asm volatile("bkpt #4");
    }
}

// SVC_Handler - naked function to dispatch syscalls
extern "C" __attribute__((naked)) void SVC_Handler() {
    asm volatile(
        // Determine which stack was used (EXC_RETURN bit 2)
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"  // Using MSP
        "mrsne r0, psp\n"  // Using PSP

        // r0 = exception frame pointer
        // r12 contains syscall number (set by caller)
        // Pass r12 as second argument (r1)
        "mov r1, r12\n"

        // Call C++ dispatcher
        "b svc_dispatch\n");
}

// PendSV_Handler is provided by scheduler
// SysTick_Handler is provided by timer driver

extern "C" __attribute__((weak)) void PendSV_Handler() {
    // Default: just return (no context switch)
}

extern "C" __attribute__((weak)) void SysTick_Handler() {
    // Default: just return
}
