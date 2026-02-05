// umibench Renode Test for STM32F4
#include <cstdint>

namespace {

// Minimal UART output for Renode
volatile std::uint32_t& uart_dr = *reinterpret_cast<volatile std::uint32_t*>(0x40011004u);
volatile std::uint32_t& uart_sr = *reinterpret_cast<volatile std::uint32_t*>(0x40011000u);

void uart_putc(char c) {
    while ((uart_sr & (1u << 7)) == 0u) {
    } // Wait for TXE
    uart_dr = static_cast<std::uint32_t>(c);
}

void uart_puts(const char* s) {
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

} // namespace

int main() {
    uart_puts("umibench STM32F4 Renode Test\r\n");

    // Simple benchmark test
    volatile std::uint32_t counter = 0;
    for (std::uint32_t i = 0; i < 1000000; ++i) {
        counter = counter + 1;
    }

    uart_puts("Test completed!\r\n");

    while (true) {
        asm volatile("wfi");
    }
    return 0;
}
