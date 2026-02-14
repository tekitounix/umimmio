// =====================================================================
// UMI-OS Simple Synthesizer - WASM Entry Point
// =====================================================================
// Implements UMIM port-based API.
// Uses the same PolySynth as embedded build.
// =====================================================================

#include "synth.hh"
#include <cstdint>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// EMSCRIPTEN_KEEPALIVE prevents function name minification with -O3
#define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_EXPORT
#endif

// =====================================================================
// Global State
// =====================================================================

static umi::synth::PolySynth g_synth;

// Port buffers
static float* g_audio_out_buffer = nullptr;

// Event queue for input events
struct MidiEvent {
    uint8_t data[8];
    uint8_t size;
    uint32_t sample_offset;
};

static constexpr size_t MAX_EVENTS = 64;
static MidiEvent g_event_queue[MAX_EVENTS];
static size_t g_event_count = 0;

// Sample rate for lazy init (UMIM spec: umi_create takes no args)
static float g_sample_rate = 48000.0f;
static bool g_initialized = false;

static void ensure_initialized(float sample_rate) {
    if (!g_initialized || g_sample_rate != sample_rate) {
        g_sample_rate = sample_rate;
        g_synth.init(sample_rate);
        g_initialized = true;
    }
}

// =====================================================================
// Port Definitions
// =====================================================================

struct PortInfo {
    const char* name;
    uint8_t direction;  // 0=In, 1=Out
    uint8_t kind;       // 0=Continuous, 1=Event
};

static constexpr PortInfo g_ports[] = {
    { "midi_in",   0, 1 },  // In, Event
    { "audio_out", 1, 0 },  // Out, Continuous
};

static constexpr uint32_t PORT_COUNT = sizeof(g_ports) / sizeof(g_ports[0]);

// =====================================================================
// WASM Exports - Lifecycle
// =====================================================================

extern "C" {

WASM_EXPORT void umi_create(void) {
    g_initialized = false;
    g_event_count = 0;
}

WASM_EXPORT void umi_destroy() {
    g_audio_out_buffer = nullptr;
    g_event_count = 0;
}

// =====================================================================
// WASM Exports - Port API
// =====================================================================

WASM_EXPORT uint32_t umi_get_port_count() {
    return PORT_COUNT;
}

WASM_EXPORT const char* umi_get_port_name(uint32_t index) {
    if (index >= PORT_COUNT) return "";
    return g_ports[index].name;
}

WASM_EXPORT uint8_t umi_get_port_direction(uint32_t index) {
    if (index >= PORT_COUNT) return 0;
    return g_ports[index].direction;
}

WASM_EXPORT uint8_t umi_get_port_kind(uint32_t index) {
    if (index >= PORT_COUNT) return 0;
    return g_ports[index].kind;
}

WASM_EXPORT void umi_set_port_buffer(uint32_t index, float* buffer) {
    if (index == umi::synth::PORT_AUDIO_OUT) {
        g_audio_out_buffer = buffer;
    }
}

WASM_EXPORT float* umi_get_port_buffer(uint32_t index) {
    if (index == umi::synth::PORT_AUDIO_OUT) {
        return g_audio_out_buffer;
    }
    return nullptr;
}

// =====================================================================
// WASM Exports - Event API
// =====================================================================

WASM_EXPORT void umi_send_event(uint32_t port_index, const uint8_t* data, uint8_t size, uint32_t sample_offset) {
    if (port_index != umi::synth::PORT_MIDI_IN) return;
    if (g_event_count >= MAX_EVENTS) return;
    if (size > 8) size = 8;

    auto& evt = g_event_queue[g_event_count++];
    std::memcpy(evt.data, data, size);
    evt.size = size;
    evt.sample_offset = sample_offset;
}

WASM_EXPORT bool umi_recv_event(uint32_t port_index, uint8_t* data, uint8_t* size, uint32_t* sample_offset) {
    (void)port_index;
    (void)data;
    (void)size;
    (void)sample_offset;
    // This synth has no output event ports
    return false;
}

WASM_EXPORT void umi_clear_events() {
    g_event_count = 0;
}

// =====================================================================
// WASM Exports - Audio Processing
// =====================================================================

WASM_EXPORT void umi_process(const float* input, float* output, uint32_t frames, uint32_t sample_rate) {
    (void)input;  // Synth generates audio, doesn't process input
    if (!output) return;

    // Lazy initialization with sample rate from process call
    ensure_initialized(static_cast<float>(sample_rate));

    g_audio_out_buffer = output;

    // Sort events by sample_offset (simple bubble sort, event count is small)
    for (size_t i = 0; i < g_event_count; ++i) {
        for (size_t j = i + 1; j < g_event_count; ++j) {
            if (g_event_queue[j].sample_offset < g_event_queue[i].sample_offset) {
                auto tmp = g_event_queue[i];
                g_event_queue[i] = g_event_queue[j];
                g_event_queue[j] = tmp;
            }
        }
    }

    // Process with sample-accurate event handling
    size_t event_idx = 0;
    for (uint32_t i = 0; i < frames; ++i) {
        // Process events at this sample position
        while (event_idx < g_event_count && g_event_queue[event_idx].sample_offset <= i) {
            g_synth.handle_midi(g_event_queue[event_idx].data, g_event_queue[event_idx].size);
            ++event_idx;
        }

        g_audio_out_buffer[i] = g_synth.process_sample();
    }

    // Clear event queue after processing
    g_event_count = 0;
}

// =====================================================================
// WASM Exports - Legacy API (backward compatibility)
// =====================================================================

WASM_EXPORT void umi_process_simple(const float* input, float* output, uint32_t frames) {
    // Default to 48kHz for legacy API
    umi_process(input, output, frames, 48000);
}

WASM_EXPORT void umi_note_on(uint8_t note, uint8_t velocity) {
    g_synth.note_on(note, velocity);
}

WASM_EXPORT void umi_note_off(uint8_t note) {
    g_synth.note_off(note);
}

// =====================================================================
// WASM Exports - Parameter API
// =====================================================================

WASM_EXPORT void umi_set_param(uint32_t index, float value) {
    (void)index;
    (void)value;
    // TODO: Add parameter support
}

WASM_EXPORT float umi_get_param(uint32_t index) {
    (void)index;
    return 0.0f;
}

WASM_EXPORT uint32_t umi_get_param_count() {
    return 0;
}

WASM_EXPORT const char* umi_get_param_name(uint32_t index) {
    (void)index;
    return "";
}

WASM_EXPORT float umi_get_param_min(uint32_t index) {
    (void)index;
    return 0.0f;
}

WASM_EXPORT float umi_get_param_max(uint32_t index) {
    (void)index;
    return 1.0f;
}

WASM_EXPORT float umi_get_param_default(uint32_t index) {
    (void)index;
    return 0.0f;
}

WASM_EXPORT uint8_t umi_get_param_curve(uint32_t index) {
    (void)index;
    return 0;
}

WASM_EXPORT uint16_t umi_get_param_id(uint32_t index) {
    (void)index;
    return 0;
}

WASM_EXPORT const char* umi_get_param_unit(uint32_t index) {
    (void)index;
    return "";
}

WASM_EXPORT void umi_process_cc(uint8_t channel, uint8_t cc, uint8_t value) {
    (void)channel;
    (void)cc;
    (void)value;
}

// =====================================================================
// WASM Exports - Plugin Info
// =====================================================================

WASM_EXPORT const char* umi_get_processor_name() {
    return "umi-synth-processor";
}

WASM_EXPORT const char* umi_get_name() {
    return "UMI Synth";
}

WASM_EXPORT const char* umi_get_vendor() {
    return "UMI-OS";
}

WASM_EXPORT const char* umi_get_version() {
    return "1.0.0";
}

WASM_EXPORT uint32_t umi_get_type() {
    return 1;  // Instrument
}

} // extern "C"
