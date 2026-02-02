// SPDX-License-Identifier: MIT
// Daisy Pod Kernel - umi::Kernel based RTOS
// 4-task architecture: Audio (REALTIME), Control (USER), Idle
#include <cstdint>
#include <cstring>
#include <atomic>
#include <common/irq.hh>

#include <arch/cache.hh>
#include <arch/context.hh>
#include <board/mcu_init.hh>
#include <board/audio.hh>
#include <board/usb.hh>
#include <board/sdram.hh>
#include <board/qspi.hh>
#include <board/midi_uart.hh>
#include <board/sdcard.hh>

// Pod HID
#include <board/hid.hh>

// umiusb: USB Audio + MIDI
#include <hal/stm32_otg.hh>
#include <audio/audio_interface.hh>
#include <core/device.hh>
#include <core/descriptor.hh>

// umios: Kernel, AudioContext, EventRouter, SharedState
#include <umios/kernel/umi_kernel.hh>
#include <umios/kernel/fpu_policy.hh>
#include <audio_context.hh>
#include <event.hh>
#include <event_router.hh>
#include <shared_state.hh>

// Local arch layer
#include "arch.hh"

// Linker-provided symbols
extern "C" {
extern std::uint32_t _estack;
extern std::uint32_t _sidata;
extern std::uint32_t _sdata;
extern std::uint32_t _edata;
extern std::uint32_t _sbss;
extern std::uint32_t _ebss;
extern std::uint32_t _sdtcm_bss;
extern std::uint32_t _edtcm_bss;
}

// ============================================================================
// Hardware Abstraction (Hw<Impl>)
// ============================================================================

namespace {

namespace arch = umi::arch::cm7;
using namespace umi::daisy;

// SysTick tick counter (incremented by SysTick_Handler)
volatile umi::usec g_tick_us = 0;
constexpr std::uint32_t SYSTICK_PERIOD_US = 1000;  // 1ms tick

struct Stm32H7Hw {
    static void set_timer_absolute(umi::usec) {}
    static umi::usec monotonic_time_usecs() { return g_tick_us; }

    // BASEPRI-based critical section.
    // Masks priority >= 1 (BASEPRI = 0x20 on STM32H7 with 4-bit priority).
    // Audio DMA (priority 0) is NOT masked — runs through critical sections.
    static void enter_critical() {
        __asm__ volatile("msr basepri, %0" ::"r"(0x20u) : "memory");
    }
    static void exit_critical() {
        __asm__ volatile("msr basepri, %0" ::"r"(0u) : "memory");
    }

    static void trigger_ipi(std::uint8_t) {}
    static std::uint8_t current_core() { return 0; }

    static void request_context_switch() { arch::request_context_switch(); }

    static void enter_sleep() {
        __asm__ volatile("msr basepri, %0\n"
                         "wfi\n"
                         "msr basepri, %0\n" ::"r"(0u)
                         : "memory");
    }
    static std::uint32_t cycle_count() { return arch::dwt_cycle(); }
    static std::uint32_t cycles_per_usec() { return 480; }  // STM32H750 @ 480 MHz
};

using HW = umi::Hw<Stm32H7Hw>;

// ============================================================================
// Kernel Instance
// ============================================================================

umi::Kernel<8, 4, HW, 1> g_kernel;

// Compile-time FPU policy determination
constexpr umi::TaskFpuDecl fpu_decl {
    .audio   = true,
    .system  = false,
    .control = true,
    .idle    = false,
};
constexpr int fpu_task_count = umi::count_fpu_tasks(fpu_decl);
constexpr auto audio_fpu_policy   = umi::resolve_fpu_policy(fpu_decl.audio,   fpu_task_count);
constexpr auto control_fpu_policy = umi::resolve_fpu_policy(fpu_decl.control, fpu_task_count);
constexpr auto idle_fpu_policy    = umi::resolve_fpu_policy(fpu_decl.idle,    fpu_task_count);

umi::TaskId g_audio_task_id;
umi::TaskId g_control_task_id;
umi::TaskId g_idle_task_id;

// ============================================================================
// Task stacks and hardware TCBs
// ============================================================================

constexpr uint32_t AUDIO_TASK_STACK_SIZE = 2048;
constexpr uint32_t CONTROL_TASK_STACK_SIZE = 2048;
constexpr uint32_t IDLE_TASK_STACK_SIZE = 128;

uint32_t g_audio_task_stack[AUDIO_TASK_STACK_SIZE];
uint32_t g_control_task_stack[CONTROL_TASK_STACK_SIZE];
uint32_t g_idle_task_stack[IDLE_TASK_STACK_SIZE];

arch::TaskContext g_audio_tcb;
arch::TaskContext g_control_tcb;
arch::TaskContext g_idle_tcb;
arch::TaskContext* g_current_tcb = nullptr;

// Map kernel TaskId to hardware TCB pointer
arch::TaskContext* task_id_to_hw_tcb(std::uint16_t idx) {
    if (idx == g_audio_task_id.value) return &g_audio_tcb;
    if (idx == g_control_task_id.value) return &g_control_tcb;
    if (idx == g_idle_task_id.value) return &g_idle_tcb;
    return nullptr;
}

// ============================================================================
// SpscQueue for DMA ISR → audio task
// ============================================================================

struct AudioBuffer {
    std::int32_t* tx;
    std::int32_t* rx;
};

umi::SpscQueue<AudioBuffer, 4> g_audio_ready_queue;

// ============================================================================
// USB MIDI
// ============================================================================

umiusb::Stm32HsHal usb_hal;
using UsbAudioMidi = umiusb::AudioFullDuplexMidi48k;
UsbAudioMidi usb_audio;

constexpr umiusb::DeviceInfo usb_device_info = {
    .vendor_id = 0x1209,
    .product_id = 0x000B,
    .device_version = 0x0100,
    .manufacturer_idx = 1,
    .product_idx = 2,
    .serial_idx = 0,
};

inline constexpr auto str_mfr = umiusb::StringDesc("UMI");
inline constexpr auto str_prod = umiusb::StringDesc("Daisy Pod Audio");

inline const std::array<std::span<const uint8_t>, 2> usb_strings = {
    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&str_mfr), str_mfr.size()),
    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&str_prod), str_prod.size()),
};

umiusb::Device<umiusb::Stm32HsHal, UsbAudioMidi> usb_device(usb_hal, usb_audio, usb_device_info);

// ============================================================================
// Pod HID
// ============================================================================

umi::daisy::pod::PodHid pod_hid;

// MIDI UART parser
umi::daisy::MidiUartParser midi_uart_parser;

// ============================================================================
// DMA audio buffers (D2 SRAM — non-cacheable via MPU)
// ============================================================================

__attribute__((section(".sram_d2_bss")))
std::int32_t audio_tx_buf[AUDIO_BUFFER_SIZE];

__attribute__((section(".sram_d2_bss")))
std::int32_t audio_rx_buf[AUDIO_BUFFER_SIZE];

// Debug area in DTCM (no D-Cache, directly visible to pyOCD)
__attribute__((section(".dtcmram_bss"), used))
volatile std::uint32_t d2_dbg[16];

// ============================================================================
// umios: AudioContext infrastructure
// ============================================================================

[[maybe_unused]] float audio_float_in[2][AUDIO_BLOCK_SIZE];
[[maybe_unused]] float audio_float_out[2][AUDIO_BLOCK_SIZE];

umi::EventQueue<> audio_event_queue;
umi::EventQueue<> audio_output_events;
[[maybe_unused]] umi::SharedParamState shared_params;
[[maybe_unused]] umi::SharedChannelState shared_channel;
[[maybe_unused]] umi::SharedInputState shared_input;
umi::EventRouter event_router;

umi::sample_position_t audio_sample_pos = 0;

// ============================================================================
// Simple Event Queue (HID → audio/control event bridge)
// ============================================================================

enum class EventType : std::uint8_t {
    NONE = 0,
    NOTE_ON,
    NOTE_OFF,
    CC,
    KNOB,
    BUTTON_DOWN,
    BUTTON_UP,
    ENCODER_INCREMENT,
};

struct Event {
    EventType type = EventType::NONE;
    std::uint8_t channel = 0;
    std::uint8_t param = 0;
    std::uint8_t value = 0;
};

struct EventQueue {
    static constexpr std::uint32_t CAPACITY = 64;
    Event events[CAPACITY] = {};
    std::atomic<std::uint32_t> head{0};
    std::atomic<std::uint32_t> tail{0};

    bool push(const Event& e) {
        auto h = head.load(std::memory_order_relaxed);
        auto next = (h + 1) % CAPACITY;
        if (next == tail.load(std::memory_order_acquire)) return false;
        events[h] = e;
        head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(Event& e) {
        auto t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false;
        e = events[t];
        tail.store((t + 1) % CAPACITY, std::memory_order_release);
        return true;
    }
};

// Place in D2 SRAM (non-cacheable) to avoid D-Cache coherency issues
// between control_task (USB poll → push) and audio_task (pop)
EventQueue midi_event_queue;

// ============================================================================
// Minimal Polyphonic Synth (8 voices, saw oscillator)
// ============================================================================

struct Voice {
    std::uint32_t phase = 0;
    std::uint32_t phase_inc = 0;
    std::int32_t amplitude = 0;
    std::uint8_t note = 0;
    bool active = false;

    std::int32_t env_level = 0;
    std::int32_t env_target = 0;
    std::int32_t env_rate = 0;
};

constexpr int NUM_VOICES = 8;
Voice voices[NUM_VOICES];

constexpr std::uint32_t midi_note_to_phase_inc(std::uint8_t note) {
    constexpr std::uint32_t a4_inc = 39370534U;
    if (note == 69) return a4_inc;
    constexpr std::uint32_t semitone_ratio[12] = {
        65536, 69433, 73562, 77936, 82570, 87480,
        92682, 98193, 104032, 110218, 116772, 123715,
    };
    int semitone = note % 12;
    int octave = note / 12;
    std::uint64_t inc = static_cast<std::uint64_t>(a4_inc) * semitone_ratio[semitone] / semitone_ratio[9];
    int shift = octave - 5;
    if (shift > 0) inc <<= shift;
    else if (shift < 0) inc >>= (-shift);
    return static_cast<std::uint32_t>(inc);
}

volatile float synth_volume = 0.5f;

// MIDI callback — called from USB poll context
void on_midi_message(std::uint8_t cable, const std::uint8_t* data, std::uint8_t len) {
    d2_dbg[6] = d2_dbg[6] + 1;
    // Debug: store last MIDI message bytes in d2_dbg[7]
    d2_dbg[7] = (static_cast<std::uint32_t>(len) << 24)
              | (static_cast<std::uint32_t>(data[0]) << 16)
              | (static_cast<std::uint32_t>(len >= 2 ? data[1] : 0) << 8)
              | (static_cast<std::uint32_t>(len >= 3 ? data[2] : 0));
    if (len < 2) return;
    std::uint8_t status = data[0] & 0xF0;
    std::uint8_t ch = data[0] & 0x0F;

    Event ev;
    ev.channel = ch;

    if (status == 0x90 && len >= 3 && data[2] > 0) {
        ev.type = EventType::NOTE_ON;
        ev.param = data[1];
        ev.value = data[2];
        midi_event_queue.push(ev);
    } else if (status == 0x80 || (status == 0x90 && len >= 3 && data[2] == 0)) {
        ev.type = EventType::NOTE_OFF;
        ev.param = data[1];
        ev.value = 0;
        midi_event_queue.push(ev);
    } else if (status == 0xB0 && len >= 3) {
        ev.type = EventType::CC;
        ev.param = data[1];
        ev.value = data[2];
        midi_event_queue.push(ev);
    }

    umi::RawInput raw;
    raw.hw_timestamp = 0;
    raw.source_id = cable;
    raw.size = len;
    for (std::uint8_t i = 0; i < len && i < 6; ++i) {
        raw.payload[i] = data[i];
    }
    event_router.receive(raw, 0, AUDIO_SAMPLE_RATE);
}

void process_midi_events() {
    Event ev;
    while (midi_event_queue.pop(ev)) {
        d2_dbg[9] = d2_dbg[9] + 1;  // pop count
        d2_dbg[10] = static_cast<std::uint32_t>(ev.type);  // last event type
        if (ev.type == EventType::NOTE_ON) {
            Voice* target = nullptr;
            for (auto& v : voices) {
                if (!v.active) { target = &v; break; }
            }
            if (target == nullptr) target = &voices[0];

            target->note = ev.param;
            target->phase = 0;
            target->phase_inc = midi_note_to_phase_inc(ev.param);
            target->amplitude = static_cast<std::int32_t>(ev.value) << 16;
            target->active = true;
            target->env_level = 0;
            target->env_target = target->amplitude;
            target->env_rate = target->amplitude / 48;
        } else if (ev.type == EventType::NOTE_OFF) {
            for (auto& v : voices) {
                if (v.active && v.note == ev.param) {
                    v.env_target = 0;
                    v.env_rate = -(v.env_level / 480);
                    if (v.env_rate == 0) v.env_rate = -1;
                    break;
                }
            }
        }
    }
}

void render_synth(std::int32_t* out, std::uint32_t frames) {
    for (std::uint32_t i = 0; i < frames * 2; ++i) {
        out[i] = 0;
    }

    float vol = synth_volume;

    for (auto& v : voices) {
        if (!v.active) continue;

        for (std::uint32_t i = 0; i < frames; ++i) {
            if (v.env_rate > 0 && v.env_level < v.env_target) {
                v.env_level += v.env_rate;
                if (v.env_level > v.env_target) v.env_level = v.env_target;
            } else if (v.env_rate < 0) {
                v.env_level += v.env_rate;
                if (v.env_level <= 0) {
                    v.env_level = 0;
                    v.active = false;
                    break;
                }
            }

            std::int32_t saw = static_cast<std::int32_t>(v.phase >> 8) - (1 << 23);
            std::int32_t sample = static_cast<std::int32_t>(
                (static_cast<std::int64_t>(saw) * v.env_level >> 24) * static_cast<std::int64_t>(vol * 256) >> 8
            );

            out[i * 2] += sample;
            out[i * 2 + 1] += sample;
            if (i == 0) d2_dbg[13] = static_cast<std::uint32_t>(sample);
            v.phase += v.phase_inc;
        }
    }
}

// ============================================================================
// Task entry functions
// ============================================================================

/// Audio task: waits on kernel event, processes MIDI→synth→DMA output
void audio_task_entry(void* /*arg*/) {
    while (true) {
        // Block until DMA ISR signals audio ready
        g_kernel.wait_block(g_audio_task_id, umi::KernelEvent::audio);

        auto buf = g_audio_ready_queue.try_pop();
        if (!buf.has_value()) continue;

        auto* out = buf->tx;
        d2_dbg[0] = d2_dbg[0] + 1;

        process_midi_events();

        bool has_active_voice = false;
        for (const auto& v : voices) {
            if (v.active) { has_active_voice = true; break; }
        }

        if (has_active_voice) {
            d2_dbg[8] = d2_dbg[8] + 1;  // render count
            render_synth(out, AUDIO_BLOCK_SIZE);
            d2_dbg[11] = static_cast<std::uint32_t>(out[0]);  // first sample
            d2_dbg[12] = static_cast<std::uint32_t>(out[1]);  // second sample
        } else {
            // No active synth — passthrough USB Audio OUT to SAI TX
            usb_audio.read_audio(out, AUDIO_BLOCK_SIZE);
        }

        // USB Audio IN: send output to host
        usb_audio.write_audio_in(out, AUDIO_BLOCK_SIZE);

        // D-Cache clean: flush TX buffer to SRAM so DMA can read it
        {
            auto addr = reinterpret_cast<std::uintptr_t>(out);
            auto end = addr + (AUDIO_BLOCK_SIZE * 2) * sizeof(std::int32_t);
            for (; addr < end; addr += 32) {
                *umi::cm7::scb::DCCMVAC = static_cast<std::uint32_t>(addr);
            }
            __asm__ volatile("dsb sy" ::: "memory");
        }

        audio_sample_pos += AUDIO_BLOCK_SIZE;
    }
}

/// Control task: HID polling + USB polling + knob→synth parameter mapping
void control_task_entry(void* /*arg*/) {
    mm::DirectTransportT<> transport;
    constexpr float hid_rate = 1000.0f;
    std::uint32_t loop_counter = 0;

    while (true) {
        d2_dbg[5] = d2_dbg[5] + 1;
        usb_device.poll();

        if (++loop_counter >= 100) {
            loop_counter = 0;
            pod_hid.update_controls(transport, hid_rate);

            float knob_val = pod_hid.knobs.value(0);
            synth_volume = (knob_val > 0.01f) ? knob_val : 0.5f;
            pod_hid.led1.set(synth_volume, 0.0f, 0.0f);
            pod_hid.led2.set(0.0f, 0.0f, pod_hid.knobs.value(1));

            if (pod_hid.encoder.click_just_pressed()) {
                umi::daisy::toggle_led();
            }

            if (pod_hid.encoder.click_just_pressed()) {
                midi_event_queue.push({EventType::BUTTON_DOWN, 0, 0, 127});
            }
            if (pod_hid.encoder.increment() != 0) {
                midi_event_queue.push({EventType::ENCODER_INCREMENT, 0, 0,
                    static_cast<std::uint8_t>(pod_hid.encoder.increment() > 0 ? 1 : 0xFF)});
            }
        }

        pod_hid.process_knobs();

        // Yield to let other tasks run
        arch::yield();
    }
}

/// Idle task: low power wait
void idle_task_entry(void*) {
    while (true) {
        asm volatile("wfi");
    }
}

// ============================================================================
// Kernel callbacks
// ============================================================================

static void tick_callback() {
    g_tick_us += SYSTICK_PERIOD_US;
    // Resume control task periodically
    g_kernel.resume_task(g_control_task_id);
}

static void switch_context_callback() {
    Stm32H7Hw::enter_critical();
    g_kernel.resolve_pending();
    auto next_opt = g_kernel.get_next_task();
    if (next_opt.has_value()) {
        g_kernel.prepare_switch(*next_opt);
        auto* next_hw_tcb = task_id_to_hw_tcb(*next_opt);
        g_current_tcb = next_hw_tcb;
        arch::current_tcb = next_hw_tcb;
    }
    Stm32H7Hw::exit_critical();
}

static void svc_callback(uint32_t* sp) {
    // Extract syscall number from R12 (saved in stack frame)
    // Stack layout: R0, R1, R2, R3, R12, LR, PC, xPSR
    [[maybe_unused]] uint32_t syscall_num = sp[4];  // R12

    // For now, yield is the only syscall
    g_kernel.yield();
}

// ============================================================================
// start_rtos
// ============================================================================

void start_rtos() {
    // Create tasks in Kernel
    g_audio_task_id = g_kernel.create_task({
        .entry = audio_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::REALTIME,
        .uses_fpu = fpu_decl.audio,
        .name = "audio",
    });

    g_control_task_id = g_kernel.create_task({
        .entry = control_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::USER,
        .uses_fpu = fpu_decl.control,
        .name = "control",
    });

    g_idle_task_id = g_kernel.create_task({
        .entry = idle_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::IDLE,
        .uses_fpu = fpu_decl.idle,
        .name = "idle",
    });

    // Initialize hardware TCBs (port layer)
    arch::init_task<audio_fpu_policy>(
        g_audio_tcb, g_audio_task_stack, AUDIO_TASK_STACK_SIZE, audio_task_entry, nullptr);

    arch::init_task<control_fpu_policy>(
        g_control_tcb, g_control_task_stack, CONTROL_TASK_STACK_SIZE, control_task_entry, nullptr);

    arch::init_task<idle_fpu_policy>(
        g_idle_tcb, g_idle_task_stack, IDLE_TASK_STACK_SIZE, idle_task_entry, nullptr);

    // Control task starts as Running
    g_kernel.prepare_switch(g_control_task_id.value);
    g_current_tcb = &g_control_tcb;

    // Set arch layer callbacks
    arch::set_tick_callback(tick_callback);
    arch::set_switch_context_callback(switch_context_callback);
    arch::set_svc_callback(svc_callback);

    // Initialize SysTick (1ms tick)
    arch::init_cycle_counter();
    arch::init_systick(480'000'000, SYSTICK_PERIOD_US);

    // Start scheduler (does not return)
    uint32_t* control_stack_top = g_control_task_stack + CONTROL_TASK_STACK_SIZE;
    arch::start_scheduler(&g_control_tcb, control_task_entry, nullptr, control_stack_top);
}

} // namespace

// ============================================================================
// Debug: SDRAM/QSPI/SD test results
// ============================================================================

struct DbgMemTest {
    volatile std::uint32_t sdram_result = 0;
    volatile std::uint32_t sdram_read = 0;
    volatile std::uint32_t qspi_byte0 = 0;
    volatile std::uint32_t sd_result = 0;
    volatile std::uint32_t sd_byte0 = 0;
    volatile std::uint32_t dma_isr_count = 0;
    volatile std::uint32_t dma_lisr_last = 0;
} dbg_mem;

// ============================================================================
// DMA IRQ handlers
// ============================================================================

extern "C" {

void DMA1_Stream0_IRQHandler() {
    using namespace umi::stm32h7;
    mm::DirectTransportT<> transport;
    auto lisr = transport.read(DMA1::LISR{});

    d2_dbg[1] = d2_dbg[1] + 1;
    d2_dbg[2] = lisr;

    AudioBuffer buf{};

    if (lisr & dma_flags::S0_HTIF) {
        transport.write(DMA1::LIFCR::value(dma_flags::S0_HTIF));
        buf.tx = audio_tx_buf;
        buf.rx = audio_rx_buf;
        d2_dbg[3] = d2_dbg[3] + 1;
    }
    if (lisr & dma_flags::S0_TCIF) {
        transport.write(DMA1::LIFCR::value(dma_flags::S0_TCIF));
        buf.tx = audio_tx_buf + AUDIO_BUFFER_SIZE / 2;
        buf.rx = audio_rx_buf + AUDIO_BUFFER_SIZE / 2;
        d2_dbg[4] = d2_dbg[4] + 1;
    }
    transport.write(DMA1::LIFCR::value(lisr & dma_flags::S0_ALL));

    if (buf.tx != nullptr) {
        // D-Cache invalidate RX buffer so CPU reads fresh DMA data
        {
            auto addr = reinterpret_cast<std::uintptr_t>(buf.rx);
            auto end = addr + (AUDIO_BUFFER_SIZE / 2) * sizeof(std::int32_t);
            for (; addr < end; addr += 32) {
                *umi::cm7::scb::DCIMVAC = static_cast<std::uint32_t>(addr);
            }
            __asm__ volatile("dsb sy" ::: "memory");
        }
        g_audio_ready_queue.try_push(buf);
        // signal() is lock-free, safe from any ISR
        g_kernel.signal(g_audio_task_id, umi::KernelEvent::audio);
        // Trigger PendSV for context switch
        umi::port::arm::SCB::trigger_pendsv();
    }
}

void DMA1_Stream1_IRQHandler() {
    using namespace umi::stm32h7;
    mm::DirectTransportT<> transport;
    transport.write(DMA1::LIFCR::value(dma_flags::S1_ALL));
}

/// USART1 IRQ: MIDI UART receive (31250 baud)
void USART1_IRQHandler() {
    using namespace umi::stm32h7;
    mm::DirectTransportT<> t;
    auto isr = t.read(USART1::ISR{});

    if (isr & ((1U << 3) | (1U << 1))) {
        umi::daisy::midi_uart_clear_errors();
    }

    if (isr & (1U << 5)) {
        auto byte = umi::daisy::midi_uart_read_byte();
        if (midi_uart_parser.feed(byte)) {
            std::uint8_t msg[3] = {midi_uart_parser.running_status,
                                    midi_uart_parser.data[0],
                                    midi_uart_parser.data[1]};
            on_midi_message(0, msg, midi_uart_parser.expected + 1);
        }
    }
}

} // extern "C"

// Fault handlers
extern "C" {
void HardFault_Handler() {
    // Extract faulting PC from exception frame
    // Determine which stack was in use from EXC_RETURN in LR
    uint32_t* sp;
    __asm__ volatile("tst lr, #4\n"
                     "ite eq\n"
                     "mrseq %0, msp\n"
                     "mrsne %0, psp\n"
                     : "=r"(sp));
    // Exception frame: R0,R1,R2,R3,R12,LR,PC,xPSR at sp[0..7]
    d2_dbg[0] = 0xDEAD0001;  // marker
    d2_dbg[1] = sp[5];       // LR at fault
    d2_dbg[2] = sp[6];       // PC at fault
    d2_dbg[3] = sp[7];       // xPSR
    d2_dbg[4] = *reinterpret_cast<volatile uint32_t*>(0xE000ED28); // CFSR
    d2_dbg[5] = *reinterpret_cast<volatile uint32_t*>(0xE000ED38); // BFAR
    d2_dbg[6] = reinterpret_cast<uint32_t>(sp);  // stack pointer
    d2_dbg[7] = sp[0];       // R0
    umi::daisy::set_led(true);
    while (true) {}
}
void MemManage_Handler() {
    umi::daisy::set_led(true);
    while (true) {}
}
void BusFault_Handler() {
    umi::daisy::set_led(true);
    while (true) {}
}
void UsageFault_Handler() {
    umi::daisy::set_led(true);
    while (true) {}
}
}

// ============================================================================
// main() — called from Reset_Handler
// ============================================================================

int main() {
    // Initialize D2 SRAM variables (NOLOAD section, not zero-init'd)
    for (auto& d : d2_dbg) d = 0;
    // midi_event_queue is in .bss (zero-initialized by startup code)

    // Initialize clocks
    umi::daisy::init_clocks();
    umi::daisy::init_pll3();
    umi::daisy::init_led();

    // External memory
    umi::daisy::init_sdram();
    umi::daisy::init_qspi();

    // SDRAM verification
    {
        auto* sdram = reinterpret_cast<volatile std::uint32_t*>(0xC000'0000);
        sdram[0] = 0xDEAD'BEEF;
        asm volatile("dsb sy" ::: "memory");
        dbg_mem.sdram_read = sdram[0];
        dbg_mem.sdram_result = (dbg_mem.sdram_read == 0xDEAD'BEEF) ? 1 : 2;
    }

    // QSPI XIP verification
    {
        auto* qspi = reinterpret_cast<volatile std::uint8_t*>(0x9000'0000);
        dbg_mem.qspi_byte0 = qspi[0];
    }

    // Detect board version and initialize codec
    auto board_ver = umi::daisy::detect_board_version();
    umi::daisy::init_codec(board_ver);

    // Initialize audio subsystem
    umi::daisy::init_sai_gpio();
    umi::daisy::init_sai();
    umi::daisy::init_audio_dma(audio_tx_buf, audio_rx_buf, AUDIO_BUFFER_SIZE);

    // USB MIDI
    umi::daisy::init_usb();
    umiusb::configure_hs_internal_phy();
    usb_audio.set_midi_callback(on_midi_message);
    usb_device.set_strings(usb_strings);
    usb_device.init();
    usb_hal.connect();

    // Pod HID: enable ADC12 clock, then initialize all controls
    {
        mm::DirectTransportT<> transport;
        transport.modify(umi::stm32h7::RCC::D3CCIPR::ADCSEL::value(2));
        transport.modify(umi::stm32h7::RCC::AHB1ENR::ADC12EN::Set{});
        [[maybe_unused]] auto dummy = transport.read(umi::stm32h7::RCC::AHB1ENR{});
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOAEN::Set{});
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOBEN::Set{});
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOCEN::Set{});
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIODEN::Set{});
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOGEN::Set{});
        dummy = transport.read(umi::stm32h7::RCC::AHB4ENR{});

        constexpr float update_rate = static_cast<float>(AUDIO_SAMPLE_RATE) / AUDIO_BLOCK_SIZE;
        pod_hid.init(transport, update_rate);
    }

    // MIDI UART (USART1, 31250 baud)
    umi::daisy::init_midi_uart();

    // Start audio DMA (ISR-driven)
    umi::daisy::start_audio();

    // Set PendSV and SysTick to lowest priority
    umi::port::arm::SCB::set_exc_prio(14, 0xFF);  // PendSV
    umi::port::arm::SCB::set_exc_prio(15, 0xFF);  // SysTick

    // Start RTOS (does not return)
    start_rtos();

    while (true) {}
}

// ============================================================================
// Boot Vector Table (Flash) - only 2 entries
// ============================================================================

extern "C" [[noreturn]] void Reset_Handler();

__attribute__((section(".isr_vector"), used))
const void* const g_boot_vectors[2] = {
    reinterpret_cast<const void*>(&_estack),
    reinterpret_cast<const void*>(Reset_Handler),
};

extern "C" {
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
}

// Forward declarations for arch.cc handlers
extern "C" void PendSV_Handler();
extern "C" void SVC_Handler();
extern "C" void SysTick_Handler();

extern "C" [[noreturn]] void Reset_Handler() {
    umi::cm7::enable_fpu();
    asm volatile("dsb\nisb" ::: "memory");

    // AXI SRAM workaround (STM32H7 errata for Rev Y silicon)
    if ((*reinterpret_cast<volatile std::uint32_t*>(0x5C001000) & 0xFFFF0000U) < 0x20000000U) {
        *reinterpret_cast<volatile std::uint32_t*>(0x51008108) = 0x00000001U;
    }

    umi::cm7::configure_mpu();
    umi::cm7::enable_icache();
    // D-Cache disabled for now — D2 SRAM DMA coherency debugging
    // umi::cm7::enable_dcache();

    std::uint32_t* src = &_sidata;
    std::uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    dst = &_sdtcm_bss;
    while (dst < &_edtcm_bss) {
        *dst++ = 0;
    }

    umi::irq::init();

    namespace exc = umi::backend::cm::exc;
    umi::irq::set_handler(exc::HardFault, HardFault_Handler);
    umi::irq::set_handler(exc::MemManage, MemManage_Handler);
    umi::irq::set_handler(exc::BusFault, BusFault_Handler);
    umi::irq::set_handler(exc::UsageFault, UsageFault_Handler);

    // PendSV, SVC, SysTick — use arch.cc handlers
    umi::irq::set_handler(exc::PendSV, PendSV_Handler);
    umi::irq::set_handler(exc::SVCall, SVC_Handler);
    umi::irq::set_handler(exc::SysTick, SysTick_Handler);

    // DMA1 Stream 0/1
    umi::irq::set_handler(11, DMA1_Stream0_IRQHandler);
    umi::irq::set_handler(12, DMA1_Stream1_IRQHandler);
    umi::irq::set_priority(11, 0x00);
    umi::irq::set_priority(12, 0x00);
    umi::irq::enable(11);
    umi::irq::enable(12);

    // USART1 (MIDI UART RX)
    umi::irq::set_handler(37, USART1_IRQHandler);
    umi::irq::set_priority(37, 0x40);
    umi::irq::enable(37);

    for (void (**p)(void) = __init_array_start; p < __init_array_end; ++p) {
        (*p)();
    }

    main();
    while (true) {}
}
