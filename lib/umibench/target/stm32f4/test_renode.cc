// umibench Renode Test for STM32F4
#include <cstdint>
#include <umibench/bench.hh>

// Minimal UART output for Renode
volatile uint32_t& UART_DR = *reinterpret_cast<volatile uint32_t*>(0x40011004);
volatile uint32_t& UART_SR = *reinterpret_cast<volatile uint32_t*>(0x40011000);

void uart_putc(char c) {
    while (!(UART_SR & (1 << 7))) {}  // Wait for TXE
    UART_DR = c;
}

void uart_puts(const char* s) {
    while (*s) uart_putc(*s++);
}

int main() {
    uart_puts("umibench STM32F4 Renode Test\r\n");
    
    // Simple benchmark test
    volatile uint32_t counter = 0;
    for (uint32_t i = 0; i < 1000000; ++i) {
        counter++;
    }
    
    uart_puts("Test completed!\r\n");
    
    while (true) {
        asm volatile("wfi");
    }
    return 0;
}
