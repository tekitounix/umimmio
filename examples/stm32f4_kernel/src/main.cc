// SPDX-License-Identifier: MIT
// STM32F4-Discovery Kernel with Audio/USB Support
// Loads and runs .umiapp applications, handles audio I/O and USB MIDI

#include <cstdint>
#include <cstring>
#include <span>

// Kernel components
#include <app_header.hh>
#include <loader.hh>
#include <mpu_config.hh>

// Platform drivers (from stm32f4_synth)
#include <umios/backend/cm/stm32f4/rcc.hh>
#include <umios/backend/cm/stm32f4/gpio.hh>
#include <umios/backend/cm/stm32f4/i2c.hh>
#include <umios/backend/cm/stm32f4/i2s.hh>
#include <umios/backend/cm/stm32f4/cs43l22.hh>
#include <umios/backend/cm/common/nvic.hh>
#include <umios/backend/cm/common/scb.hh>

// USB stack (umiusb)
#include <umiusb.hh>
#include <audio_interface.hh>
#include <hal/stm32_otg.hh>

using namespace umi::stm32;
using namespace umi::port::arm;

// ============================================================================
// Linker-Provided Symbols
// ============================================================================

extern "C" {
    extern const uint8_t _app_image_start[];
    extern const uint8_t _app_image_end[];
    extern uint8_t _app_ram_start[];
    extern const uint8_t _app_ram_end[];
    extern const uint32_t _app_ram_size;
    extern uint8_t _shared_start[];
    extern const uint32_t _shared_size;
    extern uint32_t _estack;
    extern uint32_t _sidata;
    extern uint32_t _sdata;
    extern uint32_t _edata;
    extern uint32_t _sbss;
    extern uint32_t _ebss;
}

// ============================================================================
// Syscall Definitions
// ============================================================================

namespace umi::kernel::app_syscall {
    inline constexpr uint32_t Exit          = 0;
    inline constexpr uint32_t RegisterProc  = 1;
    inline constexpr uint32_t WaitEvent     = 2;
    inline constexpr uint32_t SendEvent     = 3;
    inline constexpr uint32_t PeekEvent     = 4;
    inline constexpr uint32_t GetTime       = 10;
    inline constexpr uint32_t Sleep         = 11;
    inline constexpr uint32_t Log           = 20;
    inline constexpr uint32_t Panic         = 21;
    inline constexpr uint32_t GetParam      = 30;
    inline constexpr uint32_t SetParam      = 31;
    inline constexpr uint32_t GetShared     = 40;
    inline constexpr uint32_t MidiSend      = 50;
    inline constexpr uint32_t MidiRecv      = 51;
}

// Debug: store last syscall info
volatile uint32_t g_debug_syscall_nr = 0xDEAD;
volatile uint32_t g_debug_syscall_arg0 = 0xBEEF;
volatile uint32_t g_debug_syscall_count = 0;
volatile uint32_t g_debug_sp = 0;
volatile uint32_t g_debug_lr = 0;

// ============================================================================
// Configuration
// ============================================================================

constexpr uint32_t SAMPLE_RATE = 48000;
constexpr uint32_t BUFFER_SIZE = 64;  // Samples per channel per buffer

// ============================================================================
// Hardware Instances
// ============================================================================

namespace {

GPIO gpio_a('A');
GPIO gpio_b('B');
GPIO gpio_c('C');
GPIO gpio_d('D');
I2C i2c1;
I2S i2s3;
DMA_I2S dma_i2s;
CS43L22 codec(i2c1);

// USB stack instances (umiusb)
umiusb::Stm32FsHal usb_hal;
umiusb::AudioFullDuplexMidi48k usb_audio;
umiusb::Device<umiusb::Stm32FsHal, decltype(usb_audio)> usb_device(
    usb_hal, usb_audio,
    {
        .vendor_id = 0x1209,
        .product_id = 0x0006,
        .device_version = 0x0300,  // Version 3.0 for kernel/app separation
        .manufacturer_idx = 1,
        .product_idx = 2,
        .serial_idx = 0,
    }
);

}  // namespace

// ============================================================================
// USB Descriptors
// ============================================================================

namespace usb_config {
using namespace umiusb::desc;

constexpr auto str_manufacturer = String("UMI-OS");
constexpr auto str_product = String("UMI Kernel Synth");

constexpr std::array<std::span<const uint8_t>, 2> string_table = {{
    {str_manufacturer.data.data(), str_manufacturer.size},
    {str_product.data.data(), str_product.size},
}};
}  // namespace usb_config

// ============================================================================
// Audio Buffers (DMA double-buffering)
// ============================================================================

// DMA buffers must be in SRAM, not CCM
__attribute__((section(".dma_buffer")))
int16_t audio_buf0[BUFFER_SIZE * 2];  // Stereo interleaved

__attribute__((section(".dma_buffer")))
int16_t audio_buf1[BUFFER_SIZE * 2];

// ============================================================================
// Global State
// ============================================================================

umi::kernel::AppLoader g_loader;

__attribute__((section(".shared")))
umi::kernel::SharedMemory g_shared;

volatile uint32_t g_current_buffer = 0;
volatile bool g_audio_ready = false;
volatile int16_t* g_active_buf = nullptr;

// MIDI queue (ISR -> App)
struct MidiMsg {
    uint8_t data[4];
    uint8_t len;
};
constexpr uint32_t MIDI_QUEUE_SIZE = 64;
MidiMsg g_midi_queue[MIDI_QUEUE_SIZE];
volatile uint32_t g_midi_write = 0;
volatile uint32_t g_midi_read = 0;

// Tick counter for GetTime syscall
volatile uint32_t g_tick_us = 0;

// ============================================================================
// PLLI2S Initialization (for 48kHz audio)
// ============================================================================

static void init_plli2s() {
    constexpr uint32_t RCC_PLLI2SCFGR = 0x40023884;
    constexpr uint32_t RCC_CR = 0x40023800;

    // Disable PLLI2S
    *reinterpret_cast<volatile uint32_t*>(RCC_CR) &= ~(1U << 26);

    // Configure: PLLI2SN=258, PLLI2SR=3
    // PLLI2SCLK = 1MHz * 258 / 3 = 86 MHz
    // Fs = 86MHz / [256 * 7] = 47,991 Hz (~48kHz)
    *reinterpret_cast<volatile uint32_t*>(RCC_PLLI2SCFGR) =
        (3U << 28) |   // PLLI2SR = 3
        (258U << 6);   // PLLI2SN = 258

    // Enable PLLI2S
    *reinterpret_cast<volatile uint32_t*>(RCC_CR) |= (1U << 26);

    // Wait for lock
    while (!(*reinterpret_cast<volatile uint32_t*>(RCC_CR) & (1U << 27))) {}
}

// ============================================================================
// GPIO Initialization
// ============================================================================

static void init_gpio() {
    RCC::enable_gpio('A');
    RCC::enable_gpio('B');
    RCC::enable_gpio('C');
    RCC::enable_gpio('D');
    RCC::enable_i2c1();
    RCC::enable_spi3();
    RCC::enable_dma1();
    RCC::enable_usb_otg_fs();

    // LEDs: PD12 (Green), PD13 (Orange), PD14 (Red), PD15 (Blue)
    gpio_d.config_output(12);
    gpio_d.config_output(13);
    gpio_d.config_output(14);
    gpio_d.config_output(15);

    // USER button: PA0
    gpio_a.set_mode(0, GPIO::MODE_INPUT);
    gpio_a.set_pupd(0, GPIO::PUPD_DOWN);

    // CS43L22 Reset: PD4
    gpio_d.config_output(4);
    gpio_d.reset(4);

    // I2C1: PB6 (SCL), PB9 (SDA)
    gpio_b.config_af(6, GPIO::AF4, GPIO::SPEED_FAST, GPIO::PUPD_UP, true);
    gpio_b.config_af(9, GPIO::AF4, GPIO::SPEED_FAST, GPIO::PUPD_UP, true);

    // I2S3 (Audio OUT): PC7 (MCK), PC10 (SCK), PC12 (SD), PA4 (WS)
    gpio_c.config_af(7, GPIO::AF6, GPIO::SPEED_HIGH);
    gpio_c.config_af(10, GPIO::AF6, GPIO::SPEED_HIGH);
    gpio_c.config_af(12, GPIO::AF6, GPIO::SPEED_HIGH);
    gpio_a.config_af(4, GPIO::AF6, GPIO::SPEED_HIGH);

    // USB OTG FS: PA11 (DM), PA12 (DP)
    gpio_a.config_af(11, GPIO::AF10, GPIO::SPEED_HIGH);
    gpio_a.config_af(12, GPIO::AF10, GPIO::SPEED_HIGH);
}

// ============================================================================
// Audio Initialization
// ============================================================================

static void init_audio() {
    i2c1.init();

    // Release CS43L22 from reset
    gpio_d.set(4);
    for (int i = 0; i < 100000; ++i) { asm volatile(""); }

    if (!codec.init()) {
        gpio_d.set(14);  // Red LED = error
        while (1) {}
    }

    init_plli2s();
    i2s3.init_48khz();

    __builtin_memset(audio_buf0, 0, sizeof(audio_buf0));
    __builtin_memset(audio_buf1, 0, sizeof(audio_buf1));

    dma_i2s.init(audio_buf0, audio_buf1, BUFFER_SIZE * 2, i2s3.dr_addr());

    // DMA1_Stream5 = IRQ 16, priority 5 (audio priority)
    NVIC::set_prio(16, 5);
    NVIC::enable(16);

    i2s3.enable_dma();
    i2s3.enable();
    dma_i2s.enable();
    codec.power_on();
    codec.set_volume(0);
}

// ============================================================================
// USB Initialization
// ============================================================================

static void init_usb() {
    for (int i = 0; i < 10000; ++i) { asm volatile(""); }

    usb_audio.on_streaming_change = [](bool streaming) {
        if (streaming) {
            gpio_d.set(15);   // Blue LED ON
        } else {
            gpio_d.reset(15);
        }
    };

    usb_audio.on_audio_in_change = [](bool streaming) {
        if (streaming) {
            gpio_d.set(13);   // Orange LED ON
        } else {
            gpio_d.reset(13);
        }
    };

    usb_audio.on_audio_rx = []() {
        // Audio OUT received - toggle indicator periodically
        static uint8_t cnt = 0;
        if (++cnt >= 48) {
            cnt = 0;
        }
    };
    
    // USB MIDI -> MIDI queue (ISR context)
    usb_audio.set_midi_callback([](uint8_t /*cable*/, const uint8_t* data, uint8_t len) {
        uint32_t next = (g_midi_write + 1) % MIDI_QUEUE_SIZE;
        if (next != g_midi_read) {  // Not full
            g_midi_queue[g_midi_write].len = (len > 4) ? 4 : len;
            for (uint8_t i = 0; i < g_midi_queue[g_midi_write].len; ++i) {
                g_midi_queue[g_midi_write].data[i] = data[i];
            }
            g_midi_write = next;
        }
    });

    usb_device.set_strings(usb_config::string_table);
    usb_device.init();
    usb_hal.connect();

    // OTG_FS = IRQ 67, priority 6 (lower than audio)
    NVIC::set_prio(67, 6);
    NVIC::enable(67);
}

// ============================================================================
// SysTick Initialization (1ms tick)
// ============================================================================

static void init_systick() {
    constexpr uint32_t SYST_CSR = 0xE000E010;
    constexpr uint32_t SYST_RVR = 0xE000E014;
    constexpr uint32_t SYST_CVR = 0xE000E018;
    
    // 168MHz / 168000 = 1kHz (1ms tick)
    *reinterpret_cast<volatile uint32_t*>(SYST_RVR) = 168000 - 1;
    *reinterpret_cast<volatile uint32_t*>(SYST_CVR) = 0;
    *reinterpret_cast<volatile uint32_t*>(SYST_CSR) = 0x07;  // Enable, IRQ, use core clock
}

// ============================================================================
// Interrupt Handlers
// ============================================================================

extern "C" void DMA1_Stream5_IRQHandler() {
    if (dma_i2s.transfer_complete()) {
        dma_i2s.clear_tc();
        g_active_buf = (dma_i2s.current_buffer() == 0) ? audio_buf1 : audio_buf0;
        g_audio_ready = true;
    }
}

extern "C" void OTG_FS_IRQHandler() {
    usb_device.poll();
}

extern "C" void SysTick_Handler() {
    g_tick_us += 1000;  // 1ms = 1000us
}

// Actual syscall handler implementation
void svc_handler_impl(uint32_t* sp) {
    using namespace umi::kernel::app_syscall;
    
    // Debug: light ALL LEDs when SVC is entered
    gpio_d.set(12);  // Green
    gpio_d.set(13);  // Orange
    gpio_d.set(14);  // Red
    gpio_d.set(15);  // Blue
    
    // Debug: store sp
    g_debug_sp = reinterpret_cast<uint32_t>(sp);
    
    // Syscall number is in r0 (sp[0]), arguments in r1-r4 (sp[1-4])
    uint32_t syscall_nr = sp[0];
    uint32_t arg0 = sp[1];
    uint32_t arg1 = sp[2];
    int32_t result = 0;
    
    // Debug: store syscall info
    g_debug_syscall_nr = syscall_nr;
    g_debug_syscall_arg0 = arg0;
    g_debug_syscall_count = g_debug_syscall_count + 1;
    
    switch (syscall_nr) {
        case Exit:
            // App requested exit - mark as terminated and return
            // The app's _start will loop, but kernel continues
            g_loader.terminate(static_cast<int>(arg0));
            result = 0;
            break;
            
        case RegisterProc:
            // Debug: red LED when RegisterProc is received
            gpio_d.set(14);  // Red LED - syscall received!
            g_loader.register_processor(reinterpret_cast<void*>(arg0));
            result = 0;
            break;
            
        case WaitEvent:
            // For now, just return immediately (non-blocking)
            result = 0;
            break;
            
        case GetTime:
            sp[0] = g_tick_us;
            return;
            
        case GetShared:
            sp[0] = reinterpret_cast<uint32_t>(&g_shared);
            return;
            
        case MidiRecv:
            // Return MIDI message if available
            if (g_midi_read != g_midi_write) {
                MidiMsg* out = reinterpret_cast<MidiMsg*>(arg0);
                *out = g_midi_queue[g_midi_read];
                g_midi_read = (g_midi_read + 1) % MIDI_QUEUE_SIZE;
                result = out->len;
            } else {
                result = 0;  // No data
            }
            break;
            
        case MidiSend:
            // Send MIDI via USB
            // arg0 = pointer to data, arg1 = length (1-3 bytes)
            if (arg1 >= 1 && arg1 <= 3) {
                const uint8_t* data = reinterpret_cast<const uint8_t*>(arg0);
                uint8_t status = data[0];
                uint8_t d1 = (arg1 >= 2) ? data[1] : 0;
                uint8_t d2 = (arg1 >= 3) ? data[2] : 0;
                usb_audio.send_midi(usb_hal, 0, status, d1, d2);
                result = 0;
            } else {
                result = -1;
            }
            break;
            
        case Log:
        case Sleep:
        case SendEvent:
        case PeekEvent:
        case GetParam:
        case SetParam:
        case Panic:
            result = 0;  // Not implemented yet
            break;
            
        default:
            result = -1;  // Unknown syscall
            break;
    }
    
    sp[0] = static_cast<uint32_t>(result);
}

// Naked SVC handler - must get SP before any stack operations
extern "C" [[gnu::naked]] void SVC_Handler() {
    __asm__ volatile(
        "tst lr, #4\n"           // Check bit 4 of LR (EXC_RETURN)
        "ite eq\n"
        "mrseq r0, msp\n"        // If 0: use MSP
        "mrsne r0, psp\n"        // If 1: use PSP
        "b %0\n"                 // Jump to C handler with sp in r0
        :
        : "i"(svc_handler_impl)
        :
    );
}

// ============================================================================
// Audio Processing Loop
// ============================================================================

static void fill_audio_buffer(int16_t* buf, uint32_t frame_count) {
    // Read from USB Audio OUT into buffer
    usb_audio.read_audio_asrc(buf, frame_count);
    
    // Copy USB audio to shared input buffer (convert int16 -> float)
    for (uint32_t i = 0; i < frame_count * 2; ++i) {
        g_shared.audio_input[i] = buf[i] / 32768.0f;
    }
    
    // Clear output buffer
    std::memset(g_shared.audio_output, 0, sizeof(float) * frame_count * 2);
    
    // Call app's process function
    g_loader.call_process(
        std::span<float>(g_shared.audio_output, frame_count * 2),
        std::span<const float>(g_shared.audio_input, frame_count * 2),
        g_shared.sample_position,
        frame_count,
        1.0f / SAMPLE_RATE
    );
    
    g_shared.sample_position += frame_count;
    
    // Convert output to int16 for I2S DMA
    for (uint32_t i = 0; i < frame_count * 2; ++i) {
        float sample = g_shared.audio_output[i];
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        buf[i] = static_cast<int16_t>(sample * 32767.0f);
    }
    
    // Send to USB Audio IN (for monitoring/recording)
    usb_audio.write_audio_in(buf, frame_count);
}

static void audio_loop() {
    while (true) {
        while (!g_audio_ready) {
            __asm__ volatile("wfi");
        }
        g_audio_ready = false;
        
        if (g_active_buf) {
            fill_audio_buffer(const_cast<int16_t*>(g_active_buf), BUFFER_SIZE);
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    RCC::init_168mhz();
    init_gpio();
    
    gpio_d.set(15);  // Blue LED - startup
    
    // DEBUG: Test audio init step by step
    init_audio();
    
    // Initialize shared memory
    g_shared.sample_rate = SAMPLE_RATE;
    g_shared.buffer_size = BUFFER_SIZE;
    g_shared.sample_position = 0;
    
    // Configure app loader
    g_loader.set_app_memory(_app_ram_start, reinterpret_cast<uintptr_t>(&_app_ram_size));
    g_loader.set_shared_memory(&g_shared);
    
    // Direct XIP execution: skip header, treat flash as raw binary
    // The app is linked to run directly from 0x08060000
    // Entry point is at the start of the binary with Thumb bit set
    using EntryFn = void (*)();
    auto app_entry = reinterpret_cast<EntryFn>(
        reinterpret_cast<uintptr_t>(_app_image_start) | 1  // Thumb bit
    );
    
    // Mark app as running (needed for syscalls)
    g_loader.set_entry(app_entry);  // Store for debugging
    
    // Call app entry point directly (runs _start -> main -> register_processor)
    app_entry();
    
    // Initialize USB
    init_usb();
    
    // Initialize SysTick
    init_systick();
    
    gpio_d.reset(15);  // Turn off blue
    gpio_d.set(12);    // Green LED - running
    
    // Enter audio processing loop
    audio_loop();
    
    return 0;
}

// ============================================================================
// Vector Table and Startup
// ============================================================================

extern "C" {
    [[noreturn]] void Reset_Handler();
    void NMI_Handler()        { while (true) {} }
    void HardFault_Handler()  { gpio_d.set(14); while (true) {} }
    void MemManage_Handler()  { gpio_d.set(14); while (true) {} }
    void BusFault_Handler()   { gpio_d.set(14); while (true) {} }
    void UsageFault_Handler() { gpio_d.set(14); while (true) {} }
    void DebugMon_Handler()   { while (true) {} }
    void PendSV_Handler()     { while (true) {} }
}

// Build full vector table (98 entries for STM32F407)
__attribute__((section(".isr_vector"), used))
const void* const g_pfnVectors[98] = {
    &_estack,                                  // 0: Initial SP
    reinterpret_cast<void*>(Reset_Handler),   // 1: Reset
    reinterpret_cast<void*>(NMI_Handler),     // 2: NMI
    reinterpret_cast<void*>(HardFault_Handler), // 3: HardFault
    reinterpret_cast<void*>(MemManage_Handler), // 4: MemManage
    reinterpret_cast<void*>(BusFault_Handler),  // 5: BusFault
    reinterpret_cast<void*>(UsageFault_Handler), // 6: UsageFault
    nullptr, nullptr, nullptr, nullptr,        // 7-10: Reserved
    reinterpret_cast<void*>(SVC_Handler),      // 11: SVCall
    reinterpret_cast<void*>(DebugMon_Handler), // 12: DebugMon
    nullptr,                                   // 13: Reserved
    reinterpret_cast<void*>(PendSV_Handler),   // 14: PendSV
    reinterpret_cast<void*>(SysTick_Handler),  // 15: SysTick
    // IRQ 0-15
    nullptr, nullptr, nullptr, nullptr,        // 16-19 (IRQ 0-3)
    nullptr, nullptr, nullptr, nullptr,        // 20-23 (IRQ 4-7)
    nullptr, nullptr, nullptr, nullptr,        // 24-27 (IRQ 8-11)
    nullptr, nullptr, nullptr, nullptr,        // 28-31 (IRQ 12-15)
    // IRQ 16-31
    reinterpret_cast<void*>(DMA1_Stream5_IRQHandler), // 32: DMA1_Stream5 (IRQ 16)
    nullptr, nullptr, nullptr,                 // 33-35 (IRQ 17-19)
    nullptr, nullptr, nullptr, nullptr,        // 36-39 (IRQ 20-23)
    nullptr, nullptr, nullptr, nullptr,        // 40-43 (IRQ 24-27)
    nullptr, nullptr, nullptr, nullptr,        // 44-47 (IRQ 28-31)
    // IRQ 32-47
    nullptr, nullptr, nullptr, nullptr,        // 48-51
    nullptr, nullptr, nullptr, nullptr,        // 52-55
    nullptr, nullptr, nullptr, nullptr,        // 56-59
    nullptr, nullptr, nullptr, nullptr,        // 60-63
    // IRQ 48-63
    nullptr, nullptr, nullptr, nullptr,        // 64-67
    nullptr, nullptr, nullptr, nullptr,        // 68-71
    nullptr, nullptr, nullptr, nullptr,        // 72-75
    nullptr, nullptr, nullptr, nullptr,        // 76-79
    // IRQ 64-66
    nullptr, nullptr, nullptr,                 // 80-82
    reinterpret_cast<void*>(OTG_FS_IRQHandler), // 83: OTG_FS (IRQ 67)
    // Remaining IRQs
    nullptr, nullptr, nullptr, nullptr,        // 84-87
    nullptr, nullptr, nullptr, nullptr,        // 88-91
    nullptr, nullptr, nullptr, nullptr,        // 92-95
    nullptr, nullptr,                          // 96-97
};

// C++ global constructors
extern "C" {
    extern void (*__init_array_start[])(void);
    extern void (*__init_array_end[])(void);
}

extern "C" [[noreturn]] void Reset_Handler() {
    // Enable FPU
    SCB::enable_fpu();
    __asm__ volatile("dsb\nisb" ::: "memory");
    
    // Copy .data from flash to RAM
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }
    
    // Zero .bss
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }
    
    // Call C++ global constructors (init_array)
    for (void (**p)(void) = __init_array_start; p < __init_array_end; ++p) {
        (*p)();
    }
    
    // Call main
    main();
    
    // Should not return
    while (true) {}
}
