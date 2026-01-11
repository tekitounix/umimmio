// SPDX-License-Identifier: MIT
// UMI-OS - DSP Filter
//
// Dependency-free filter implementations for audio processing.
// Can be used in any C++ project without UMI-OS dependencies.

#pragma once

#include <cmath>
#include <cstdint>

namespace umi::dsp {

// ============================================================================
// One-Pole Filter
// ============================================================================

/// Simple one-pole lowpass filter
class OnePole {
public:
    OnePole() = default;
    
    /// Set cutoff frequency
    /// @param cutoff Normalized frequency (0.0 to 0.5)
    void set_cutoff(float cutoff) noexcept {
        // Simple approximation: coef = 2 * pi * fc
        // More accurate: coef = 1 - exp(-2 * pi * fc)
        coef_ = cutoff * 6.283185307f;
        if (coef_ > 1.0f) coef_ = 1.0f;
    }
    
    /// Process one sample
    [[nodiscard]] float tick(float in) noexcept {
        y1_ += coef_ * (in - y1_);
        return y1_;
    }
    
    /// Reset filter state
    void reset() noexcept { y1_ = 0.0f; }
    
private:
    float y1_ = 0.0f;
    float coef_ = 0.0f;
};

// ============================================================================
// Biquad Filter
// ============================================================================

/// Biquad filter (transposed direct form II)
class Biquad {
public:
    Biquad() = default;
    
    /// Set coefficients directly
    void set_coeffs(float b0, float b1, float b2, float a1, float a2) noexcept {
        b0_ = b0; b1_ = b1; b2_ = b2;
        a1_ = a1; a2_ = a2;
    }
    
    /// Configure as lowpass
    /// @param cutoff Normalized cutoff frequency (0.0 to 0.5)
    /// @param Q Resonance (0.707 = Butterworth, higher = more resonant)
    void set_lowpass(float cutoff, float Q = 0.707f) noexcept {
        float omega = cutoff * 6.283185307f;
        float sin_omega = std::sin(omega);
        float cos_omega = std::cos(omega);
        float alpha = sin_omega / (2.0f * Q);
        
        float a0 = 1.0f + alpha;
        float a0_inv = 1.0f / a0;
        
        b0_ = (1.0f - cos_omega) * 0.5f * a0_inv;
        b1_ = (1.0f - cos_omega) * a0_inv;
        b2_ = b0_;
        a1_ = -2.0f * cos_omega * a0_inv;
        a2_ = (1.0f - alpha) * a0_inv;
    }
    
    /// Configure as highpass
    void set_highpass(float cutoff, float Q = 0.707f) noexcept {
        float omega = cutoff * 6.283185307f;
        float sin_omega = std::sin(omega);
        float cos_omega = std::cos(omega);
        float alpha = sin_omega / (2.0f * Q);
        
        float a0 = 1.0f + alpha;
        float a0_inv = 1.0f / a0;
        
        b0_ = (1.0f + cos_omega) * 0.5f * a0_inv;
        b1_ = -(1.0f + cos_omega) * a0_inv;
        b2_ = b0_;
        a1_ = -2.0f * cos_omega * a0_inv;
        a2_ = (1.0f - alpha) * a0_inv;
    }
    
    /// Configure as bandpass
    void set_bandpass(float cutoff, float Q = 1.0f) noexcept {
        float omega = cutoff * 6.283185307f;
        float sin_omega = std::sin(omega);
        float cos_omega = std::cos(omega);
        float alpha = sin_omega / (2.0f * Q);
        
        float a0 = 1.0f + alpha;
        float a0_inv = 1.0f / a0;
        
        b0_ = alpha * a0_inv;
        b1_ = 0.0f;
        b2_ = -alpha * a0_inv;
        a1_ = -2.0f * cos_omega * a0_inv;
        a2_ = (1.0f - alpha) * a0_inv;
    }
    
    /// Configure as notch
    void set_notch(float cutoff, float Q = 1.0f) noexcept {
        float omega = cutoff * 6.283185307f;
        float sin_omega = std::sin(omega);
        float cos_omega = std::cos(omega);
        float alpha = sin_omega / (2.0f * Q);
        
        float a0 = 1.0f + alpha;
        float a0_inv = 1.0f / a0;
        
        b0_ = a0_inv;
        b1_ = -2.0f * cos_omega * a0_inv;
        b2_ = a0_inv;
        a1_ = b1_;
        a2_ = (1.0f - alpha) * a0_inv;
    }
    
    /// Process one sample
    [[nodiscard]] float tick(float in) noexcept {
        float out = b0_ * in + s1_;
        s1_ = b1_ * in - a1_ * out + s2_;
        s2_ = b2_ * in - a2_ * out;
        return out;
    }
    
    /// Reset filter state
    void reset() noexcept {
        s1_ = 0.0f;
        s2_ = 0.0f;
    }
    
private:
    // Coefficients
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    // State (transposed form II)
    float s1_ = 0.0f, s2_ = 0.0f;
};

// ============================================================================
// State Variable Filter (SVF)
// ============================================================================

/// State Variable Filter - outputs LP, BP, HP, Notch simultaneously
class SVF {
public:
    SVF() = default;
    
    /// Set filter parameters
    /// @param cutoff Normalized cutoff (0.0 to 0.5)
    /// @param Q Resonance
    void set_params(float cutoff, float Q = 0.707f) noexcept {
        f_ = 2.0f * std::sin(cutoff * 3.141592653f);
        if (f_ > 1.9f) f_ = 1.9f;  // Prevent instability
        q_ = 1.0f / Q;
    }
    
    /// Process one sample
    void tick(float in) noexcept {
        hp_ = in - lp_ - q_ * bp_;
        bp_ += f_ * hp_;
        lp_ += f_ * bp_;
        notch_ = hp_ + lp_;
    }
    
    /// Get lowpass output
    [[nodiscard]] float lp() const noexcept { return lp_; }
    
    /// Get bandpass output
    [[nodiscard]] float bp() const noexcept { return bp_; }
    
    /// Get highpass output
    [[nodiscard]] float hp() const noexcept { return hp_; }
    
    /// Get notch output
    [[nodiscard]] float notch() const noexcept { return notch_; }
    
    /// Reset filter state
    void reset() noexcept {
        lp_ = bp_ = hp_ = notch_ = 0.0f;
    }
    
private:
    float f_ = 0.0f;
    float q_ = 1.0f;
    float lp_ = 0.0f, bp_ = 0.0f, hp_ = 0.0f, notch_ = 0.0f;
};

} // namespace umi::dsp
