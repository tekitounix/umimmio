// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Peripheral-style register map definition example (hierarchical style).
///
/// Shows how to model a realistic SPI peripheral using nested structs.
/// The hierarchy SpiDevice > CR1 > BR > BrDiv256 directly represents
/// the device/register/field/value relationship found in datasheets.
///
/// @author Shota Moriguchi @tekitounix

#include <cstdio>

#include <umimmio/region.hh>

// =============================================================================
// Example: SPI peripheral register map (STM32-style, hierarchical)
// =============================================================================

namespace spi {

using namespace umi::mmio;

/// SPI device at base address 0x4001'3000.
/// Registers, fields, and named values are nested inside the device.
struct SpiDevice : Device<RW, DirectTransportTag> {
    static constexpr Addr base_address = 0x4001'3000;

    /// CR1: control register 1
    struct CR1 : Register<SpiDevice, 0x00, bits32> {
        struct CPHA : Field<CR1, 0, 1> {};  // Clock phase
        struct CPOL : Field<CR1, 1, 1> {};  // Clock polarity
        struct MSTR : Field<CR1, 2, 1> {};  // Master selection
        struct BR : Field<CR1, 3, 3> {      // Baud rate prescaler
            using Div2   = Value<BR, 0>;
            using Div4   = Value<BR, 1>;
            using Div8   = Value<BR, 2>;
            using Div256 = Value<BR, 7>;
        };
        struct SPE : Field<CR1, 6, 1> {};   // SPI enable
        struct DFF : Field<CR1, 11, 1> {};  // Data frame format
    };

    /// CR2: control register 2
    struct CR2 : Register<SpiDevice, 0x04, bits32> {};

    /// SR: status register (read-only)
    struct SR : Register<SpiDevice, 0x08, bits32, RO> {
        struct RXNE : Field<SR, 0, 1> {};  // Receive buffer not empty
        struct TXE : Field<SR, 1, 1> {};   // Transmit buffer empty
        struct BSY : Field<SR, 7, 1> {};   // Busy flag
    };

    /// DR: data register (16-bit)
    struct DR : Register<SpiDevice, 0x0C, bits16> {};
};

} // namespace spi

int main() {
    using Spi = spi::SpiDevice;

    // All compile-time verifiable
    static_assert(Spi::CR1::address == 0x4001'3000);
    static_assert(Spi::SR::address == 0x4001'3008);
    static_assert(Spi::DR::address == 0x4001'300C);

    static_assert(Spi::CR1::BR::mask() == 0x38);   // bits 3-5
    static_assert(Spi::CR1::SPE::mask() == 0x40);   // bit 6
    static_assert(Spi::CR1::BR::Div256::shifted_value == (7U << 3));

    std::printf("SPI CR1:         0x%08lX\n", static_cast<unsigned long>(Spi::CR1::address));
    std::printf("BR mask:         0x%08X\n", Spi::CR1::BR::mask());
    std::printf("BR::Div256 shifted: 0x%08X\n", Spi::CR1::BR::Div256::shifted_value);

    return 0;
}
