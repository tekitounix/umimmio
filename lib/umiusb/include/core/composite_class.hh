// SPDX-License-Identifier: MIT
// UMI-USB: Composite Class — combines multiple USB Classes into one
// Satisfies the Class concept by dispatching to sub-classes
#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include "core/types.hh"

namespace umiusb {

/// Composite USB Class combining two sub-classes (e.g., Audio + MIDI).
/// Dispatches Class concept methods to the appropriate sub-class
/// based on endpoint numbers and interface numbers.
///
/// Satisfies the Class concept.
template<typename ClassA, typename ClassB>
class CompositeClass {
public:
    static constexpr bool USES_IAD = true;

    CompositeClass(ClassA& a, ClassB& b) : a_(a), b_(b) {}

    // --- Class concept required methods ---

    [[nodiscard]] std::span<const uint8_t> config_descriptor() const {
        // Return class A's descriptor (primary)
        // In a full implementation, this would merge both descriptors
        return a_.config_descriptor();
    }

    [[nodiscard]] std::span<const uint8_t> bos_descriptor() const {
        // Delegate to class A (typically has BOS for WinUSB/WebUSB)
        auto bos = a_.bos_descriptor();
        if (!bos.empty()) return bos;
        return b_.bos_descriptor();
    }

    bool handle_vendor_request(const SetupPacket& setup, std::span<uint8_t>& response) {
        if (a_.handle_vendor_request(setup, response)) return true;
        return b_.handle_vendor_request(setup, response);
    }

    void on_configured(bool configured) {
        a_.on_configured(configured);
        b_.on_configured(configured);
    }

    bool handle_request(const SetupPacket& setup, std::span<uint8_t>& response) {
        // Try class A first, then B
        if (a_.handle_request(setup, response)) return true;
        return b_.handle_request(setup, response);
    }

    void on_rx(uint8_t ep, std::span<const uint8_t> data) {
        // Dispatch by endpoint — each class knows its own endpoints
        a_.on_rx(ep, data);
        b_.on_rx(ep, data);
    }

    // --- Optional Class concept methods ---

    template<typename HalT>
    void configure_endpoints(HalT& hal) {
        a_.configure_endpoints(hal);
        b_.configure_endpoints(hal);
    }

    template<typename HalT>
    void set_interface(HalT& hal, uint8_t interface, uint8_t alt_setting) {
        if constexpr (requires { a_.set_interface(hal, interface, alt_setting); }) {
            a_.set_interface(hal, interface, alt_setting);
        }
        if constexpr (requires { b_.set_interface(hal, interface, alt_setting); }) {
            b_.set_interface(hal, interface, alt_setting);
        }
    }

    void on_ep0_rx(std::span<const uint8_t> data) {
        if constexpr (requires { a_.on_ep0_rx(data); }) {
            a_.on_ep0_rx(data);
        }
        if constexpr (requires { b_.on_ep0_rx(data); }) {
            b_.on_ep0_rx(data);
        }
    }

    template<typename HalT>
    void on_sof(HalT& hal) {
        a_.on_sof(hal);
        b_.on_sof(hal);
    }

    template<typename HalT>
    void on_tx_complete(HalT& hal, uint8_t ep) {
        a_.on_tx_complete(hal, ep);
        b_.on_tx_complete(hal, ep);
    }

    // --- Sub-class access ---

    ClassA& class_a() { return a_; }
    ClassB& class_b() { return b_; }
    const ClassA& class_a() const { return a_; }
    const ClassB& class_b() const { return b_; }

private:
    ClassA& a_;
    ClassB& b_;
};

}  // namespace umiusb
