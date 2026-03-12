// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief MockTransport and shared device/register definitions for umimmio tests.
/// @author Shota Moriguchi @tekitounix
/// @details Provides MockTransport and device/register definitions that simulate
///          a real hardware register map for host-based testing.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include <umimmio/ops.hh>  // IWYU pragma: export
#include <umitest/test.hh> // IWYU pragma: export

namespace umimmio::test {

// =============================================================================
// MockTransport — simulates direct memory mapped I/O with a RAM buffer
// =============================================================================

using namespace umi::mmio;

/// @brief In-memory mock transport for testing.
///
/// Provides the same interface as DirectTransport (reg_read/reg_write).
/// No CRTP parameter needed (deducing this resolves RegOps automatically).
///
/// @note Named clear_memory() instead of reset() to avoid collision with
/// RegOps::reset(Reg). clear_memory() explicitly means "zero all mock RAM".
struct MockTransport : private RegOps<> {
  public:
    using RegOps<>::read;
    using RegOps<>::write;
    using RegOps<>::modify;
    using RegOps<>::is;
    using RegOps<>::flip;
    using RegOps<>::clear;
    using RegOps<>::reset;
    using RegOps<>::read_variant;

    using TransportTag = Direct;

    mutable std::array<std::uint8_t, 256> memory{};

    /// @brief Clear all mock memory to zero.
    void clear_memory() noexcept { memory.fill(0); }

    /// @brief Read register value from mock memory.
    template <typename Reg>
    auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        typename Reg::RegValueType val{};
        std::memcpy(&val, &memory[Reg::address], sizeof(val));
        return val;
    }

    /// @brief Write register value to mock memory.
    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType val) const noexcept {
        std::memcpy(&memory[Reg::address], &val, sizeof(val));
    }

    /// @brief Peek raw memory at arbitrary address (test helper).
    template <typename T>
    T peek(Addr addr) const noexcept {
        T val{};
        std::memcpy(&val, &memory[static_cast<std::size_t>(addr)], sizeof(val));
        return val;
    }

    /// @brief Poke raw memory at arbitrary address (test helper).
    template <typename T>
    void poke(Addr addr, T val) noexcept {
        std::memcpy(&memory[static_cast<std::size_t>(addr)], &val, sizeof(val));
    }
};

// =============================================================================
// Mock device register map — simulates a realistic peripheral
// =============================================================================

/// @brief Mock device with direct transport only
struct MockDevice : Device<> {};

// --- Registers at various offsets ---

/// @brief Status register (32-bit, read-only, reset = 0x0001)
struct StatusReg : Register<MockDevice, 0x00, bits32, RO, 0x0001> {};

/// @brief Config register (32-bit, read-write, reset = 0xFF00)
struct ConfigReg : Register<MockDevice, 0x04, bits32, RW, 0xFF00> {};

/// @brief Data register (32-bit, read-write, reset = 0)
struct DataReg : Register<MockDevice, 0x08, bits32, RW, 0> {};

/// @brief Control register (16-bit, read-write, reset = 0)
struct CtrlReg : Register<MockDevice, 0x0C, bits16, RW, 0> {};

/// @brief Write-only command register
struct CmdReg : Register<MockDevice, 0x10, bits32, WO, 0> {};

/// @brief 8-bit register (read-write, reset = 0xA5)
struct ByteReg : Register<MockDevice, 0x18, bits8, RW, 0xA5> {};

/// @brief 64-bit register (read-write, reset = 0)
struct Reg64Direct : Register<MockDevice, 0x28, bits64, RW, 0> {};
struct Field64Low32 : Field<Reg64Direct, 0, 32, Numeric> {};
struct Field64High32 : Field<Reg64Direct, 32, 32, Numeric> {};

/// @brief Low nibble field (bits 0-3)
struct ByteLow : Field<ByteReg, 0, 4, Numeric> {};

/// @brief High nibble field (bits 4-7)
struct ByteHigh : Field<ByteReg, 4, 4, Numeric> {};

/// @brief Status register with W1C fields (32-bit, read-write, reset = 0)
struct W1cStatusReg : Register<MockDevice, 0x14, bits32, RW, 0, /*W1cMask=*/0x03> {};

/// @brief W1C overflow flag (bit 0)
struct W1cOvr : Field<W1cStatusReg, 0, 1, W1C> {};

/// @brief W1C end-of-conversion flag (bit 1)
struct W1cEoc : Field<W1cStatusReg, 1, 1, W1C> {};

/// @brief Non-W1C enable flag in same register (bit 8)
struct W1cRegEnable : Field<W1cStatusReg, 8, 1> {};

/// @brief Non-W1C mode flag in same register (bit 9)
struct W1cRegMode : Field<W1cStatusReg, 9, 1> {};

/// @brief All-W1C register — every bit is W1C (no non-W1C fields)
struct AllW1cReg : Register<MockDevice, 0x1C, bits16, RW, 0, /*W1cMask=*/0xFFFF> {};
struct AllW1cFlag0 : Field<AllW1cReg, 0, 1, W1C> {};
struct AllW1cFlag1 : Field<AllW1cReg, 1, 1, W1C> {};
struct AllW1cFlag2 : Field<AllW1cReg, 2, 1, W1C> {};

/// @brief 16-bit RW register with W1C fields — tests W1C on non-32-bit widths
struct Ctrl16W1cReg : Register<MockDevice, 0x20, bits16, RW, 0, /*W1cMask=*/0x03> {};
struct Ctrl16W1cBit0 : Field<Ctrl16W1cReg, 0, 1, W1C> {};
struct Ctrl16W1cBit1 : Field<Ctrl16W1cReg, 1, 1, W1C> {};
struct Ctrl16Enable : Field<Ctrl16W1cReg, 8, 1> {};
struct Ctrl16Mode : Field<Ctrl16W1cReg, 4, 2, Numeric> {};

// --- Fields within ConfigReg ---

/// @brief Enable bit (bit 0, 1-bit)
struct ConfigEnable : Field<ConfigReg, 0, 1> {};

/// @brief Mode field (bits 1-2, 2-bit)
struct ConfigMode : Field<ConfigReg, 1, 2> {};

/// @brief Prescaler field (bits 8-15, 8-bit, Numeric — raw value() enabled)
struct ConfigPrescaler : Field<ConfigReg, 8, 8, Numeric> {};

// --- Fields within CtrlReg ---

/// @brief Start bit (bit 0, 1-bit)
struct CtrlStart : Field<CtrlReg, 0, 1> {};

/// @brief IRQ enable bit (bit 1, 1-bit) — with domain-specific aliases
struct CtrlIrqEn : Field<CtrlReg, 1, 1> {
    using Enabled = Value<CtrlIrqEn, 1>;
    using Disabled = Value<CtrlIrqEn, 0>;
};

/// @brief Channel select (bits 4-7, 4-bit, Numeric — raw value() enabled)
struct CtrlChannel : Field<CtrlReg, 4, 4, Numeric> {};

// --- Enum values for Mode ---

enum class ModeVal : std::uint8_t { NORMAL = 0, FAST = 1, LOW_POWER = 2, TEST = 3 };

using ModeNormal = Value<ConfigMode, static_cast<std::uint8_t>(ModeVal::NORMAL)>;
using ModeFast = Value<ConfigMode, static_cast<std::uint8_t>(ModeVal::FAST)>;
using ModeLowPower = Value<ConfigMode, static_cast<std::uint8_t>(ModeVal::LOW_POWER)>;
using ModeTest = Value<ConfigMode, static_cast<std::uint8_t>(ModeVal::TEST)>;

} // namespace umimmio::test
