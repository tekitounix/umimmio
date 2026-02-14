// SPDX-License-Identifier: MIT
// WM8731 Audio Codec - Register definitions via umimmio
// Reference: WM8731 datasheet, Table 32 (Register Map)
#pragma once

#include <umimmio/mmio.hh>

namespace umi::device {

// NOLINTBEGIN(readability-identifier-naming)

/// WM8731 stereo ADC/DAC register map (I2C, 9-bit data with 7-bit address)
/// Default I2C address: 0x1A (CSB=0 on Daisy Seed Rev5)
struct WM8731 : umi::mmio::Device<umi::mmio::RW, umi::mmio::I2CTransportTag> {
    static constexpr std::uint8_t i2c_address = 0x1A;

    // WM8731 uses 7-bit register address + 9-bit data packed into 16-bit I2C write.
    // Register "offset" here represents the 7-bit address (0x00-0x0F).

    struct LINVOL : umi::mmio::Register<WM8731, 0x00, 9> {
        struct LINVOL_F : umi::mmio::Field<LINVOL, 0, 5> {};    // Left line input volume
        struct LINMUTE : umi::mmio::Field<LINVOL, 7, 1> {};     // Mute
        struct LRINBOTH : umi::mmio::Field<LINVOL, 8, 1> {};    // Both channels
    };

    struct RINVOL : umi::mmio::Register<WM8731, 0x01, 9> {
        struct RINVOL_F : umi::mmio::Field<RINVOL, 0, 5> {};
        struct RINMUTE : umi::mmio::Field<RINVOL, 7, 1> {};
        struct RLINBOTH : umi::mmio::Field<RINVOL, 8, 1> {};
    };

    struct LHPOUT : umi::mmio::Register<WM8731, 0x02, 9> {
        struct LHPVOL : umi::mmio::Field<LHPOUT, 0, 7> {};
        struct LZCEN : umi::mmio::Field<LHPOUT, 7, 1> {};
        struct LRHPBOTH : umi::mmio::Field<LHPOUT, 8, 1> {};
    };

    struct RHPOUT : umi::mmio::Register<WM8731, 0x03, 9> {
        struct RHPVOL : umi::mmio::Field<RHPOUT, 0, 7> {};
        struct RZCEN : umi::mmio::Field<RHPOUT, 7, 1> {};
        struct RLHPBOTH : umi::mmio::Field<RHPOUT, 8, 1> {};
    };

    struct AAPCTRL : umi::mmio::Register<WM8731, 0x04, 9> {
        struct MICBOOST : umi::mmio::Field<AAPCTRL, 0, 1> {};
        struct MUTEMIC : umi::mmio::Field<AAPCTRL, 1, 1> {};
        struct INSEL : umi::mmio::Field<AAPCTRL, 2, 1> {};
        struct BYPASS : umi::mmio::Field<AAPCTRL, 3, 1> {};
        struct DACSEL : umi::mmio::Field<AAPCTRL, 4, 1> {};
        struct SIDETONE : umi::mmio::Field<AAPCTRL, 5, 1> {};
        struct SIDEATT : umi::mmio::Field<AAPCTRL, 6, 2> {};
    };

    struct DAPCTRL : umi::mmio::Register<WM8731, 0x05, 9> {
        struct ADCHPD : umi::mmio::Field<DAPCTRL, 0, 1> {};
        struct DEEMP : umi::mmio::Field<DAPCTRL, 1, 2> {};
        struct DACMU : umi::mmio::Field<DAPCTRL, 3, 1> {};
        struct HPOR : umi::mmio::Field<DAPCTRL, 4, 1> {};
    };

    struct PWRDOWN : umi::mmio::Register<WM8731, 0x06, 9> {
        struct LINEINPD : umi::mmio::Field<PWRDOWN, 0, 1> {};
        struct MICPD : umi::mmio::Field<PWRDOWN, 1, 1> {};
        struct ADCPD : umi::mmio::Field<PWRDOWN, 2, 1> {};
        struct DACPD : umi::mmio::Field<PWRDOWN, 3, 1> {};
        struct OUTPD : umi::mmio::Field<PWRDOWN, 4, 1> {};
        struct OSCPD : umi::mmio::Field<PWRDOWN, 5, 1> {};
        struct CLKOUTPD : umi::mmio::Field<PWRDOWN, 6, 1> {};
        struct POWEROFF : umi::mmio::Field<PWRDOWN, 7, 1> {};
    };

    struct DAIF : umi::mmio::Register<WM8731, 0x07, 9> {
        struct FORMAT : umi::mmio::Field<DAIF, 0, 2> {};
        struct IWL : umi::mmio::Field<DAIF, 2, 2> {};
        struct LRP : umi::mmio::Field<DAIF, 4, 1> {};
        struct LRSWAP : umi::mmio::Field<DAIF, 5, 1> {};
        struct MS : umi::mmio::Field<DAIF, 6, 1> {};
        struct BCLKINV : umi::mmio::Field<DAIF, 7, 1> {};
    };

    struct SAMPLING : umi::mmio::Register<WM8731, 0x08, 9> {
        struct NORMAL_USB : umi::mmio::Field<SAMPLING, 0, 1> {};
        struct BOSR : umi::mmio::Field<SAMPLING, 1, 1> {};
        struct SR : umi::mmio::Field<SAMPLING, 2, 4> {};
        struct CLKIDIV2 : umi::mmio::Field<SAMPLING, 6, 1> {};
        struct CLKODIV2 : umi::mmio::Field<SAMPLING, 7, 1> {};
    };

    struct ACTIVE : umi::mmio::Register<WM8731, 0x09, 9> {
        struct ACTIVE_F : umi::mmio::Field<ACTIVE, 0, 1> {};
    };

    struct RESET : umi::mmio::Register<WM8731, 0x0F, 9> {};
};

// Digital audio interface format values
namespace wm8731_fmt {
constexpr std::uint32_t RIGHT_JUST = 0b00;
constexpr std::uint32_t LEFT_JUST  = 0b01;  // MSB-justified
constexpr std::uint32_t I2S        = 0b10;
constexpr std::uint32_t DSP        = 0b11;
} // namespace wm8731_fmt

// Input word length
namespace wm8731_iwl {
constexpr std::uint32_t IWL_16BIT = 0b00;
constexpr std::uint32_t IWL_20BIT = 0b01;
constexpr std::uint32_t IWL_24BIT = 0b10;
constexpr std::uint32_t IWL_32BIT = 0b11;
} // namespace wm8731_iwl

// NOLINTEND(readability-identifier-naming)

} // namespace umi::device
