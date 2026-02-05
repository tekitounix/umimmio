// STM32F4 (Cortex-M4) Startup
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
void Reset_Handler();
void Default_Handler();
int main();
}

__attribute__((section(".isr_vector"), used)) const std::array<const void*, 16> g_vector_table = {
    reinterpret_cast<const void*>(&_estack),
    reinterpret_cast<const void*>(Reset_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    reinterpret_cast<const void*>(Default_Handler),
    nullptr,
    nullptr,
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
};

extern "C" __attribute__((noreturn)) void Reset_Handler() {
    const auto data_size =
        static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(&_edata) - reinterpret_cast<std::uintptr_t>(&_sdata));
    std::memcpy(static_cast<void*>(&_sdata), static_cast<const void*>(&_sidata), data_size);

    const auto bss_size =
        static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(&_ebss) - reinterpret_cast<std::uintptr_t>(&_sbss));
    std::memset(static_cast<void*>(&_sbss), 0, bss_size);

    constexpr std::uintptr_t cpacr_addr = 0xE000ED88u;
    auto* const cpacr = reinterpret_cast<volatile std::uint32_t*>(cpacr_addr);
    *cpacr |= (0xFU << 20);
    asm volatile("dsb\n isb" ::: "memory");

    main();
    while (true) {
        asm volatile("wfi");
    }
}

extern "C" void Default_Handler() {
    while (true) {
        asm volatile("bkpt #0");
    }
}

extern "C" __attribute__((alias("Reset_Handler"), noreturn)) void _start();
