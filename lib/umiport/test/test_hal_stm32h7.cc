// SPDX-License-Identifier: MIT
// STM32H7 HAL host unit tests
// Tests constexpr clock calculation helpers and GPIO utility functions

#include "test_common.hh"

#include <mcu/rcc.hh>

using namespace umi::stm32h7;

int main() {
    // ================================================================
    // PLL calculation tests
    // ================================================================

    SECTION("PLL ref clock");
    {
        PllConfig cfg{.src_hz = 16'000'000, .m = 8, .n = 240, .p = 1};
        CHECK_EQ(static_cast<int>(pll_ref_hz(cfg)), 2'000'000, "HSE/8 = 2MHz");
    }

    SECTION("PLL VCO 480MHz");
    {
        PllConfig cfg{.src_hz = 16'000'000, .m = 8, .n = 240, .p = 1};
        CHECK_EQ(static_cast<int>(pll_vco_hz(cfg)), 480'000'000, "2MHz * 240 = 480MHz");
    }

    SECTION("PLL P output");
    {
        PllConfig cfg{.src_hz = 16'000'000, .m = 8, .n = 240, .p = 1};
        CHECK_EQ(static_cast<int>(pll_p_hz(cfg)), 480'000'000, "VCO/1 = 480MHz");
    }

    SECTION("PLL P divided");
    {
        PllConfig cfg{.src_hz = 16'000'000, .m = 8, .n = 240, .p = 2};
        CHECK_EQ(static_cast<int>(pll_p_hz(cfg)), 240'000'000, "VCO/2 = 240MHz");
    }

    SECTION("PLL2 SAI clock");
    {
        PllConfig cfg{.src_hz = 16'000'000, .m = 4, .n = 100, .p = 2};
        CHECK_EQ(static_cast<int>(pll_ref_hz(cfg)), 4'000'000, "ref = 4MHz");
        CHECK_EQ(static_cast<int>(pll_vco_hz(cfg)), 400'000'000, "VCO = 400MHz");
        CHECK_EQ(static_cast<int>(pll_p_hz(cfg)), 200'000'000, "P = 200MHz");
    }

    SECTION("PLL constexpr");
    {
        constexpr PllConfig cfg{.src_hz = 16'000'000, .m = 8, .n = 240, .p = 1};
        static_assert(pll_vco_hz(cfg) == 480'000'000);
        static_assert(pll_p_hz(cfg) == 480'000'000);
        static_assert(pll_ref_hz(cfg) == 2'000'000);
        CHECK(true, "static_assert passed");
    }

    // ================================================================
    // Flash wait state tests
    // ================================================================

    SECTION("Flash wait states VOS0 boost");
    {
        CHECK_EQ(static_cast<int>(flash_wait_states(240'000'000, true)), 4, "240MHz → WS4");
        CHECK_EQ(static_cast<int>(flash_wait_states(210'000'000, true)), 3, "210MHz → WS3");
        CHECK_EQ(static_cast<int>(flash_wait_states(185'000'000, true)), 2, "185MHz → WS2");
        CHECK_EQ(static_cast<int>(flash_wait_states(140'000'000, true)), 1, "140MHz → WS1");
        CHECK_EQ(static_cast<int>(flash_wait_states(70'000'000, true)), 0, "70MHz → WS0");
    }

    SECTION("Flash wait states VOS1");
    {
        CHECK_EQ(static_cast<int>(flash_wait_states(200'000'000, false)), 2, "200MHz → WS2");
        CHECK_EQ(static_cast<int>(flash_wait_states(140'000'000, false)), 1, "140MHz → WS1");
        CHECK_EQ(static_cast<int>(flash_wait_states(70'000'000, false)), 0, "70MHz → WS0");
    }

    SECTION("Flash constexpr");
    {
        static_assert(flash_wait_states(240'000'000, true) == 4);
        static_assert(flash_wait_states(70'000'000, false) == 0);
        CHECK(true, "static_assert passed");
    }

    // ================================================================
    // GPIO helper tests
    // ================================================================

    SECTION("GPIO 2-bit mask");
    {
        CHECK_EQ(static_cast<int>(gpio_2bit_mask(0)), 0x3, "pin0");
        CHECK_EQ(static_cast<int>(gpio_2bit_mask(7)), 0x3 << 14, "pin7");
        CHECK_EQ(static_cast<int>(gpio_2bit_mask(15)), static_cast<int>(0x3u << 30), "pin15");
    }

    SECTION("GPIO 1-bit mask");
    {
        CHECK_EQ(static_cast<int>(gpio_1bit_mask(0)), 1, "pin0");
        CHECK_EQ(static_cast<int>(gpio_1bit_mask(7)), 1 << 7, "pin7");
    }

    SECTION("GPIO AF register index");
    {
        CHECK_EQ(static_cast<int>(gpio_af_reg_index(0)), 0, "pin0 → AFRL");
        CHECK_EQ(static_cast<int>(gpio_af_reg_index(7)), 0, "pin7 → AFRL");
        CHECK_EQ(static_cast<int>(gpio_af_reg_index(8)), 1, "pin8 → AFRH");
        CHECK_EQ(static_cast<int>(gpio_af_reg_index(15)), 1, "pin15 → AFRH");
    }

    SECTION("GPIO AF shift");
    {
        CHECK_EQ(static_cast<int>(gpio_af_shift(0)), 0, "pin0");
        CHECK_EQ(static_cast<int>(gpio_af_shift(5)), 20, "pin5");
        CHECK_EQ(static_cast<int>(gpio_af_shift(12)), 16, "pin12");
    }

    SECTION("GPIO constexpr");
    {
        static_assert(gpio_2bit_mask(0) == 0x3);
        static_assert(gpio_1bit_mask(7) == 128);
        static_assert(gpio_af_reg_index(15) == 1);
        static_assert(gpio_af_shift(3) == 12);
        CHECK(true, "static_assert passed");
    }

    TEST_SUMMARY();
}
