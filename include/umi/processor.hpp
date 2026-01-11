// SPDX-License-Identifier: MIT
// UMI-OS - Universal Musical Instruments Operating System
// Processor base class and descriptors

#pragma once

#include "types.hpp"
#include "audio_context.hpp"
#include <span>
#include <string_view>
#include <cstdint>

namespace umi {

// ============================================================================
// Port Descriptor
// ============================================================================

/// Port direction
enum class PortDirection : uint8_t {
    In,
    Out,
};

/// Port kind
enum class PortKind : uint8_t {
    Continuous,  ///< Audio, CV - fixed sample rate
    Event,       ///< MIDI, OSC, Parameters - variable timing
};

/// Event type hint for event ports
enum class TypeHint : uint16_t {
    Unknown      = 0x0000,
    
    // MIDI
    MidiBytes    = 0x0100,
    MidiUmp      = 0x0101,
    MidiSysex    = 0x0102,
    
    // Parameters
    ParamChange  = 0x0200,
    ParamGesture = 0x0201,
    
    // Network/Serial
    Osc          = 0x0300,
    Serial       = 0x0301,
    
    // System
    Clock        = 0x0400,
    Transport    = 0x0401,
    
    // User defined (0x8000+)
    UserDefined  = 0x8000,
};

/// Port descriptor
struct PortDescriptor {
    port_id_t id = 0;
    std::string_view name;
    PortKind kind = PortKind::Continuous;
    PortDirection dir = PortDirection::In;
    
    // For Continuous ports
    uint32_t channels = 1;
    
    // For Event ports
    TypeHint type_hint = TypeHint::Unknown;
};

// ============================================================================
// Parameter Descriptor
// ============================================================================

/// Parameter descriptor
struct ParamDescriptor {
    param_id_t id = 0;
    std::string_view name;
    float default_value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    
    /// Normalize value to 0-1 range
    [[nodiscard]] constexpr float normalize(float value) const noexcept {
        if (max_value == min_value) return 0.0f;
        return (value - min_value) / (max_value - min_value);
    }
    
    /// Denormalize from 0-1 range
    [[nodiscard]] constexpr float denormalize(float normalized) const noexcept {
        return min_value + normalized * (max_value - min_value);
    }
    
    /// Clamp value to valid range
    [[nodiscard]] constexpr float clamp(float value) const noexcept {
        if (value < min_value) return min_value;
        if (value > max_value) return max_value;
        return value;
    }
};

// ============================================================================
// Processor Requirements
// ============================================================================

/// Resource requirements for a processor
struct Requirements {
    uint32_t stack_size = 1024;     ///< Stack size in bytes
    uint32_t heap_size = 0;         ///< Heap requirement in bytes
    bool needs_control_thread = false;
};

// ============================================================================
// Processor Base Class
// ============================================================================

/// Base class for audio processors
/// 
/// Lifecycle:
/// 1. Construction (allocate static resources)
/// 2. initialize() - one-time setup, heap allowed
/// 3. prepare() - called when sample rate/buffer size changes
/// 4. process() - called for each audio buffer (real-time safe!)
/// 5. control() - called from control thread (optional)
/// 6. release() - called before reconfiguration
/// 7. terminate() - called before destruction
class Processor {
public:
    virtual ~Processor() = default;
    
    // === Lifecycle ===
    
    /// Called once after construction
    /// Heap allocation allowed here
    virtual void initialize() {}
    
    /// Called when audio stream configuration changes
    /// Minimal heap allowed (sample-rate dependent buffers)
    virtual void prepare(const StreamConfig& config) { (void)config; }
    
    /// Called before reconfiguration or shutdown
    virtual void release() {}
    
    /// Called before destruction
    virtual void terminate() {}
    
    // === Processing ===
    
    /// Audio processing callback (REQUIRED)
    /// Called from audio thread at buffer rate
    /// NO heap allocation, NO blocking, NO system calls!
    virtual void process(AudioContext& ctx) = 0;
    
    /// Control processing callback (optional)
    /// Called from control thread, lower priority
    virtual void control(ControlContext& ctx) { (void)ctx; }
    
    // === Descriptors ===
    
    /// Return parameter descriptors
    virtual std::span<const ParamDescriptor> params() const { return {}; }
    
    /// Return port descriptors
    virtual std::span<const PortDescriptor> ports() const { return {}; }
    
    // === State management ===
    
    /// Save processor state to binary blob
    virtual size_t save_state(std::span<uint8_t> buffer) const { 
        (void)buffer; 
        return 0; 
    }
    
    /// Load processor state from binary blob
    virtual bool load_state(std::span<const uint8_t> data) { 
        (void)data; 
        return true; 
    }
    
    // === Requirements ===
    
    /// Return resource requirements (can be static)
    virtual Requirements requirements() const { return {}; }
};

} // namespace umi
