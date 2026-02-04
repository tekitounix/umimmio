// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace umi::bench {

/// UART output for STM32 (USART2)
struct UartOutput {
    static constexpr std::uint32_t usart2_base = 0x40004400;
    static constexpr std::uint32_t rcc_apb1enr = 0x40023840;

    static void init() {
        // Enable USART2 clock
        *reinterpret_cast<volatile std::uint32_t*>(rcc_apb1enr) |= (1u << 17);
        // Enable USART, TX
        *reinterpret_cast<volatile std::uint32_t*>(usart2_base + 0x0C) = (1u << 13) | (1u << 3);
    }

    static void putc(char c) {
        // Wait for TXE
        while (!(*reinterpret_cast<volatile std::uint32_t*>(usart2_base) & 0x80)) {
        }
        *reinterpret_cast<volatile std::uint32_t*>(usart2_base + 0x04) = c;
    }

    static void puts(const char* s) {
        while (*s) {
            putc(*s++);
        }
    }

    static void print_uint(std::uint32_t value) {
        if (value == 0) {
            putc('0');
            return;
        }
        char buf[12];
        int i = 0;
        while (value > 0) {
            buf[i++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
        while (i--) {
            putc(buf[i]);
        }
    }
};

} // namespace umi::bench
