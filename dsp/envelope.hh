// SPDX-License-Identifier: MIT
// UMI-OS - DSP Envelope
//
// Dependency-free envelope generator implementations.
// Can be used in any C++ project without UMI-OS dependencies.

#pragma once

#include <cstdint>
#include <cmath>

namespace umi::dsp {

// ============================================================================
// ADSR Envelope
// ============================================================================

/// ADSR envelope generator
class ADSR {
public:
    enum class State : uint8_t {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };
    
    ADSR() = default;
    
    /// Set envelope times and sustain level
    /// @param attack_ms Attack time in milliseconds
    /// @param decay_ms Decay time in milliseconds
    /// @param sustain Sustain level (0.0 to 1.0)
    /// @param release_ms Release time in milliseconds
    /// @param sample_rate Sample rate in Hz
    void set_params(float attack_ms, float decay_ms, float sustain,
                    float release_ms, float sample_rate) noexcept {
        sustain_ = sustain;
        
        // Calculate rates (samples to reach target)
        float attack_samples = attack_ms * sample_rate / 1000.0f;
        float decay_samples = decay_ms * sample_rate / 1000.0f;
        float release_samples = release_ms * sample_rate / 1000.0f;
        
        // Use exponential curves: coef = 1 - exp(-time_constant)
        // For 99% of target in N samples: coef = 1 - exp(-5/N)
        attack_rate_ = attack_samples > 0 ? 
            1.0f - std::exp(-5.0f / attack_samples) : 1.0f;
        decay_rate_ = decay_samples > 0 ?
            1.0f - std::exp(-5.0f / decay_samples) : 1.0f;
        release_rate_ = release_samples > 0 ?
            1.0f - std::exp(-5.0f / release_samples) : 1.0f;
    }
    
    /// Trigger envelope (gate on)
    void trigger() noexcept {
        state_ = State::Attack;
    }
    
    /// Release envelope (gate off)
    void release() noexcept {
        if (state_ != State::Idle) {
            state_ = State::Release;
        }
    }
    
    /// Force envelope to idle
    void reset() noexcept {
        state_ = State::Idle;
        value_ = 0.0f;
    }
    
    /// Generate next sample
    [[nodiscard]] float tick() noexcept {
        switch (state_) {
            case State::Idle:
                value_ = 0.0f;
                break;
                
            case State::Attack:
                value_ += attack_rate_ * (1.0f - value_);
                if (value_ >= 0.999f) {
                    value_ = 1.0f;
                    state_ = State::Decay;
                }
                break;
                
            case State::Decay:
                value_ += decay_rate_ * (sustain_ - value_);
                if (value_ <= sustain_ + 0.001f) {
                    value_ = sustain_;
                    state_ = State::Sustain;
                }
                break;
                
            case State::Sustain:
                value_ = sustain_;
                break;
                
            case State::Release:
                value_ += release_rate_ * (0.0f - value_);
                if (value_ <= 0.001f) {
                    value_ = 0.0f;
                    state_ = State::Idle;
                }
                break;
        }
        
        return value_;
    }
    
    /// Current envelope state
    [[nodiscard]] State state() const noexcept { return state_; }
    
    /// Current envelope value
    [[nodiscard]] float value() const noexcept { return value_; }
    
    /// Is envelope active (not idle)?
    [[nodiscard]] bool active() const noexcept { return state_ != State::Idle; }
    
private:
    State state_ = State::Idle;
    float value_ = 0.0f;
    float sustain_ = 0.5f;
    float attack_rate_ = 0.01f;
    float decay_rate_ = 0.001f;
    float release_rate_ = 0.001f;
};

// ============================================================================
// AR Envelope (Attack-Release only)
// ============================================================================

/// Simple attack-release envelope
class AR {
public:
    AR() = default;
    
    void set_params(float attack_ms, float release_ms, float sample_rate) noexcept {
        float attack_samples = attack_ms * sample_rate / 1000.0f;
        float release_samples = release_ms * sample_rate / 1000.0f;
        
        attack_rate_ = attack_samples > 0 ?
            1.0f - std::exp(-5.0f / attack_samples) : 1.0f;
        release_rate_ = release_samples > 0 ?
            1.0f - std::exp(-5.0f / release_samples) : 1.0f;
    }
    
    void trigger() noexcept { attacking_ = true; }
    void release() noexcept { attacking_ = false; }
    void reset() noexcept { value_ = 0.0f; attacking_ = false; }
    
    [[nodiscard]] float tick() noexcept {
        if (attacking_) {
            value_ += attack_rate_ * (1.0f - value_);
        } else {
            value_ += release_rate_ * (0.0f - value_);
        }
        return value_;
    }
    
    [[nodiscard]] float value() const noexcept { return value_; }
    [[nodiscard]] bool active() const noexcept { return value_ > 0.001f; }
    
private:
    float value_ = 0.0f;
    float attack_rate_ = 0.01f;
    float release_rate_ = 0.001f;
    bool attacking_ = false;
};

// ============================================================================
// Linear Ramp
// ============================================================================

/// Linear ramp generator
class Ramp {
public:
    Ramp() = default;
    
    /// Set target value and time to reach it
    void set_target(float target, uint32_t samples) noexcept {
        if (samples == 0) {
            value_ = target;
            rate_ = 0.0f;
            samples_remaining_ = 0;
        } else {
            rate_ = (target - value_) / static_cast<float>(samples);
            samples_remaining_ = samples;
            target_ = target;
        }
    }
    
    /// Set immediate value (no ramp)
    void set_immediate(float value) noexcept {
        value_ = value;
        rate_ = 0.0f;
        samples_remaining_ = 0;
    }
    
    [[nodiscard]] float tick() noexcept {
        if (samples_remaining_ > 0) {
            value_ += rate_;
            samples_remaining_--;
            if (samples_remaining_ == 0) {
                value_ = target_;
            }
        }
        return value_;
    }
    
    [[nodiscard]] float value() const noexcept { return value_; }
    [[nodiscard]] bool active() const noexcept { return samples_remaining_ > 0; }
    
private:
    float value_ = 0.0f;
    float target_ = 0.0f;
    float rate_ = 0.0f;
    uint32_t samples_remaining_ = 0;
};

} // namespace umi::dsp
