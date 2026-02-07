// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief STM32F4 platform binding for umirtm tests.
/// @note Uses umiport shared UART output backend.

#include <umiport/stm32f4/uart_output.hh>

namespace umi::port {

/// @brief STM32F4 test platform definition.
struct Platform {
    using Output = umi::port::stm32f4::RenodeUartOutput;

    static void init() { Output::init(); }
};

} // namespace umi::port
