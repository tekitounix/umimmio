// SPDX-License-Identifier: MIT
// UMI Plugin Format (UPF) Adapter
//
// A standardized WASM plugin format for audio processors.
// Single macro: UPF_PLUGIN(AppClass)
//
// App must provide:
//   - void process(AudioContext& ctx)
//   - void note_on(uint8_t note, uint8_t velocity)
//   - void note_off(uint8_t note)
//   - void set_param(uint32_t index, float value)
//   - float get_param(uint32_t index) const
//   - static constexpr size_t param_count()
//   - static const ParamMeta& param_meta(uint32_t index)
//   - static uint32_t find_param_index_by_cc(uint8_t channel, uint8_t cc)

#pragma once

#include <cstdint>
#include <array>

#include "core/types.hh"
#include "core/audio_context.hh"
#include "core/event.hh"
#include "core/ui_map.hh"

namespace umi::upf {

// ============================================================================
// UPF Adapter - Core logic (C++ template)
// ============================================================================

template<typename App, size_t MaxBufferSize = 512>
class Adapter {
public:
    void create(float sample_rate) {
        sample_rate_ = sample_rate;
    }
    
    void process(const float* input, float* output, uint32_t frames) {
        input_ptrs_[0] = input;
        output_ptrs_[0] = output;
        
        AudioContext ctx{
            .inputs = input_ptrs_,
            .outputs = output_ptrs_,
            .events = events_,
            .sample_rate = static_cast<uint32_t>(sample_rate_),
            .buffer_size = frames,
            .dt = 1.0f / sample_rate_,
            .sample_position = sample_position_,
        };
        
        app_.process(ctx);
        events_.clear();
        sample_position_ += frames;
    }
    
    void process_synth_only(float* output, uint32_t frames) {
        process(nullptr, output, frames);
    }
    
    void note_on(uint8_t note, uint8_t velocity) {
        app_.note_on(note, velocity);
    }
    
    void note_off(uint8_t note) {
        app_.note_off(note);
    }
    
    void set_param(uint32_t index, float value) {
        app_.set_param(index, value);
    }
    
    float get_param(uint32_t index) const {
        return app_.get_param(index);
    }
    
    static constexpr size_t param_count() {
        return App::param_count();
    }
    
    static const auto& param_meta(uint32_t index) {
        return App::param_meta(index);
    }
    
    void set_param_normalized(uint32_t index, float normalized) {
        if (index >= param_count()) return;
        const auto& meta = param_meta(index);
        float display = meta.to_display(normalized);
        set_param(index, display);
    }
    
    void process_cc(uint8_t channel, uint8_t cc, uint8_t value) {
        auto param_index = App::find_param_index_by_cc(channel, cc);
        if (param_index < param_count()) {
            float normalized = static_cast<float>(value) / 127.0f;
            set_param_normalized(param_index, normalized);
            return;
        }
        
        const auto* dynamic_mapping = midi_map_.find(channel, cc);
        if (dynamic_mapping) {
            float normalized = dynamic_mapping->cc_to_normalized(value);
            set_param_normalized(dynamic_mapping->param_id, normalized);
        }
    }
    
    void midi_learn(uint8_t cc, uint32_t param_id) {
        midi_map_.add({
            .channel = 0,
            .cc_number = cc,
            .param_id = param_id,
            .is_learned = true,
        });
    }
    
    void midi_unlearn(uint8_t /*cc*/) {
        // TODO: implement
    }
    
    float* get_buffer_ptr() {
        return buffer_.data();
    }
    
    App& app() { return app_; }
    const App& app() const { return app_; }
    
private:
    App app_;
    EventQueue<> events_;
    MidiMapDynamic midi_map_;
    
    float sample_rate_ = 48000.0f;
    sample_position_t sample_position_ = 0;
    
    std::array<float, MaxBufferSize> buffer_{};
    std::array<const sample_t*, 1> input_ptrs_{};
    std::array<sample_t*, 1> output_ptrs_{};
};

} // namespace umi::upf

// ============================================================================
// UPF_PLUGIN Macro - Single argument, generates everything
// ============================================================================
// Usage: UPF_PLUGIN(MyAppClass)

#define UPF_PLUGIN(AppClass) \
    namespace { \
        using _UPF_Adapter = ::umi::upf::Adapter<AppClass>; \
        _UPF_Adapter _upf_adapter; \
    } \
    extern "C" { \
    __attribute__((used, visibility("default"))) \
    void upf_create(float sample_rate) { \
        _upf_adapter.create(sample_rate); \
    } \
    __attribute__((used, visibility("default"))) \
    void upf_process(const float* input, float* output, uint32_t frames) { \
        _upf_adapter.process(input, output, frames); \
    } \
    __attribute__((used, visibility("default"))) \
    void upf_note_on(uint8_t note, uint8_t velocity) { \
        _upf_adapter.note_on(note, velocity); \
    } \
    __attribute__((used, visibility("default"))) \
    void upf_note_off(uint8_t note) { \
        _upf_adapter.note_off(note); \
    } \
    __attribute__((used, visibility("default"))) \
    void upf_set_param(uint32_t id, float value) { \
        _upf_adapter.set_param(id, value); \
    } \
    __attribute__((used, visibility("default"))) \
    float upf_get_param(uint32_t id) { \
        return _upf_adapter.get_param(id); \
    } \
    __attribute__((used, visibility("default"))) \
    void upf_process_cc(uint8_t channel, uint8_t cc, uint8_t value) { \
        _upf_adapter.process_cc(channel, cc, value); \
    } \
    __attribute__((used, visibility("default"))) \
    void upf_midi_learn(uint8_t cc, uint32_t param_id) { \
        _upf_adapter.midi_learn(cc, param_id); \
    } \
    __attribute__((used, visibility("default"))) \
    void upf_midi_unlearn(uint8_t cc) { \
        _upf_adapter.midi_unlearn(cc); \
    } \
    __attribute__((used, visibility("default"))) \
    float* upf_get_buffer_ptr() { \
        return _upf_adapter.get_buffer_ptr(); \
    } \
    __attribute__((used, visibility("default"))) \
    uint32_t upf_get_param_count() { \
        return static_cast<uint32_t>(_UPF_Adapter::param_count()); \
    } \
    __attribute__((used, visibility("default"))) \
    const char* upf_get_param_name(uint32_t index) { \
        return _UPF_Adapter::param_meta(index).name; \
    } \
    __attribute__((used, visibility("default"))) \
    float upf_get_param_min(uint32_t index) { \
        return _UPF_Adapter::param_meta(index).min; \
    } \
    __attribute__((used, visibility("default"))) \
    float upf_get_param_max(uint32_t index) { \
        return _UPF_Adapter::param_meta(index).max; \
    } \
    __attribute__((used, visibility("default"))) \
    float upf_get_param_default(uint32_t index) { \
        return _UPF_Adapter::param_meta(index).default_val; \
    } \
    __attribute__((used, visibility("default"))) \
    uint8_t upf_get_param_curve(uint32_t index) { \
        return static_cast<uint8_t>(_UPF_Adapter::param_meta(index).curve); \
    } \
    __attribute__((used, visibility("default"))) \
    const char* upf_get_param_unit(uint32_t index) { \
        return _UPF_Adapter::param_meta(index).unit; \
    } \
    } // extern "C"
