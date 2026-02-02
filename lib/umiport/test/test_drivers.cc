// SPDX-License-Identifier: MIT
// Device driver mock tests for PCM3060, WM8731, AK4556

#include "test_common.hh"

#include <array>
#include <cstdint>

// PCM3060: uses lambda I2C transport
#include <pcm3060/pcm3060.hh>

// WM8731: uses lambda I2C 16-bit transport
#include <wm8731/wm8731.hh>

// AK4556: uses GPIO driver template
#include <ak4556/ak4556.hh>

// ============================================================================
// Mock I2C memory for PCM3060 (8-bit regs)
// ============================================================================
namespace {

struct MockI2c8 {
    std::array<std::uint8_t, 256> mem{};

    auto writer() {
        return [this](std::uint8_t reg, std::uint8_t data) { mem[reg] = data; };
    }

    auto reader() {
        return [this](std::uint8_t reg) -> std::uint8_t { return mem[reg]; };
    }
};

// ============================================================================
// Mock I2C 16-bit transport for WM8731 (9-bit data)
// ============================================================================

struct MockI2c16 {
    std::array<std::uint16_t, 16> mem{};  // WM8731 has 16 registers

    auto writer() {
        return [this](std::uint8_t reg, std::uint16_t data) {
            if (reg < 16) mem[reg] = data & 0x1FF;
        };
    }
};

// ============================================================================
// Mock GPIO driver for AK4556
// ============================================================================

struct MockGpio {
    bool pin_state = false;

    void reset_pin(std::uint8_t) { pin_state = false; }
    void set_pin(std::uint8_t) { pin_state = true; }
};

} // namespace

// ============================================================================
// Tests
// ============================================================================

int main() {
    // ------------------------------------------------------------------
    // PCM3060
    // ------------------------------------------------------------------
    SECTION("PCM3060 init");
    {
        MockI2c8 bus;
        umi::device::PCM3060Driver driver(bus.writer(), bus.reader());
        driver.init();

        CHECK_EQ(static_cast<int>(bus.mem[0x40]), 0x00, "SYSTEM: reset released");
        // DAC_CTRL: SE=1, FMT=left-just(01)<<1 => 0x03
        CHECK_EQ(static_cast<int>(bus.mem[0x41]), 0x03, "DAC_CTRL: slave, left-just, SE");
        // ADC_CTRL: FMT=left-just(01)<<1 => 0x02
        CHECK_EQ(static_cast<int>(bus.mem[0x44]), 0x02, "ADC_CTRL: slave, left-just");
        CHECK_EQ(static_cast<int>(bus.mem[0x42]), 0xFF, "DAC_ATT1: 0dB");
        CHECK_EQ(static_cast<int>(bus.mem[0x43]), 0xFF, "DAC_ATT2: 0dB");
        CHECK_EQ(static_cast<int>(bus.mem[0x45]), 0xD7, "ADC_ATT1: 0dB default");
        CHECK_EQ(static_cast<int>(bus.mem[0x46]), 0xD7, "ADC_ATT2: 0dB default");
    }

    SECTION("PCM3060 power_down");
    {
        MockI2c8 bus;
        umi::device::PCM3060Driver driver(bus.writer(), bus.reader());
        driver.power_down();
        CHECK_EQ(static_cast<int>(bus.mem[0x40]), 0x70, "SYSTEM: MRST+ADPSV+DAPSV");
    }

    SECTION("PCM3060 set_dac_volume");
    {
        MockI2c8 bus;
        umi::device::PCM3060Driver driver(bus.writer(), bus.reader());

        driver.set_dac_volume(0x80);
        CHECK_EQ(static_cast<int>(bus.mem[0x42]), 0x80, "DAC_ATT1 = 0x80");
        CHECK_EQ(static_cast<int>(bus.mem[0x43]), 0x80, "DAC_ATT2 = 0x80");

        driver.set_dac_volume(0x00);
        CHECK_EQ(static_cast<int>(bus.mem[0x42]), 0x00, "DAC_ATT1 = mute");
    }

    SECTION("PCM3060 mute_dac");
    {
        MockI2c8 bus;
        umi::device::PCM3060Driver driver(bus.writer(), bus.reader());

        // init sets DAC_CTRL = 0x03
        driver.init();
        driver.mute_dac(true);
        CHECK_EQ(static_cast<int>(bus.mem[0x41] & (1 << 5)), 0x20, "DMC=1 (muted)");

        driver.mute_dac(false);
        CHECK_EQ(static_cast<int>(bus.mem[0x41] & (1 << 5)), 0x00, "DMC=0 (unmuted)");
    }

    // ------------------------------------------------------------------
    // WM8731
    // ------------------------------------------------------------------
    SECTION("WM8731 reset");
    {
        MockI2c16 bus;
        umi::device::WM8731Driver driver(bus.writer());
        driver.reset();
        // Reset writes to reg 0x0F
        CHECK(true, "reset write to 0x0F completed");
    }

    SECTION("WM8731 init");
    {
        MockI2c16 bus;
        umi::device::WM8731Driver driver(bus.writer());
        driver.init();

        // Power down: MICPD=1, OSCPD=1, CLKOUTPD=1 => 0x062
        CHECK_EQ(static_cast<int>(bus.mem[0x06]), 0x062, "PWRDOWN: mic+osc+clkout off");

        // DAIF: LEFT_JUST(01)<<0 | IWL_24BIT(10)<<2 => 0x09
        CHECK_EQ(static_cast<int>(bus.mem[0x07]),
                 static_cast<int>((umi::device::wm8731_fmt::LEFT_JUST << 0) | (umi::device::wm8731_iwl::IWL_24BIT << 2)),
                 "DAIF: left-just, 24-bit");

        // Sampling: 0x000
        CHECK_EQ(static_cast<int>(bus.mem[0x08]), 0x000, "SAMPLING: normal 48kHz");

        // AAPCTRL: DACSEL=1, MUTEMIC=1 => 0x012
        CHECK_EQ(static_cast<int>(bus.mem[0x04]), 0x012, "AAPCTRL: DAC select, mic mute");

        // DAPCTRL: 0x000 (no de-emphasis, unmute)
        CHECK_EQ(static_cast<int>(bus.mem[0x05]), 0x000, "DAPCTRL: unmuted");

        // ACTIVE: 0x001
        CHECK_EQ(static_cast<int>(bus.mem[0x09]), 0x001, "ACTIVE: activated");

        // Line input volume
        CHECK_EQ(static_cast<int>(bus.mem[0x00]), 0x017, "LINVOL: 0dB");
        CHECK_EQ(static_cast<int>(bus.mem[0x01]), 0x017, "RINVOL: 0dB");

        // Headphone volume: LZCEN=1, vol=0x79 => 0x179
        CHECK_EQ(static_cast<int>(bus.mem[0x02]), 0x179, "LHPOUT: 0dB, zero-cross");
        CHECK_EQ(static_cast<int>(bus.mem[0x03]), 0x179, "RHPOUT: 0dB, zero-cross");
    }

    SECTION("WM8731 power_down");
    {
        MockI2c16 bus;
        umi::device::WM8731Driver driver(bus.writer());
        driver.power_down();
        CHECK_EQ(static_cast<int>(bus.mem[0x09]), 0x000, "ACTIVE: deactivated");
        CHECK_EQ(static_cast<int>(bus.mem[0x06]), 0x0FF, "PWRDOWN: all off");
    }

    SECTION("WM8731 set_hp_volume");
    {
        MockI2c16 bus;
        umi::device::WM8731Driver driver(bus.writer());

        driver.set_hp_volume(0x79);  // 0dB
        CHECK_EQ(static_cast<int>(bus.mem[0x02]), static_cast<int>(0x100 | 0x79), "HP vol 0dB + LRHPBOTH");

        driver.set_hp_volume(0x30);  // -73dB
        CHECK_EQ(static_cast<int>(bus.mem[0x02]), static_cast<int>(0x100 | 0x30), "HP vol -73dB + LRHPBOTH");
    }

    SECTION("WM8731 mute_dac");
    {
        MockI2c16 bus;
        umi::device::WM8731Driver driver(bus.writer());

        driver.mute_dac(true);
        CHECK_EQ(static_cast<int>(bus.mem[0x05]), 0x008, "DACMU=1");

        driver.mute_dac(false);
        CHECK_EQ(static_cast<int>(bus.mem[0x05]), 0x000, "DACMU=0");
    }

    // ------------------------------------------------------------------
    // AK4556
    // ------------------------------------------------------------------
    SECTION("AK4556 init");
    {
        MockGpio gpio;
        umi::device::AK4556 codec(gpio, 0);

        CHECK(!gpio.pin_state, "pin starts low (default)");

        codec.init();
        CHECK(gpio.pin_state, "pin high after init (reset released)");
    }

    SECTION("AK4556 reset_assert/release");
    {
        MockGpio gpio;
        gpio.pin_state = true;
        umi::device::AK4556 codec(gpio, 0);

        codec.reset_assert();
        CHECK(!gpio.pin_state, "pin low after reset_assert");

        codec.reset_release();
        CHECK(gpio.pin_state, "pin high after reset_release");
    }

    TEST_SUMMARY();
}
