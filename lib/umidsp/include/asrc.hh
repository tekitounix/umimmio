// SPDX-License-Identifier: MIT
// UMI-DSP: Asynchronous Sample Rate Conversion (ASRC)
// PI controller + cubic hermite interpolation for clock drift compensation
#pragma once

#include <cstdint>

namespace umidsp {

// ============================================================================
// PI Rate Controller
// ============================================================================

/// PI Controller for software ASRC (Asynchronous Sample Rate Conversion)
/// Maintains buffer at target level to absorb timing jitter between two clock domains.
///
/// See lib/umiusb/docs/ASRC_DESIGN.md for detailed design rationale.
///
/// Usage:
///   PiRateController<128, 8> pi;  // target=128 frames, hysteresis=8
///   int32_t ppm = pi.update(current_buffer_level);
///   uint32_t rate = PiRateController<>::ppm_to_rate_q16(ppm);
///
/// For Audio OUT (playback): use ppm directly
/// For Audio IN (recording): use -ppm (inverted polarity)
template<
    int32_t TargetLevel = 128,      // Target buffer level (frames)
    int32_t Hysteresis = 8,         // Dead zone (±frames)
    int32_t MaxPpmAdjust = 1000,    // Maximum PPM adjustment
    int32_t KpNum = 2,              // Kp numerator
    int32_t KpDen = 1,              // Kp denominator
    int32_t KiNum = 1,              // Ki numerator
    int32_t KiDen = 50,             // Ki denominator (Ki = 0.02)
    int32_t IntegralMax = 25000     // Anti-windup limit
>
class PiRateController {
public:
    static constexpr int32_t TARGET_LEVEL = TargetLevel;
    static constexpr int32_t HYSTERESIS = Hysteresis;
    static constexpr int32_t MAX_PPM_ADJUST = MaxPpmAdjust;

    void reset() {
        current_ppm_ = 0;
        integral_ = 0;
        prev_error_ = 0;
    }

    /// Update rate adjustment based on buffer level
    /// Call this every audio DMA completion
    /// Returns PPM adjustment: positive = consume more input (speed up)
    int32_t update(int32_t buffer_level) {
        int32_t error = buffer_level - TARGET_LEVEL;

        // P term with dead zone to avoid micro-adjustments
        int32_t p_contribution = 0;
        if (error < -HYSTERESIS || error > HYSTERESIS) {
            p_contribution = (error * KpNum) / KpDen;
        }

        // I term with trapezoidal integration
        integral_ += (error + prev_error_) / 2;
        prev_error_ = error;

        // Anti-windup clamp
        if (integral_ > IntegralMax) integral_ = IntegralMax;
        if (integral_ < -IntegralMax) integral_ = -IntegralMax;

        int32_t i_contribution = (integral_ * KiNum) / KiDen;

        // Total adjustment
        current_ppm_ = p_contribution + i_contribution;

        // Clamp to safe range
        if (current_ppm_ > MAX_PPM_ADJUST) current_ppm_ = MAX_PPM_ADJUST;
        if (current_ppm_ < -MAX_PPM_ADJUST) current_ppm_ = -MAX_PPM_ADJUST;

        return current_ppm_;
    }

    [[nodiscard]] int32_t current_ppm() const { return current_ppm_; }
    [[nodiscard]] int32_t integral() const { return integral_; }
    [[nodiscard]] int32_t prev_error() const { return prev_error_; }

    /// Convert PPM adjustment to Q16.16 rate ratio
    /// Returns: 0x10000 = 1.0 (no change)
    ///          > 0x10000 = consume more input (speed up)
    ///          < 0x10000 = consume fewer input (slow down)
    [[nodiscard]] static constexpr uint32_t ppm_to_rate_q16(int32_t ppm) {
        // rate = 1.0 + ppm/1000000
        // In Q16.16: 0x10000 + (ppm * 65536) / 1000000
        return static_cast<uint32_t>(0x10000 + (ppm * 65536 + 500000) / 1000000);
    }

private:
    int32_t current_ppm_ = 0;
    int32_t integral_ = 0;
    int32_t prev_error_ = 0;
};

// Default configuration for USB Audio (256 frame buffer, 50% target)
using UsbAudioPiController = PiRateController<128, 8, 1000, 2, 1, 1, 50, 25000>;

// ============================================================================
// Phase Accumulator
// ============================================================================

/// Fixed-point phase accumulator for sample rate conversion
/// Uses Q16.16 format: upper 16 bits = integer, lower 16 bits = fraction
class PhaseAccumulator {
public:
    static constexpr uint32_t FRAC_BITS = 16;
    static constexpr uint32_t FRAC_MASK = (1U << FRAC_BITS) - 1;
    static constexpr uint32_t ONE = 1U << FRAC_BITS;  // 1.0 in Q16.16

    void reset() {
        phase_ = 0;
        rate_ = ONE;
    }

    /// Set rate ratio (Q16.16). 1.0 = 0x10000
    void set_rate(uint32_t rate_q16) { rate_ = rate_q16; }

    /// Get current rate (Q16.16)
    [[nodiscard]] uint32_t rate() const { return rate_; }

    /// Advance phase and return integer samples consumed
    uint32_t advance() {
        phase_ += rate_;
        uint32_t consumed = phase_ >> FRAC_BITS;
        phase_ &= FRAC_MASK;
        return consumed;
    }

    /// Get fractional part as Q0.16
    [[nodiscard]] uint32_t fraction() const { return phase_; }

    /// Get fractional part as float [0, 1)
    [[nodiscard]] float fraction_f() const {
        return static_cast<float>(phase_) / static_cast<float>(ONE);
    }

private:
    uint32_t phase_ = 0;
    uint32_t rate_ = ONE;
};

// ============================================================================
// Cubic Hermite Interpolation (Catmull-Rom)
// ============================================================================

namespace cubic_hermite {

/// Integer implementation using fixed-point math
/// y0, y1, y2, y3: four consecutive samples
/// t_q16: fractional position between y1 and y2 (Q0.16, 0-65535)
/// Returns: interpolated sample between y1 and y2
inline int16_t interpolate_i16(int16_t y0, int16_t y1, int16_t y2, int16_t y3,
                                uint32_t t_q16) {
    // Catmull-Rom spline coefficients:
    // a = -0.5*y0 + 1.5*y1 - 1.5*y2 + 0.5*y3
    // b = y0 - 2.5*y1 + 2*y2 - 0.5*y3
    // c = -0.5*y0 + 0.5*y2
    // d = y1
    // result = ((a*t + b)*t + c)*t + d

    // Use Q8.8 for t to avoid overflow
    int32_t t = static_cast<int32_t>(t_q16 >> 8);

    // Coefficients scaled by 2 to avoid 0.5 fractions
    int32_t a2 = -y0 + 3*y1 - 3*y2 + y3;
    int32_t b2 = 2*y0 - 5*y1 + 4*y2 - y3;
    int32_t c2 = -y0 + y2;
    int32_t d = y1;

    // Horner's method
    int32_t result = a2;
    result = (result * t) >> 8;
    result = result + b2;
    result = (result * t) >> 8;
    result = result + c2;
    result = (result * t) >> 9;  // Extra /2 for 2x scaling
    result = result + d;

    // Clamp to int16 range
    if (result > 32767) result = 32767;
    if (result < -32768) result = -32768;

    return static_cast<int16_t>(result);
}

/// Float implementation (more accurate, requires FPU)
inline int16_t interpolate_f(int16_t y0, int16_t y1, int16_t y2, int16_t y3,
                              float t) {
    float a = -0.5f*y0 + 1.5f*y1 - 1.5f*y2 + 0.5f*y3;
    float b = y0 - 2.5f*y1 + 2.0f*y2 - 0.5f*y3;
    float c = -0.5f*y0 + 0.5f*y2;
    float d = y1;
    float result = ((a*t + b)*t + c)*t + d;

    if (result > 32767.0f) result = 32767.0f;
    if (result < -32768.0f) result = -32768.0f;

    return static_cast<int16_t>(result);
}

/// Interpolate stereo frame (2 channels)
inline void interpolate_stereo_i16(const int16_t* y0, const int16_t* y1,
                                    const int16_t* y2, const int16_t* y3,
                                    uint32_t t_q16, int16_t* out) {
    out[0] = interpolate_i16(y0[0], y1[0], y2[0], y3[0], t_q16);
    out[1] = interpolate_i16(y0[1], y1[1], y2[1], y3[1], t_q16);
}

}  // namespace cubic_hermite

// ============================================================================
// ASRC Processor
// ============================================================================

/// Complete ASRC processor combining PI control and cubic interpolation
/// Template parameters:
///   Channels: number of audio channels (1=mono, 2=stereo)
///   BufferFrames: size of the source buffer (must be power of 2)
template<uint8_t Channels = 2, uint32_t BufferFrames = 256>
class AsrcProcessor {
public:
    static_assert((BufferFrames & (BufferFrames - 1)) == 0, "BufferFrames must be power of 2");
    static constexpr uint32_t MASK = BufferFrames - 1;

    void reset() {
        pi_controller_.reset();
        read_frac_ = 0;
    }

    /// Process ASRC: read from circular buffer with rate conversion
    /// src_buffer: circular buffer of frames (Channels samples per frame)
    /// read_pos: current read position in buffer (updated by caller)
    /// write_pos: current write position in buffer
    /// dest: output buffer
    /// frame_count: number of output frames to generate
    /// Returns: number of input frames consumed
    uint32_t process(const int16_t* src_buffer,
                     uint32_t& read_pos,
                     uint32_t write_pos,
                     int16_t* dest,
                     uint32_t frame_count) {
        // Calculate buffer level and update PI controller
        uint32_t available = (write_pos - read_pos) & MASK;
        int32_t ppm = pi_controller_.update(static_cast<int32_t>(available));
        uint32_t rate = UsbAudioPiController::ppm_to_rate_q16(ppm);

        // Need at least 4 frames for cubic interpolation
        if (available < 4) {
            __builtin_memset(dest, 0, frame_count * Channels * sizeof(int16_t));
            return 0;
        }

        uint32_t out_frames = 0;
        uint32_t frac = read_frac_;
        uint32_t consumed = 0;

        while (out_frames < frame_count) {
            uint32_t cur_available = (write_pos - read_pos) & MASK;
            if (cur_available < 4) break;

            // Get 4 frame indices for interpolation
            uint32_t idx0 = read_pos & MASK;
            uint32_t idx1 = (read_pos + 1) & MASK;
            uint32_t idx2 = (read_pos + 2) & MASK;
            uint32_t idx3 = (read_pos + 3) & MASK;

            // Interpolate each channel
            for (uint8_t ch = 0; ch < Channels; ++ch) {
                int16_t s0 = src_buffer[idx0 * Channels + ch];
                int16_t s1 = src_buffer[idx1 * Channels + ch];
                int16_t s2 = src_buffer[idx2 * Channels + ch];
                int16_t s3 = src_buffer[idx3 * Channels + ch];

                dest[out_frames * Channels + ch] =
                    cubic_hermite::interpolate_i16(s0, s1, s2, s3, frac);
            }
            out_frames++;

            // Advance fractional position
            frac += rate;
            uint32_t step = frac >> 16;
            frac &= 0xFFFF;

            read_pos = (read_pos + step) & MASK;
            consumed += step;
        }

        read_frac_ = frac;

        // Zero-fill remaining frames
        if (out_frames < frame_count) {
            uint32_t remaining = frame_count - out_frames;
            __builtin_memset(dest + out_frames * Channels, 0,
                            remaining * Channels * sizeof(int16_t));
        }

        return consumed;
    }

    /// Get current PPM adjustment
    [[nodiscard]] int32_t current_ppm() const { return pi_controller_.current_ppm(); }

    /// Get current rate as Q16.16
    [[nodiscard]] uint32_t current_rate_q16() const {
        return UsbAudioPiController::ppm_to_rate_q16(pi_controller_.current_ppm());
    }

    /// Direct access to PI controller for custom configurations
    UsbAudioPiController& pi_controller() { return pi_controller_; }
    const UsbAudioPiController& pi_controller() const { return pi_controller_; }

private:
    UsbAudioPiController pi_controller_;
    uint32_t read_frac_ = 0;
};

}  // namespace umidsp
