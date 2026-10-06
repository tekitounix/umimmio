#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief I2C transport with runtime driver reference.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

#include "../ops.hh"
#include "../policy.hh" // IWYU pragma: keep  — AssertOnError, I2c, max_reg_bytes
#include "detail.hh"

namespace umi::mmio {

/// @brief I2C transport with runtime driver reference.
///
/// @tparam I2C         I2C driver type providing write() and write_read().
/// @tparam CheckPolicy Enable runtime range checks.
/// @tparam ErrorPolicy Error handler for range violations.
/// @tparam AddressWidth Register address width on the bus (uint8_t or uint16_t).
/// @tparam AddrEndian  Byte order for register addresses on the wire.
/// @tparam DataEndian  Byte order for data on the wire.
template <typename I2C,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          typename AddressWidth = std::uint8_t,
          std::endian AddrEndian = std::endian::big,
          std::endian DataEndian = std::endian::little>
class I2cTransport : public ByteAdapter<CheckPolicy, ErrorPolicy, AddressWidth, DataEndian> {
    I2C& i2c;
    std::uint8_t device_addr;

  public:
    using TransportTag = I2c;

    /// @brief Construct an I2C transport.
    /// @param bus  Reference to I2C driver. Must outlive this transport.
    /// @param addr Device address (7-bit, left-shifted by 1).
    /// @note The transport stores a reference; the bus must outlive the
    /// transport. The `[[clang::lifetimebound]]` attribute lets Clang detect
    /// constructing this transport from a temporary or short-lived bus and
    /// returning it (the bound reference would dangle). GCC 14.2 ignores the
    /// attribute, so the check is Clang-only; for static-singleton drivers
    /// the contract is satisfied structurally.
    I2cTransport(I2C& bus UMI_LIFETIMEBOUND, std::uint8_t addr) noexcept : i2c(bus), device_addr(addr) {}

    /// @brief Write register data over I2C.
    /// @note If the HAL driver's write() returns a type convertible to bool,
    ///       a failure triggers ErrorPolicy::on_transport_error().
    void raw_write(AddressWidth reg_addr, const void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressWidth);

        std::array<std::uint8_t, addr_size + max_reg_bytes> buf{};
        detail::encode_address<AddrEndian>(reg_addr, buf.data());
        std::memcpy(&buf[addr_size], data, size);

        auto span = std::span<const std::uint8_t>{buf.data(), addr_size + size};
        if constexpr (std::is_void_v<decltype(i2c.write(device_addr, span))>) {
            i2c.write(device_addr, span);
        } else {
            if (!i2c.write(device_addr, span)) {
                ErrorPolicy::on_transport_error("I2C write failed");
            }
        }
    }

    /// @brief Read-modify-write is **deleted** for I2C transports (compile error).
    /// @details I2C read-modify-write is unreliable on real hardware: the
    ///   register read can return stale RX FIFO data, NACK silently under
    ///   the `IgnoreError` policy, race with codec-internal state machines
    ///   that hold the bus, or trip the codec's clock-stretching watchdog.
    ///   Always use `transport.write(Field1, Field2, ...)` with the full
    ///   intended register state instead — the driver is expected to keep
    ///   the canonical state (e.g. a codec driver whose methods rewrite
    ///   complete registers rather than patching individual fields).
    /// @note `= delete` (not `[[deprecated]]`) because `[[deprecated]]` only
    ///   emits a warning and lets unsafe code slip through. Deleting the
    ///   overload turns any I²C read-modify-write attempt into a hard
    ///   compile error, which is the only type-safe enforcement that
    ///   prevents hardware-level damage from silent NACK / stale-RX
    ///   bit-flips.  umimmio's own modify() tests must exercise modify()
    ///   on a non-I²C transport (DirectTransport mock or a dedicated
    ///   atomic-RMW transport) — see tests/.
    template <typename... Values>
    void modify(Values&&...) const = delete;

    /// @brief Read register data over I2C.
    /// @note If the HAL driver's write_read() returns a type convertible to bool,
    ///       a failure triggers ErrorPolicy::on_transport_error().
    void raw_read(AddressWidth reg_addr, void* data, std::size_t size) const noexcept {
        constexpr std::size_t addr_size = sizeof(AddressWidth);

        std::array<std::uint8_t, addr_size> addr_buf{};
        detail::encode_address<AddrEndian>(reg_addr, addr_buf.data());

        auto tx = std::span<const std::uint8_t>{addr_buf.data(), addr_size};
        auto rx = std::span<std::uint8_t>{static_cast<std::uint8_t*>(data), size};
        if constexpr (std::is_void_v<decltype(i2c.write_read(device_addr, tx, rx))>) {
            i2c.write_read(device_addr, tx, rx);
        } else {
            if (!i2c.write_read(device_addr, tx, rx)) {
                ErrorPolicy::on_transport_error("I2C read failed");
            }
        }
    }
};

} // namespace umi::mmio
