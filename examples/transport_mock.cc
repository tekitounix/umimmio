// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Comprehensive example covering all public APIs with a RAM-backed mock transport.
///
/// Demonstrates every runtime operation in the umimmio public API:
///   read, write, modify, is, flip, clear, reset, read_variant,
///   RegionValue (get, bits, is), DynamicValue, Numeric, W1C, ErrorPolicy.
///
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cstdio>
#include <cstring>
#include <variant>

#include <umimmio/ops.hh>

// =============================================================================
// Mock transport: RAM-backed register I/O for testing
// =============================================================================

using namespace umi::mmio;

/// @brief A trivial RAM-backed transport suitable for host-side testing.
class MockTransport : private RegOps<> {
  public:
    using RegOps<>::write;
    using RegOps<>::read;
    using RegOps<>::modify;
    using RegOps<>::is;
    using RegOps<>::flip;
    using RegOps<>::clear;
    using RegOps<>::reset;
    using RegOps<>::read_variant;
    using TransportTag = Direct;

    MockTransport() = default;

    template <typename Reg>
    auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        T val{};
        std::memcpy(&val, &ram[Reg::address], sizeof(T));
        return val;
    }

    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;
        std::memcpy(&ram[Reg::address], &value, sizeof(T));
    }

  private:
    mutable std::array<std::uint8_t, 256> ram{};
};

// =============================================================================
// Device register definitions (hierarchical style)
// =============================================================================

struct MockDevice : Device<> {
    /// CTRL: 32-bit control register at offset 0x00 (reset = 0x0000)
    struct CTRL : Register<MockDevice, 0x00, bits32, RW, 0x0000> {
        struct EN : Field<CTRL, 0, 1> {};
        struct MODE : Field<CTRL, 1, 2> {
            using Normal = Value<MODE, 0>;
            using Fast = Value<MODE, 1>;
            using LowPwr = Value<MODE, 2>;
        };
        struct PRESCALER : Field<CTRL, 8, 8, Numeric> {};
    };

    /// SR: 32-bit status register with W1C fields (reset = 0x0000, W1cMask = 0x03)
    struct SR : Register<MockDevice, 0x04, bits32, RW, 0x0000, 0x03> {
        struct OVR : Field<SR, 0, 1, W1C> {};
        struct EOC : Field<SR, 1, 1, W1C> {};
        struct READY : Field<SR, 8, 1> {};
    };
};

// Aliases for readability
using CTRL = MockDevice::CTRL;
using SR = MockDevice::SR;

// =============================================================================
// main — exercises every public API
// =============================================================================

int main() {
    MockTransport const io;
    int errors = 0;

    auto check = [&](bool cond, const char* label) {
        if (!cond) {
            std::printf("FAIL: %s\n", label);
            ++errors;
        }
    };

    // -------------------------------------------------------------------------
    // 1. write() — single and multi-field
    // -------------------------------------------------------------------------
    io.write(CTRL::EN::Set{});
    check(io.is(CTRL::EN::Set{}), "write single field");

    io.write(CTRL::EN::Set{}, CTRL::MODE::Fast{}, CTRL::PRESCALER::value(10U));
    check(io.read(CTRL{}).bits() == (1U | (1U << 1) | (10U << 8)), "write multi-field");

    // -------------------------------------------------------------------------
    // 2. read() — register and field level
    // -------------------------------------------------------------------------
    auto ctrl_val = io.read(CTRL{}); // RegionValue<CTRL>
    check(io.is(CTRL::EN::Set{}), "read field");

    // -------------------------------------------------------------------------
    // 3. RegionValue — get(), bits(), is()
    // -------------------------------------------------------------------------
    check(ctrl_val.is(CTRL::EN::Set{}), "RegionValue.get() via is()");
    check(ctrl_val.is(CTRL::MODE::Fast{}), "RegionValue.is()");

    // -------------------------------------------------------------------------
    // 4. modify() — read-modify-write preserving other fields
    // -------------------------------------------------------------------------
    io.modify(CTRL::MODE::LowPwr{});
    check(io.is(CTRL::EN::Set{}), "modify preserves EN");
    check(io.is(CTRL::MODE::LowPwr{}), "modify changes MODE");

    // -------------------------------------------------------------------------
    // 5. is() — named value comparison
    // -------------------------------------------------------------------------
    check(!io.is(CTRL::MODE::Fast{}), "is() mismatch returns false");
    check(io.is(CTRL::MODE::LowPwr{}), "is() match returns true");

    // -------------------------------------------------------------------------
    // 6. flip() — toggle 1-bit field
    // -------------------------------------------------------------------------
    check(io.is(CTRL::EN::Set{}), "EN is Set before flip");
    io.flip(CTRL::EN{});
    check(io.is(CTRL::EN::Reset{}), "flip toggles EN to Reset");
    io.flip(CTRL::EN{});
    check(io.is(CTRL::EN::Set{}), "flip toggles EN back to Set");

    // -------------------------------------------------------------------------
    // 7. DynamicValue — runtime numeric value via value()
    // -------------------------------------------------------------------------
    auto dv = CTRL::PRESCALER::value(42U);
    io.modify(dv);
    auto prescaler_val = io.read(CTRL::PRESCALER{}).bits();
    check(prescaler_val == 42, "DynamicValue via value()");

    // -------------------------------------------------------------------------
    // 8. reset() — restore register to compile-time reset_value
    // -------------------------------------------------------------------------
    io.reset(CTRL{});
    check(io.read(CTRL{}).bits() == CTRL::reset_value(), "reset() restores reset_value");

    // -------------------------------------------------------------------------
    // 9. W1C: clear() — write-1-to-clear a flag
    // -------------------------------------------------------------------------
    // In real hardware, W1C bits are set by the peripheral (e.g. interrupt flags).
    // Here we demonstrate the API:
    //   - clear(W1cField{}) writes 1 to the W1C bit (clearing it in real HW).
    //   - In a mixed register (W1C + non-W1C fields), clear() uses RMW to
    //     preserve non-W1C field values.
    io.write(SR::READY::Set{}); // set non-W1C field
    io.clear(SR::OVR{});        // clear W1C bit (RMW preserves READY)
    check(io.is(SR::READY::Set{}), "clear() preserves non-W1C fields");

    // -------------------------------------------------------------------------
    // 10. read_variant() — pattern match field value to std::variant
    // -------------------------------------------------------------------------
    io.write(CTRL::EN::Set{}, CTRL::MODE::Fast{});

    auto mode = io.read_variant<CTRL::MODE, CTRL::MODE::Normal, CTRL::MODE::Fast, CTRL::MODE::LowPwr>();

    bool matched_fast = false;
    std::visit(
        [&](auto v) {
            if constexpr (std::is_same_v<decltype(v), CTRL::MODE::Fast>) {
                matched_fast = true;
            }
        },
        mode);
    check(matched_fast, "read_variant matches Fast");

    // read_variant with unknown value → UnknownValue
    io.modify(CTRL::MODE::LowPwr{});
    auto mode2 = io.read_variant<CTRL::MODE, CTRL::MODE::Normal,
                                 CTRL::MODE::Fast>(); // LowPwr not listed

    bool is_unknown = false;
    std::visit(
        [&](auto v) {
            if constexpr (std::is_same_v<decltype(v), UnknownValue<CTRL::MODE>>) {
                is_unknown = true;
            }
        },
        mode2);
    check(is_unknown, "read_variant returns UnknownValue for unlisted value");

    // -------------------------------------------------------------------------
    // Summary
    // -------------------------------------------------------------------------
    if (errors == 0) {
        std::printf("All checks passed.\n");
    } else {
        std::printf("%d check(s) FAILED.\n", errors);
    }
    return errors;
}
