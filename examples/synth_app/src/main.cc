// SPDX-License-Identifier: MIT
// Polyphonic Synth Application (.umiapp)
// Uses PolySynth from headless_webhost
// Receives MIDI via kernel syscall, outputs audio via synth_process callback

#include <umi_app.hh>
#include <synth.hh>  // headless_webhost/src/synth.hh

// ============================================================================
// Synth Instance
// ============================================================================

static umi::synth::PolySynth g_synth;
static bool g_initialized = false;

// ============================================================================
// Audio Processor (AudioContext-based)
// ============================================================================

namespace {
struct SynthProcessor {
    void process(umi::AudioContext& ctx) {
        // Initialize or update sample rate from AudioContext
        if (!g_initialized) {
            g_synth.init(static_cast<float>(ctx.sample_rate));
            g_initialized = true;
        } else if (static_cast<uint32_t>(g_synth.get_sample_rate()) != ctx.sample_rate) {
            g_synth.set_sample_rate(static_cast<float>(ctx.sample_rate));
        }

        // Process input events from AudioContext
        for (const auto& ev : ctx.input_events) {
            if (ev.type == umi::EventType::Midi) {
                g_synth.handle_midi(ev.midi.bytes, ev.midi.size);
            }
        }

        auto* out_l = ctx.output(0);
        auto* out_r = ctx.output(1);
        if (!out_l) return;

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            float sample = g_synth.process_sample();
            out_l[i] = sample;
            if (out_r) {
                out_r[i] = sample;
            }
        }
    }
};

static SynthProcessor g_processor;
}  // namespace

// ============================================================================
// Main
// ============================================================================

// Debug: toggle GPIO (if accessible from unprivileged mode...)
// Actually we're running in privileged mode when kernel calls us
namespace {
struct GPIORegs {
    volatile uint32_t MODER;
    volatile uint32_t OTYPER;
    volatile uint32_t OSPEEDR;
    volatile uint32_t PUPDR;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t LCKR;
    volatile uint32_t AFRL;
    volatile uint32_t AFRH;
};
static auto& debug_gpio = *reinterpret_cast<GPIORegs*>(0x40020C00);  // GPIOD
}

int main() {
    // Debug: we're called by kernel in privileged mode
    // Orange LED (PD13) shows we entered main
    debug_gpio.BSRR = (1 << 13);  // Set PD13 (orange LED)
    
    // Register processor with kernel (AudioContext-based)
    umi::register_processor(g_processor);
    
    // If we get here, syscall returned
    // Blue LED + orange = both on = syscall completed
    debug_gpio.BSRR = (1 << 15);  // Set PD15 (blue LED)
    
    // Return to kernel - audio processing happens via callback
    return 0;
}
