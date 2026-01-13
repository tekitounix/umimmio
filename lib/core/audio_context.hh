// SPDX-License-Identifier: MIT
// UMI-OS - Universal Musical Instruments Operating System
// Audio Context for process() callback

#pragma once

#include "types.hh"
#include "event.hh"
#include "error.hh"
#include <span>
#include <cstdint>
#include <algorithm>

namespace umi {

/// Stream configuration
struct StreamConfig {
    uint32_t sample_rate = 48000;   ///< Sample rate in Hz
    uint32_t buffer_size = 256;     ///< Buffer size in samples
};

/// Audio context passed to Processor::process()
/// Contains all information needed for sample-accurate audio processing
struct AudioContext {
    /// Input audio buffers (one pointer per channel)
    std::span<const sample_t* const> inputs;
    
    /// Output audio buffers (one pointer per channel)
    std::span<sample_t* const> outputs;
    
    /// Event queue for sample-accurate MIDI/param events
    EventQueue<>& events;
    
    /// Current sample rate in Hz
    uint32_t sample_rate;
    
    /// Buffer size in samples
    uint32_t buffer_size;
    
    /// Delta time per sample (1.0 / sample_rate)
    /// Use this for time-dependent DSP calculations
    float dt;
    
    /// Absolute sample position in the stream
    /// Increments by buffer_size each process() call
    sample_position_t sample_position;
    
    // === Convenience accessors ===
    
    /// Number of input channels
    [[nodiscard]] size_t num_inputs() const noexcept { return inputs.size(); }
    
    /// Number of output channels
    [[nodiscard]] size_t num_outputs() const noexcept { return outputs.size(); }
    
    /// Get input channel buffer (nullptr if out of range)
    [[nodiscard]] const sample_t* input(size_t ch) const noexcept {
        return ch < inputs.size() ? inputs[ch] : nullptr;
    }
    
    /// Get output channel buffer (nullptr if out of range)
    [[nodiscard]] sample_t* output(size_t ch) const noexcept {
        return ch < outputs.size() ? outputs[ch] : nullptr;
    }

    /// Get input channel buffer with error handling
    [[nodiscard]] umi::Result<const sample_t*> input_checked(size_t ch) const noexcept {
        if (ch >= inputs.size()) return umi::Err(Error::InvalidParam);
        return umi::Ok(inputs[ch]);
    }

    /// Get output channel buffer with error handling
    [[nodiscard]] umi::Result<sample_t*> output_checked(size_t ch) const noexcept {
        if (ch >= outputs.size()) return umi::Err(Error::InvalidParam);
        return umi::Ok(outputs[ch]);
    }
    
    /// Clear all output buffers to zero
    void clear_outputs() noexcept {
        for (size_t ch = 0; ch < outputs.size(); ++ch) {
            for (size_t i = 0; i < buffer_size; ++i) {
                outputs[ch][i] = 0.0f;
            }
        }
    }
    
    /// Copy input to output (up to min of channels)
    void passthrough() noexcept {
        const size_t n = std::min(inputs.size(), outputs.size());
        for (size_t ch = 0; ch < n; ++ch) {
            for (size_t i = 0; i < buffer_size; ++i) {
                outputs[ch][i] = inputs[ch][i];
            }
        }
    }
};

/// Control context for Processor::control() callback
struct ControlContext {
    EventQueue<>& events;
    // ParamState& params;  // TODO: Add when ParamState is implemented
};

} // namespace umi
