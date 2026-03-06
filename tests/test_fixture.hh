// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Shared test definitions for umimmio tests.
/// @author Shota Moriguchi @tekitounix
/// @details Provides MockTransport and device/register definitions that simulate
///          a real hardware register map for host-based testing.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include <umimmio/ops.hh>
#include <umitest/test.hh>

namespace umimmio::test {

// =============================================================================
// MockTransport — simulates direct memory mapped I/O with a RAM buffer
// =============================================================================

using namespace umi::mmio;

/// @brief In-memory mock transport for testing.
///
/// DirectTransport と同じインターフェース (reg_read/reg_write) を提供する。
/// CRTP パラメータは不要 (deducing this で RegOps が自動解決)。
///
/// @note clear_memory() という名前を使う理由:
/// RegOps が reset(Reg) メソッドを持つため、reset() では名前衝突する。
/// clear_memory() はモック固有の「全メモリゼロクリア」を明示する。
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

    std::array<std::uint8_t, 256> mutable memory{};

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

/// @brief IRQ enable bit (bit 1, 1-bit)
struct CtrlIrqEn : Field<CtrlReg, 1, 1> {};

/// @brief Channel select (bits 4-7, 4-bit, Numeric — raw value() enabled)
struct CtrlChannel : Field<CtrlReg, 4, 4, Numeric> {};

// --- Enum values for Mode ---

enum class ModeVal : uint8_t { NORMAL = 0, FAST = 1, LOW_POWER = 2, TEST = 3 };

using ModeNormal = Value<ConfigMode, static_cast<uint8_t>(ModeVal::NORMAL)>;
using ModeFast = Value<ConfigMode, static_cast<uint8_t>(ModeVal::FAST)>;
using ModeLowPower = Value<ConfigMode, static_cast<uint8_t>(ModeVal::LOW_POWER)>;
using ModeTest = Value<ConfigMode, static_cast<uint8_t>(ModeVal::TEST)>;

// =============================================================================
// Test registration functions
// =============================================================================

void run_register_field_tests(umi::test::Suite& suite);
void run_transport_tests(umi::test::Suite& suite);
void run_access_policy_tests(umi::test::Suite& suite);
void run_spi_bitbang_tests(umi::test::Suite& suite);
void run_protected_tests(umi::test::Suite& suite);

} // namespace umimmio::test
