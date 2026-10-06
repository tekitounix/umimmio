#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Paged I2C transport for register files with a page-select register.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

#include "../ops.hh"
#include "../policy.hh" // IWYU pragma: keep  — AssertOnError, I2c, max_reg_bytes

namespace umi::mmio {

/// @brief I2C transport for paged register files (page-select register at a
/// fixed in-page address, present identically on every page).
///
/// @details Many audio codecs and multi-function I2C peripherals expose more
/// registers than an 8-bit address can reach, and organize them as pages
/// selected by writing the target page number to a fixed page-select
/// register. This transport models the logical register address as 16
/// bits — `(page << 8) | in_page_offset` — while the wire protocol stays
/// 8-bit: an optional 1-byte page-select write, followed by a single-byte
/// in-page register address plus payload.
///
/// The transport caches the last successfully selected page and only emits
/// the page-select write when the cache is invalid or the target page
/// differs from it. The cache starts **invalid**: the chip's actual page at
/// construction time is unknown (the chip may not have been reset, or an
/// external agent may have changed the page), so the first access always
/// emits a page-select write regardless of the target page. The cache is
/// advanced only when the page-select write succeeds, so a failed select
/// under a non-trapping ErrorPolicy (e.g. LatchError) never poisons the
/// cache — the next access retries the select instead of silently talking
/// to the wrong page.
///
/// @tparam I2C              I2C driver type providing write() and write_read().
/// @tparam CheckPolicy      Enable runtime range checks.
/// @tparam ErrorPolicy      Error handler for range/transport violations.
/// @tparam page_select_addr In-page register address of the page-select
///                          register, identical on every page.
template <typename I2C,
          typename CheckPolicy = std::true_type,
          typename ErrorPolicy = AssertOnError,
          std::uint8_t page_select_addr = 0x00>
class PagedI2cTransport : public ByteAdapter<CheckPolicy, ErrorPolicy, std::uint16_t, std::endian::little> {
    I2C& i2c;
    std::uint8_t device_addr;
    mutable std::uint8_t current_page = 0;
    mutable bool page_cache_valid = false;

  public:
    using TransportTag = I2c;

    /// @brief Construct a paged I2C transport.
    /// @param bus  Reference to I2C driver. Must outlive this transport.
    /// @param addr Device address (7-bit, left-shifted by 1).
    /// @note The transport stores a reference; the bus must outlive the
    /// transport. See I2cTransport (transport/i2c.hh) for the full rationale.
    /// @note The page cache starts invalid — the chip's current page is
    /// unknown at construction time — so the first access always emits a
    /// page-select write. Call invalidate_page_cache() after any chip soft
    /// reset or any operation that may change the page externally; see its
    /// doc comment for the contract.
    PagedI2cTransport(I2C& bus UMI_LIFETIMEBOUND, std::uint8_t addr) noexcept : i2c(bus), device_addr(addr) {}

    /// @brief Invalidate the cached page so the next access always emits a
    /// page-select write.
    /// @details Drivers MUST call this after any chip soft reset, and after
    /// any operation that may change the chip's page selection outside this
    /// transport (another bus master, a vendor init blob, a raw HAL write).
    /// After invalidation the next access emits a page-select write
    /// regardless of the target page — no assumption is made about the
    /// chip's post-reset page, so a failed or partial reset cannot leave the
    /// cache claiming a page the chip is not actually on.
    void invalidate_page_cache() const noexcept { page_cache_valid = false; }

    /// @brief Write register data over paged I2C.
    /// @note Emits a page-select write first if the page cache is invalid or
    ///       the target page differs from the cached page. If the
    ///       page-select write fails, the register write is not attempted.
    /// @note If the HAL driver's write() returns a type convertible to bool,
    ///       a failure triggers ErrorPolicy::on_transport_error().
    void raw_write(std::uint16_t reg_addr, const void* data, std::size_t size) const noexcept {
        if (size > max_reg_bytes) {
            ErrorPolicy::on_range_error("Paged I2C write size exceeds max_reg_bytes");
            return;
        }
        if (!select_page(reg_addr)) {
            return;
        }

        std::array<std::uint8_t, 1 + max_reg_bytes> buf{};
        buf[0] = in_page_offset(reg_addr);
        std::memcpy(&buf[1], data, size);

        auto span = std::span<const std::uint8_t>{buf.data(), 1 + size};
        if constexpr (std::is_void_v<decltype(i2c.write(device_addr, span))>) {
            i2c.write(device_addr, span);
        } else {
            if (!i2c.write(device_addr, span)) {
                ErrorPolicy::on_transport_error("Paged I2C write failed");
            }
        }
    }

    /// @brief Read-modify-write is **deleted** for paged I2C transports (compile error).
    /// @details Same rationale as I2cTransport::modify() (transport/i2c.hh):
    ///   I2C read-modify-write is unreliable on real hardware (stale RX FIFO
    ///   data, silent NACK, races with codec-internal state machines, clock-
    ///   stretch watchdogs) and paging adds a second hazard — a concurrent
    ///   page change between the read and the write would corrupt the wrong
    ///   page. Always use write(Field1, Field2, ...) with the full intended
    ///   register state instead.
    template <typename... Values>
    void modify(Values&&...) const = delete;

    /// @brief Read register data over paged I2C.
    /// @note Emits a page-select write first if the page cache is invalid or
    ///       the target page differs from the cached page.
    /// @note If the page-select write or the read fails, @p data is
    ///       zero-filled before ErrorPolicy::on_transport_error() fires, so
    ///       a non-trapping policy (e.g. LatchError) never surfaces
    ///       uninitialized bytes as a typed register value.
    void raw_read(std::uint16_t reg_addr, void* data, std::size_t size) const noexcept {
        if (!select_page(reg_addr)) {
            std::memset(data, 0, size);
            return;
        }

        std::array<std::uint8_t, 1> addr_buf{in_page_offset(reg_addr)};
        auto tx = std::span<const std::uint8_t>{addr_buf.data(), 1};
        auto rx = std::span<std::uint8_t>{static_cast<std::uint8_t*>(data), size};
        if constexpr (std::is_void_v<decltype(i2c.write_read(device_addr, tx, rx))>) {
            i2c.write_read(device_addr, tx, rx);
        } else {
            if (!i2c.write_read(device_addr, tx, rx)) {
                std::memset(data, 0, size);
                ErrorPolicy::on_transport_error("Paged I2C read failed");
            }
        }
    }

  private:
    /// @brief Extract the page number from a 16-bit logical address.
    static constexpr std::uint8_t page_of(std::uint16_t reg_addr) noexcept {
        return static_cast<std::uint8_t>(reg_addr >> 8);
    }

    /// @brief Extract the in-page register offset from a 16-bit logical address.
    static constexpr std::uint8_t in_page_offset(std::uint16_t reg_addr) noexcept {
        return static_cast<std::uint8_t>(reg_addr & 0xFFU);
    }

    /// @brief Emit a page-select write if the cache is invalid or the target
    /// page differs from the cached page.
    /// @return true if the target page is selected (cache hit, or the
    /// page-select write succeeded); false if the page-select write failed.
    /// The cache is advanced only on success, so a failed select is retried
    /// on the next access instead of leaving the cache pointing at a page
    /// the chip never switched to.
    [[nodiscard]] bool select_page(std::uint16_t reg_addr) const noexcept {
        auto const page = page_of(reg_addr);
        if (page_cache_valid && page == current_page) {
            return true;
        }

        std::array<std::uint8_t, 2> buf{page_select_addr, page};
        auto span = std::span<const std::uint8_t>{buf.data(), 2};
        if constexpr (std::is_void_v<decltype(i2c.write(device_addr, span))>) {
            i2c.write(device_addr, span);
        } else {
            if (!i2c.write(device_addr, span)) {
                ErrorPolicy::on_transport_error("Paged I2C page-select write failed");
                return false;
            }
        }
        current_page = page;
        page_cache_valid = true;
        return true;
    }
};

} // namespace umi::mmio
