// SPDX-License-Identifier: MIT
// Daisy Pod Board Support Package
// Pin mapping derived from libDaisy daisy_pod.cpp (Rev3/Rev4 compatible)
#pragma once

#include <cstdint>
#include <mcu/gpio.hh>
#include <mcu/adc.hh>

namespace umi::daisy::pod {

// ============================================================================
// Pin Definitions (from libDaisy seed::Dxx → GPIO port/pin)
// ============================================================================

// Buttons (active low, no external pull-up — use internal pull-up)
struct Button1 { using Port = stm32h7::GPIOG; static constexpr std::uint8_t pin = 9; };   // D27
struct Button2 { using Port = stm32h7::GPIOA; static constexpr std::uint8_t pin = 2; };   // D28

// Rotary encoder (quadrature + push button)
struct EncA     { using Port = stm32h7::GPIOD; static constexpr std::uint8_t pin = 11; }; // D26
struct EncB     { using Port = stm32h7::GPIOA; static constexpr std::uint8_t pin = 0; };  // D25
struct EncClick { using Port = stm32h7::GPIOB; static constexpr std::uint8_t pin = 6; };  // D13

// RGB LED 1 (active low / inverted polarity)
struct Led1R { using Port = stm32h7::GPIOC; static constexpr std::uint8_t pin = 1; };     // D20
struct Led1G { using Port = stm32h7::GPIOA; static constexpr std::uint8_t pin = 6; };     // D19
struct Led1B { using Port = stm32h7::GPIOA; static constexpr std::uint8_t pin = 7; };     // D18

// RGB LED 2 (active low / inverted polarity)
struct Led2R { using Port = stm32h7::GPIOB; static constexpr std::uint8_t pin = 1; };     // D17
struct Led2G { using Port = stm32h7::GPIOA; static constexpr std::uint8_t pin = 1; };     // D24
struct Led2B { using Port = stm32h7::GPIOA; static constexpr std::uint8_t pin = 4; };     // D23

// Knobs (ADC1 channels)
struct Knob1 {
    using Port = stm32h7::GPIOC;
    static constexpr std::uint8_t pin = 4;
    static constexpr std::uint8_t adc_channel = 4;   // ADC1_INP4
};
struct Knob2 {
    using Port = stm32h7::GPIOC;
    static constexpr std::uint8_t pin = 0;
    static constexpr std::uint8_t adc_channel = 10;  // ADC1_INP10
};

static constexpr std::uint8_t NUM_KNOBS = 2;

} // namespace umi::daisy::pod
