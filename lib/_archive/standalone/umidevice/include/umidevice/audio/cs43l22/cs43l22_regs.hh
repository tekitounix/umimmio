// SPDX-License-Identifier: MIT
// CS43L22 Audio DAC - Register definitions via umimmio
#pragma once

#include <umimmio/mmio.hh>

namespace umi::device {

// NOLINTBEGIN(readability-identifier-naming)

/// CS43L22 Audio DAC register map (I2C, 8-bit registers)
struct CS43L22 : umi::mmio::Device<umi::mmio::RW, umi::mmio::I2CTransportTag> {
    static constexpr std::uint8_t i2c_address = 0x4A;

    struct ID : umi::mmio::Register<CS43L22, 0x01, 8, umi::mmio::RO> {
        struct REVID : umi::mmio::Field<ID, 0, 3> {};
        struct CHIPID : umi::mmio::Field<ID, 3, 5> {};
    };

    struct POWER_CTL1 : umi::mmio::Register<CS43L22, 0x02, 8> {
        using PowerDown = umi::mmio::Value<POWER_CTL1, 0x01>;
        using PowerUp = umi::mmio::Value<POWER_CTL1, 0x9E>;
    };

    struct POWER_CTL2 : umi::mmio::Register<CS43L22, 0x04, 8> {
        struct SPK_A : umi::mmio::Field<POWER_CTL2, 0, 2> {};
        struct SPK_B : umi::mmio::Field<POWER_CTL2, 2, 2> {};
        struct HP_A : umi::mmio::Field<POWER_CTL2, 4, 2> {};
        struct HP_B : umi::mmio::Field<POWER_CTL2, 6, 2> {};
        using HeadphoneOn = umi::mmio::Value<POWER_CTL2, 0xAF>;
        using MuteAll = umi::mmio::Value<POWER_CTL2, 0xFF>;
    };

    struct CLOCKING_CTL : umi::mmio::Register<CS43L22, 0x05, 8> {
        struct AUTO_DETECT : umi::mmio::Field<CLOCKING_CTL, 7, 1> {};
        struct SPEED : umi::mmio::Field<CLOCKING_CTL, 5, 2> {};
        struct RATIO : umi::mmio::Field<CLOCKING_CTL, 1, 2> {};
    };

    struct INTERFACE_CTL1 : umi::mmio::Register<CS43L22, 0x06, 8> {
        struct SLAVE : umi::mmio::Field<INTERFACE_CTL1, 6, 1> {};
        struct SCLK_INV : umi::mmio::Field<INTERFACE_CTL1, 5, 1> {};
        struct DSP_MODE : umi::mmio::Field<INTERFACE_CTL1, 4, 1> {};
        struct DAC_IF : umi::mmio::Field<INTERFACE_CTL1, 2, 2> {};
        struct AWL : umi::mmio::Field<INTERFACE_CTL1, 0, 2> {};
        using I2s16Bit = umi::mmio::Value<INTERFACE_CTL1, 0x04>;
        using I2s24Bit = umi::mmio::Value<INTERFACE_CTL1, 0x06>;
    };

    struct INTERFACE_CTL2 : umi::mmio::Register<CS43L22, 0x07, 8> {};
    struct PASSTHROUGH_A : umi::mmio::Register<CS43L22, 0x08, 8> {};
    struct PASSTHROUGH_B : umi::mmio::Register<CS43L22, 0x09, 8> {};
    struct ANALOG_SET : umi::mmio::Register<CS43L22, 0x0A, 8> {};
    struct PASSTHROUGH_GANG : umi::mmio::Register<CS43L22, 0x0C, 8> {};
    struct PLAYBACK_CTL1 : umi::mmio::Register<CS43L22, 0x0D, 8> {};
    struct MISC_CTL : umi::mmio::Register<CS43L22, 0x0E, 8> {};
    struct PLAYBACK_CTL2 : umi::mmio::Register<CS43L22, 0x0F, 8> {};
    struct PASSTHROUGH_VOL_A : umi::mmio::Register<CS43L22, 0x14, 8> {};
    struct PASSTHROUGH_VOL_B : umi::mmio::Register<CS43L22, 0x15, 8> {};
    struct PCMA_VOL : umi::mmio::Register<CS43L22, 0x1A, 8> {};
    struct PCMB_VOL : umi::mmio::Register<CS43L22, 0x1B, 8> {};
    struct BEEP_FREQ : umi::mmio::Register<CS43L22, 0x1C, 8> {};
    struct BEEP_VOL : umi::mmio::Register<CS43L22, 0x1D, 8> {};
    struct BEEP_CONF : umi::mmio::Register<CS43L22, 0x1E, 8> {};
    struct TONE_CTL : umi::mmio::Register<CS43L22, 0x1F, 8> {};
    struct MASTER_VOL_A : umi::mmio::Register<CS43L22, 0x20, 8> {};
    struct MASTER_VOL_B : umi::mmio::Register<CS43L22, 0x21, 8> {};
    struct HP_VOL_A : umi::mmio::Register<CS43L22, 0x22, 8> {};
    struct HP_VOL_B : umi::mmio::Register<CS43L22, 0x23, 8> {};
    struct SPEAKER_VOL_A : umi::mmio::Register<CS43L22, 0x24, 8> {};
    struct SPEAKER_VOL_B : umi::mmio::Register<CS43L22, 0x25, 8> {};
    struct CH_MIXER : umi::mmio::Register<CS43L22, 0x26, 8> {};
    struct LIMIT_CTL1 : umi::mmio::Register<CS43L22, 0x27, 8> {};
    struct LIMIT_CTL2 : umi::mmio::Register<CS43L22, 0x28, 8> {};
    struct LIMIT_ATTACK : umi::mmio::Register<CS43L22, 0x29, 8> {};
    struct STATUS : umi::mmio::Register<CS43L22, 0x2E, 8, umi::mmio::RO> {};
    struct BATTERY_COMP : umi::mmio::Register<CS43L22, 0x2F, 8> {};
    struct VP_BATTERY : umi::mmio::Register<CS43L22, 0x30, 8, umi::mmio::RO> {};
    struct SPEAKER_STATUS : umi::mmio::Register<CS43L22, 0x31, 8, umi::mmio::RO> {};
    struct CHARGE_PUMP : umi::mmio::Register<CS43L22, 0x34, 8> {};
};

// NOLINTEND(readability-identifier-naming)

} // namespace umi::device
