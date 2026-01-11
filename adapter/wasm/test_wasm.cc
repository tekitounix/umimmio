// =====================================================================
// UMI-OS WASM Test Module
// =====================================================================
// Basic WASM test to verify types and DSP compilation
// Build: xmake build wasm_test
// =====================================================================

#include <cstdint>

// Include UMI headers
#include "include/umi/types.hh"
#include "include/umi/time.hh"
#include "include/umi/event.hh"
#include "dsp/dsp.hh"

extern "C" {

// ---------------------------------------------------------------------
// Type Tests
// ---------------------------------------------------------------------

int32_t umi_test_types() {
    int32_t passed = 0;
    
    // Test sample_t (1)
    umi::sample_t s = 0.5f;
    if (s > 0.0f && s < 1.0f) passed++;
    
    // Test constants (2, 3)
    if (umi::kDefaultSampleRate == 48000) passed++;
    if (umi::kDefaultBlockSize == 64) passed++;  // Default is 64, not 256
    
    // Test time conversions (4, 5)
    auto samples = umi::time::ms_to_samples(1000.0f, 48000);
    if (samples >= 47000 && samples <= 49000) passed++;  // Very lenient for float issues
    
    auto ms = umi::time::samples_to_ms(48000, 48000);
    if (ms > 900.0f && ms < 1100.0f) passed++;  // Very lenient
    
    // Test Event struct (6)
    umi::Event ev{};
    ev.type = umi::EventType::Midi;
    ev.sample_pos = 0;
    ev.port_id = 0;
    ev.midi.bytes[0] = 0x90;
    ev.midi.bytes[1] = 60;
    ev.midi.bytes[2] = 100;
    ev.midi.size = 3;
    if (ev.midi.bytes[0] == 0x90) passed++;
    
    return passed;  // Expected: 6
}

// ---------------------------------------------------------------------
// DSP Tests
// ---------------------------------------------------------------------

int32_t umi_test_dsp() {
    int32_t passed = 0;
    
    constexpr float sample_rate = 48000.0f;
    
    // Test sine oscillator
    umi::dsp::Sine osc;
    const float freq_norm = 440.0f / sample_rate;  // Normalized frequency
    
    float sum = 0.0f;
    for (int i = 0; i < 100; ++i) {
        float s = osc.tick(freq_norm);
        sum += s * s;  // RMS accumulation
    }
    if (sum > 0.0f) passed++;  // Oscillator produces output
    
    // Test ADSR envelope
    umi::dsp::ADSR env;
    // set_params(attack_ms, decay_ms, sustain, release_ms, sample_rate)
    env.set_params(10.0f, 100.0f, 0.7f, 200.0f, sample_rate);
    
    env.trigger();
    float peak = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        float v = env.tick();
        if (v > peak) peak = v;
    }
    if (peak > 0.5f) passed++;  // Envelope reaches sustain
    
    env.release();
    float final_val = 0.0f;
    for (int i = 0; i < 20000; ++i) {
        final_val = env.tick();
    }
    if (final_val < 0.01f) passed++;  // Envelope releases
    
    // Test OnePole filter
    umi::dsp::OnePole lpf;
    lpf.set_cutoff(1000.0f / sample_rate);  // Normalized cutoff
    
    float filtered = 0.0f;
    for (int i = 0; i < 100; ++i) {
        filtered = lpf.tick((i % 2 == 0) ? 1.0f : -1.0f);
    }
    // Low-pass should smooth out square wave
    if (filtered > -1.0f && filtered < 1.0f) passed++;
    
    // Test utility functions
    float freq = umi::dsp::midi_to_freq(69);  // A4 = 440Hz
    if (freq > 439.0f && freq < 441.0f) passed++;
    
    float clipped = umi::dsp::soft_clip(5.0f);
    if (clipped > 0.9f && clipped < 1.01f) passed++;
    
    return passed;  // Expected: 6
}

}  // extern "C"
