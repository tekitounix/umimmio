// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief STM32F4 platform binding with DWT timer and USART1 output.

#include <cstdint>
#include <umimmio/mmio.hh>

#include "../../dwt.hh"

namespace umi::bench::target {

namespace mm = umi::mmio;

/// @brief Minimal USART1 output backend for STM32F4/Renode.
struct RenodeUartOutput {
    /// @brief Enable USART1 clock and transmitter.
    static void init() {
        transport().modify(RCC::APB2ENR::USART1EN::Set{});
        transport().modify(USART1::CR1::UE::Set{}, USART1::CR1::TE::Set{});
    }

    /// @brief Transmit one character via USART1.
    /// @param c Character to transmit.
    /// @warning Busy-waits until TXE is ready.
    static void putc(char c) {
        while (!transport().is(USART1::SR::TXE::Set{})) {
        } // Wait for TXE
        transport().write(USART1::DR::value(static_cast<std::uint32_t>(c)));
    }

    /// @brief Transmit a null-terminated string.
    /// @param s String to transmit.
    static void puts(const char* s) {
        while (*s != '\0') {
            putc(*s++);
        }
    }

    /// @brief Print an unsigned integer in decimal.
    /// @param value Value to print.
    static void print_uint(std::uint64_t value) {
        if (value == 0) {
            putc('0');
            return;
        }
        char buf[21];
        int i = 0;
        while (value > 0) {
            buf[i++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
        while (i--) {
            putc(buf[i]);
        }
    }

    /// @brief Print a floating-point value with two decimals.
    /// @param value Value to print.
    static void print_double(double value) {
        if (value < 0.0) {
            putc('-');
            value = -value;
        }
        auto integer_part = static_cast<std::uint64_t>(value);
        print_uint(integer_part);
        putc('.');
        double frac = value - static_cast<double>(integer_part);
        // 2 decimal places
        auto frac_int = static_cast<std::uint64_t>(frac * 100.0 + 0.5);
        if (frac_int < 10) {
            putc('0');
        }
        print_uint(frac_int);
    }

  private:
    /// @brief RCC register subset used for USART1 clock enable.
    struct RCC : mm::Device<mm::RW, mm::DirectTransportTag> {
        static constexpr mm::Addr base_address = 0x40023800;

        struct APB2ENR : mm::Register<RCC, 0x44, 32> {
            struct USART1EN : mm::Field<APB2ENR, 4, 1> {};
        };
    };

    /// @brief USART1 register subset used for transmit-only output.
    struct USART1 : mm::Device<mm::RW, mm::DirectTransportTag> {
        static constexpr mm::Addr base_address = 0x40011000;

        struct SR : mm::Register<USART1, 0x00, 32, mm::RO> {
            struct TXE : mm::Field<SR, 7, 1> {};
        };

        struct DR : mm::Register<USART1, 0x04, 32> {};

        struct CR1 : mm::Register<USART1, 0x0C, 32> {
            struct UE : mm::Field<CR1, 13, 1> {};
            struct TE : mm::Field<CR1, 3, 1> {};
        };
    };

    /// @brief Access singleton MMIO transport instance.
    /// @return Transport object reference.
    static mm::DirectTransport<>& transport() {
        static mm::DirectTransport<> transport;
        return transport;
    }
};

/// @brief STM32F4 benchmark platform definition.
struct Platform {
    /// @brief Timer backend.
    using Timer = DwtTimer;
    /// @brief Output backend.
    using Output = RenodeUartOutput;

    /// @brief Platform name shown in reports.
    /// @return `"stm32f4"`.
    static constexpr const char* target_name() { return "stm32f4"; }
    /// @brief Timer unit shown in reports.
    /// @return `"cy"`.
    static constexpr const char* timer_unit() { return "cy"; }

    /// @brief Initialize timer and output backend.
    static void init() {
        Timer::enable();
        Output::init();
    }

    /// @brief Halt the CPU in low-power wait-for-interrupt loop.
    [[noreturn]] static void halt() {
        while (true) {
            asm volatile("wfi");
        }
    }
};

} // namespace umi::bench::target

namespace umi::bench {
/// @brief Convenience alias to the selected target platform type.
using Platform = target::Platform;
} // namespace umi::bench
