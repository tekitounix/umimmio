// =====================================================================
// UMI-OS WASM Synth Module
// =====================================================================
// Simple polyphonic synthesizer for AudioWorklet
// Build: xmake build wasm_synth
// =====================================================================

#include <cstdint>
#include <array>
#include <cmath>

#include "include/umi/types.hh"
#include "include/umi/audio_context.hh"
#include "include/umi/processor.hh"
#include "dsp/dsp.hh"

namespace {

// ---------------------------------------------------------------------
// Voice - Single monophonic voice
// ---------------------------------------------------------------------

struct Voice {
    umi::dsp::SawBL osc;
    umi::dsp::ADSR env;
    umi::dsp::SVF filter;
    
    uint8_t note = 0;
    float velocity = 0.0f;
    float freq_norm = 0.0f;
    float filter_cutoff_norm = 0.1f;  // Normalized cutoff
    bool active = false;
    
    void init(float sample_rate) {
        // ADSR: set_params(attack_ms, decay_ms, sustain, release_ms, sample_rate)
        env.set_params(10.0f, 100.0f, 0.6f, 300.0f, sample_rate);
        filter.set_params(filter_cutoff_norm, 0.5f);
    }
    
    void note_on(uint8_t n, float vel, float sample_rate) {
        note = n;
        velocity = vel;
        active = true;
        
        float freq = umi::dsp::midi_to_freq(n);
        freq_norm = freq / sample_rate;
        env.trigger();
    }
    
    void note_off() {
        env.release();
    }
    
    void set_filter(float cutoff_norm, float resonance) {
        filter_cutoff_norm = cutoff_norm;
        filter.set_params(cutoff_norm, 1.0f / (1.0f - resonance * 0.99f));
    }
    
    float tick() {
        if (!active) return 0.0f;
        
        float e = env.tick();
        if (env.state() == umi::dsp::ADSR::State::Idle) {
            active = false;
            return 0.0f;
        }
        
        float sample = osc.tick(freq_norm);
        filter.tick(sample);
        return filter.lp() * e * velocity;
    }
};

// ---------------------------------------------------------------------
// SimpleSynth - Polyphonic synthesizer
// ---------------------------------------------------------------------

constexpr int kMaxVoices = 8;
constexpr int kMaxBufferSize = 256;

class SimpleSynth {
    std::array<Voice, kMaxVoices> voices_;
    std::array<float, kMaxBufferSize> buffer_;
    
    float sample_rate_ = 48000.0f;
    float master_volume_ = 0.5f;
    float filter_cutoff_norm_ = 0.1f;  // 0.0 - 0.5
    float filter_resonance_ = 0.5f;    // 0.0 - 1.0
    
public:
    void init(float sample_rate) {
        sample_rate_ = sample_rate;
        for (auto& v : voices_) {
            v.init(sample_rate);
        }
        buffer_.fill(0.0f);
    }
    
    void note_on(uint8_t note, uint8_t velocity) {
        float vel = velocity / 127.0f;
        
        // Find free voice or steal oldest
        Voice* target = nullptr;
        
        // First, try to find a free voice
        for (auto& v : voices_) {
            if (!v.active) {
                target = &v;
                break;
            }
        }
        
        // If no free voice, steal the first one
        if (!target) {
            target = &voices_[0];
        }
        
        target->note_on(note, vel, sample_rate_);
    }
    
    void note_off(uint8_t note) {
        for (auto& v : voices_) {
            if (v.active && v.note == note) {
                v.note_off();
            }
        }
    }
    
    void set_param(uint32_t id, float value) {
        switch (id) {
            case 0:  // Master volume (0-1)
                master_volume_ = value;
                break;
            case 1:  // Filter cutoff (20-20000 Hz -> normalized)
                filter_cutoff_norm_ = value / sample_rate_;
                if (filter_cutoff_norm_ > 0.49f) filter_cutoff_norm_ = 0.49f;
                for (auto& v : voices_) {
                    v.set_filter(filter_cutoff_norm_, filter_resonance_);
                }
                break;
            case 2:  // Filter resonance (0-1)
                filter_resonance_ = value;
                for (auto& v : voices_) {
                    v.set_filter(filter_cutoff_norm_, filter_resonance_);
                }
                break;
        }
    }
    
    void process(float* output, uint32_t frames) {
        for (uint32_t i = 0; i < frames; ++i) {
            float sum = 0.0f;
            for (auto& v : voices_) {
                sum += v.tick();
            }
            output[i] = umi::dsp::soft_clip(sum * master_volume_);
        }
    }
    
    float* get_buffer_ptr() {
        return buffer_.data();
    }
};

// Global instance
SimpleSynth g_synth;

}  // namespace

// ---------------------------------------------------------------------
// C API for JavaScript
// ---------------------------------------------------------------------

extern "C" {

void* umi_create(float sample_rate) {
    g_synth.init(sample_rate);
    return &g_synth;
}

void umi_destroy(void* /* synth */) {
    // Nothing to do for static instance
}

void umi_process(void* /* synth */, float* output, uint32_t frames) {
    g_synth.process(output, frames);
}

void umi_note_on(void* /* synth */, uint8_t note, uint8_t velocity) {
    g_synth.note_on(note, velocity);
}

void umi_note_off(void* /* synth */, uint8_t note) {
    g_synth.note_off(note);
}

void umi_set_param(void* /* synth */, uint32_t id, float value) {
    g_synth.set_param(id, value);
}

float* umi_get_buffer_ptr(void* /* synth */) {
    return g_synth.get_buffer_ptr();
}

}  // extern "C"
