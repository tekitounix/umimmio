// SPDX-License-Identifier: MIT
/// @file
/// @brief Peripheral-style register map definition example.

#include <cstdio>
#include <umimmio/register.hh>

// =============================================================================
// Example: SPI peripheral register map (STM32-style)
// =============================================================================

namespace spi {

// Base address
constexpr std::uint32_t base = 0x4001'3000;

// Registers
using CR1 = umi::mmio::Region<base + 0x00, std::uint32_t>;
using CR2 = umi::mmio::Region<base + 0x04, std::uint32_t>;
using SR = umi::mmio::Region<base + 0x08, std::uint32_t>;
using DR = umi::mmio::Region<base + 0x0C, std::uint16_t>;

// CR1 fields
using CPHA = umi::mmio::Field<CR1, 0, 1>; // Clock phase
using CPOL = umi::mmio::Field<CR1, 1, 1>; // Clock polarity
using MSTR = umi::mmio::Field<CR1, 2, 1>; // Master selection
using BR = umi::mmio::Field<CR1, 3, 3>;   // Baud rate prescaler
using SPE = umi::mmio::Field<CR1, 6, 1>;  // SPI enable
using DFF = umi::mmio::Field<CR1, 11, 1>; // Data frame format

// SR fields
using RXNE = umi::mmio::Field<SR, 0, 1>; // Receive buffer not empty
using TXE = umi::mmio::Field<SR, 1, 1>;  // Transmit buffer empty
using BSY = umi::mmio::Field<SR, 7, 1>;  // Busy flag

// Named values
using BR_Div2 = umi::mmio::Value<BR, 0>;
using BR_Div4 = umi::mmio::Value<BR, 1>;
using BR_Div8 = umi::mmio::Value<BR, 2>;
using BR_Div256 = umi::mmio::Value<BR, 7>;

} // namespace spi

int main() {
    // All compile-time verifiable
    static_assert(spi::CR1::address == 0x4001'3000);
    static_assert(spi::SR::address == 0x4001'3008);
    static_assert(spi::DR::address == 0x4001'300C);

    static_assert(spi::BR::mask == 0x38);  // bits 3-5
    static_assert(spi::SPE::mask == 0x40); // bit 6
    static_assert(spi::BR_Div256::shifted_value == (7u << 3));

    std::printf("SPI CR1: 0x%08X\n", spi::CR1::address);
    std::printf("BR mask: 0x%08X\n", spi::BR::mask);
    std::printf("BR_Div256 shifted: 0x%08X\n", spi::BR_Div256::shifted_value);

    return 0;
}
