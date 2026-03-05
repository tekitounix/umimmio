// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Peripheral-style register map definition example.
/// @author Shota Moriguchi @tekitounix

#include <cstdio>

#include <umimmio/region.hh>

// =============================================================================
// Example: SPI peripheral register map (STM32-style)
// =============================================================================

namespace spi {

using namespace umi::mmio;

// Device at base address 0x4001'3000
struct SpiDevice : Device<RW, DirectTransportTag> {
    static constexpr Addr base_address = 0x4001'3000;
};

// Registers
struct CR1 : Register<SpiDevice, 0x00, bits32> {};
struct CR2 : Register<SpiDevice, 0x04, bits32> {};
struct SR : Register<SpiDevice, 0x08, bits32, RO> {};
struct DR : Register<SpiDevice, 0x0C, bits16> {};

// CR1 fields
struct CPHA : Field<CR1, 0, 1> {}; // Clock phase
struct CPOL : Field<CR1, 1, 1> {}; // Clock polarity
struct MSTR : Field<CR1, 2, 1> {}; // Master selection
struct BR : Field<CR1, 3, 3> {};   // Baud rate prescaler
struct SPE : Field<CR1, 6, 1> {};  // SPI enable
struct DFF : Field<CR1, 11, 1> {}; // Data frame format

// SR fields
struct RXNE : Field<SR, 0, 1> {}; // Receive buffer not empty
struct TXE : Field<SR, 1, 1> {};  // Transmit buffer empty
struct BSY : Field<SR, 7, 1> {};  // Busy flag

// Named values
using BrDiv2 = Value<BR, 0>;
using BrDiv4 = Value<BR, 1>;
using BrDiv8 = Value<BR, 2>;
using BrDiv256 = Value<BR, 7>;

} // namespace spi

int main() {
    // All compile-time verifiable
    static_assert(spi::CR1::address == 0x4001'3000);
    static_assert(spi::SR::address == 0x4001'3008);
    static_assert(spi::DR::address == 0x4001'300C);

    static_assert(spi::BR::mask() == 0x38);  // bits 3-5
    static_assert(spi::SPE::mask() == 0x40); // bit 6
    static_assert(spi::BrDiv256::shifted_value == (7U << 3));

    std::printf("SPI CR1: 0x%08lX\n", static_cast<unsigned long>(spi::CR1::address));
    std::printf("BR mask: 0x%08X\n", spi::BR::mask());
    std::printf("BrDiv256 shifted: 0x%08X\n", spi::BrDiv256::shifted_value);

    return 0;
}
