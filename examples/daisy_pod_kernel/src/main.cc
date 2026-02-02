// SPDX-License-Identifier: MIT
// Daisy Pod Kernel - Phase 5: Audio + RTOS + USB MIDI + QSPI + SDRAM
// 4-task architecture: Audio (REALTIME), System (SERVER), Control (USER), Idle
#include <cstdint>
#include <atomic>
#include <common/irq.hh>

#include <arch/cache.hh>
#include <arch/context.hh>
#include <arch/handlers.hh>
#include <arch/switch.hh>
#include <board/mcu_init.hh>
#include <board/audio.hh>
#include <board/usb.hh>
#include <board/sdram.hh>
#include <board/qspi.hh>

// Pod HID
#include <board/hid.hh>

// umiusb: USB Audio + MIDI
#include <hal/stm32_otg.hh>
#include <audio/audio_interface.hh>
#include <core/device.hh>
#include <core/descriptor.hh>

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
// Task definitions
// ============================================================================

namespace {

using namespace umi::daisy;
using TaskContext = umi::port::cm7::TaskContext;

// Task IDs
enum TaskId : std::uint8_t {
    TASK_AUDIO   = 0,  // Priority: REALTIME
    TASK_SYSTEM  = 1,  // Priority: SERVER
    TASK_CONTROL = 2,  // Priority: USER
    TASK_IDLE    = 3,
    NUM_TASKS    = 4,
};

// Task stacks (in words)
constexpr std::uint32_t AUDIO_STACK_SIZE   = 512;
constexpr std::uint32_t SYSTEM_STACK_SIZE  = 256;
constexpr std::uint32_t CONTROL_STACK_SIZE = 256;
constexpr std::uint32_t IDLE_STACK_SIZE    = 64;

std::uint32_t audio_stack[AUDIO_STACK_SIZE];
std::uint32_t system_stack[SYSTEM_STACK_SIZE];
std::uint32_t control_stack[CONTROL_STACK_SIZE];
std::uint32_t idle_stack[IDLE_STACK_SIZE];

TaskContext task_contexts[NUM_TASKS];

// Minimal scheduler state
volatile TaskId current_task = TASK_IDLE;

// Task notification bits (set by ISR, cleared by task)
std::atomic<std::uint32_t> task_notifications[NUM_TASKS] = {};

// Notification bits
constexpr std::uint32_t NOTIFY_AUDIO_READY = (1U << 0);
[[maybe_unused]] constexpr std::uint32_t NOTIFY_USB_IRQ = (1U << 1);

// ============================================================================
// USB MIDI
// ============================================================================

// USB device uses HS peripheral with internal FS PHY (Stm32HsHal)
umiusb::Stm32HsHal usb_hal;
using UsbAudioMidi = umiusb::AudioFullDuplexMidi48k;
UsbAudioMidi usb_audio;

constexpr umiusb::DeviceInfo usb_device_info = {
    .vendor_id = 0x1209,       // pid.codes test VID
    .product_id = 0x000B,      // UMI Audio+MIDI
    .device_version = 0x0100,
    .manufacturer_idx = 1,
    .product_idx = 2,
    .serial_idx = 0,
};

inline constexpr auto str_mfr = umiusb::StringDesc("UMI");
inline constexpr auto str_prod = umiusb::StringDesc("Daisy Pod Audio");

[[maybe_unused]] inline const std::array<std::span<const uint8_t>, 2> usb_strings = {
    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&str_mfr), str_mfr.size()),
    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&str_prod), str_prod.size()),
};

umiusb::Device<umiusb::Stm32HsHal, UsbAudioMidi> usb_device(usb_hal, usb_audio, usb_device_info);

// ============================================================================
// Pod HID
// ============================================================================

umi::daisy::pod::PodHid pod_hid;

// Debug: SDRAM/QSPI test results (read via GDB)
struct DbgMemTest {
    volatile std::uint32_t sdram_result = 0;  // 0=untested, 1=pass, 2=fail
    volatile std::uint32_t sdram_read = 0;
    volatile std::uint32_t qspi_byte0 = 0;
} dbg_mem;

// ============================================================================
// DMA audio buffers
// ============================================================================

__attribute__((section(".dma_buffer")))
std::int32_t audio_tx_buf[AUDIO_BUFFER_SIZE];

__attribute__((section(".dma_buffer")))
std::int32_t audio_rx_buf[AUDIO_BUFFER_SIZE];

// Pending audio buffer pointers (set by ISR, consumed by audio task)
volatile std::int32_t* pending_tx = nullptr;
volatile std::int32_t* pending_rx = nullptr;

// ============================================================================
// Sine wave test signal
// ============================================================================

constexpr int SINE_TABLE_SIZE = 256;
constexpr std::int32_t sine_table[SINE_TABLE_SIZE] = {
    0, 205887, 411239, 615526, 818221, 1018805, 1216764, 1411594,
    1602801, 1789903, 1972429, 2149924, 2321947, 2488073, 2647898, 2801033,
    2947114, 3085797, 3216762, 3339715, 3454384, 3560525, 3657918, 3746371,
    3825717, 3895821, 3956571, 3907885, 4048703, 4078990, 4099736, 4110959,
    4112599, 4104725, 4087427, 4060822, 4025050, 3980274, 3926680, 3864475,
    3793888, 3715168, 3628582, 3534416, 3432972, 3324572, 3209548, 3088251,
    2961040, 2828289, 2690381, 2547712, 2400688, 2249720, 2095233, 1937651,
    1777406, 1614935, 1450679, 1285082, 1118590, 951646, 784695, 618178,
    452536, 288203, 125610, -35016, -193350, -349073, -501872, -651441,
    -797481, -939705, -1077833, -1211601, -1340755, -1465053, -1584269, -1698189,
    -1806611, -1909349, -2006230, -2097097, -2181812, -2260246, -2332289, -2397848,
    -2456843, -2509213, -2554908, -2593896, -2626161, -2651698, -2670523, -2682660,
    -2688150, -2687049, -2679427, -2665369, -2644970, -2618339, -2585595, -2546868,
    -2502297, -2452032, -2396234, -2335067, -2268706, -2197331, -2121127, -2040286,
    -1955005, -1865487, -1771936, -1674563, -1573582, -1469210, -1361669, -1251183,
    -1137978, -1022284, -904331, -784352, -662581, -539253, -414602, -288864,
    -162273, -35062, 92537, 220293, 347972, 475342, 602172, 728230,
    853289, 977121, 1099500, 1220205, 1339015, 1455715, 1570094, 1681945,
    1791068, 1897266, 2000350, 2100138, 2196454, 2289131, 2378009, 2462939,
    2543777, 2620387, 2692641, 2760421, 2823617, 2882128, 2935862, 2984736,
    3028677, 3067621, 3101514, 3130312, 3153980, 3172494, 3185838, 3194007,
    3197005, 3194845, 3187547, 3175141, 3157666, 3135167, 3107700, 3075330,
    3038128, 2996175, 2949557, 2898370, 2842715, 2782701, 2718441, 2650056,
    2577672, 2501421, 2421440, 2337872, 2250865, 2160572, 2067148, 1970754,
    1871553, 1769712, 1665400, 1558790, 1450055, 1339372, 1226917, 1112873,
    997419, 880740, 763019, 644440, 525189, 405451, 285413, 165260,
    45180, -74641, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

std::uint32_t phase_acc = 0;
constexpr std::uint32_t phase_inc = (440 * SINE_TABLE_SIZE) / AUDIO_SAMPLE_RATE * 65536;

// ============================================================================
// Task entry functions
// ============================================================================

/// Audio task: waits for DMA notification, bridges USB Audio ↔ SAI DMA
void audio_task_entry(void*) {
    while (true) {
        while (!(task_notifications[TASK_AUDIO].load(std::memory_order_acquire) & NOTIFY_AUDIO_READY)) {
            asm volatile("wfe");
        }
        task_notifications[TASK_AUDIO].fetch_and(~NOTIFY_AUDIO_READY, std::memory_order_release);

        auto* out = const_cast<std::int32_t*>(pending_tx);
        auto* in = const_cast<std::int32_t*>(pending_rx);

        if (out) {
            // USB Audio OUT → SAI TX (DAC): read from USB host ringbuffer
            auto frames = usb_audio.read_audio(out, AUDIO_BLOCK_SIZE);
            if (frames == 0) {
                // No USB audio: fallback to sine wave test signal
                for (std::uint32_t i = 0; i < AUDIO_BLOCK_SIZE; ++i) {
                    auto idx = (phase_acc >> 16) % SINE_TABLE_SIZE;
                    std::int32_t sample = sine_table[idx];
                    out[i * 2] = sample;
                    out[i * 2 + 1] = sample;
                    phase_acc += phase_inc;
                }
            }
        }

        if (in) {
            // SAI RX (ADC) → USB Audio IN: write to USB host ringbuffer
            usb_audio.write_audio_in(in, AUDIO_BLOCK_SIZE);
        }
    }
}

/// System task: USB polling
void system_task_entry(void*) {
    while (true) {
        usb_device.poll();
        asm volatile("wfe");
    }
}

/// Control task: HID polling + USB polling (user application task)
void control_task_entry(void*) {
    mm::DirectTransportT<> transport;
    constexpr float hid_rate = 1000.0f;  // ~1 kHz update rate
    std::uint32_t loop_counter = 0;

    while (true) {
        usb_device.poll();

        // Update digital controls + LED PWM at ~1 kHz
        // (rough timing via loop counter — will be replaced with SysTick)
        if (++loop_counter >= 100) {
            loop_counter = 0;
            pod_hid.update_controls(transport, hid_rate);

            // Knob-driven LED demo: knob1 → LED1 red, knob2 → LED2 blue
            pod_hid.led1.set(pod_hid.knobs.value(0), 0.0f, 0.0f);
            pod_hid.led2.set(0.0f, 0.0f, pod_hid.knobs.value(1));

            // Encoder click → toggle Seed board LED
            if (pod_hid.encoder.click_just_pressed()) {
                umi::daisy::toggle_led();
            }
        }

        // Update knob filter at full loop rate
        pod_hid.process_knobs();
    }
}

/// Idle task: low power wait
void idle_task_entry(void*) {
    while (true) {
        asm volatile("wfi");
    }
}

// ============================================================================
// Scheduler
// ============================================================================

/// Simple priority scheduler: pick highest priority ready task
TaskId schedule() {
    // Audio task has highest priority
    if (task_notifications[TASK_AUDIO].load(std::memory_order_relaxed) & NOTIFY_AUDIO_READY) {
        return TASK_AUDIO;
    }
    // Control task is always ready (running LED loop)
    return TASK_CONTROL;
}

} // namespace

// ============================================================================
// RTOS linkage symbols (required by handlers.cc)
// ============================================================================

extern "C" __attribute__((used))
umi::port::cm7::TaskContext* volatile umi_cm7_current_tcb = nullptr;

extern "C" __attribute__((used)) void umi_cm7_switch_context() {
    auto next = schedule();
    current_task = next;
    umi_cm7_current_tcb = &task_contexts[next];
}

// ============================================================================
// DMA IRQ handlers
// ============================================================================

extern "C" {

void DMA1_Stream0_IRQHandler() {
    using namespace umi::stm32h7;
    mm::DirectTransportT<> transport;
    auto lisr = transport.read(DMA1::LISR{});

    if (lisr & dma_flags::S0_HTIF) {
        transport.write(DMA1::LIFCR::value(dma_flags::S0_HTIF));
        pending_tx = audio_tx_buf;
        pending_rx = audio_rx_buf;
        task_notifications[TASK_AUDIO].fetch_or(NOTIFY_AUDIO_READY, std::memory_order_release);
        umi::kernel::port::cm7::request_context_switch();
    }
    if (lisr & dma_flags::S0_TCIF) {
        transport.write(DMA1::LIFCR::value(dma_flags::S0_TCIF));
        pending_tx = audio_tx_buf + AUDIO_BUFFER_SIZE / 2;
        pending_rx = audio_rx_buf + AUDIO_BUFFER_SIZE / 2;
        task_notifications[TASK_AUDIO].fetch_or(NOTIFY_AUDIO_READY, std::memory_order_release);
        umi::kernel::port::cm7::request_context_switch();
    }
    transport.write(DMA1::LIFCR::value(lisr & dma_flags::S0_ALL));
}

void DMA1_Stream1_IRQHandler() {
    using namespace umi::stm32h7;
    mm::DirectTransportT<> transport;
    transport.write(DMA1::LIFCR::value(dma_flags::S1_ALL));
}

} // extern "C"

// Fault handlers
extern "C" {
void HardFault_Handler() {
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
// main() — called from Reset_Handler, sets up tasks and starts scheduler
// ============================================================================

int main() {
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
    usb_device.set_strings(usb_strings);
    usb_device.init();
    usb_hal.connect();

    // Pod HID: enable ADC12 clock, then initialize all controls
    {
        mm::DirectTransportT<> transport;
        // ADC clock source: per_ck (HSI 64 MHz by default)
        transport.modify(umi::stm32h7::RCC::D3CCIPR::ADCSEL::value(2));  // 10 = per_ck
        // ADC12 clock enable (AHB1)
        transport.modify(umi::stm32h7::RCC::AHB1ENR::ADC12EN::Set{});
        [[maybe_unused]] auto dummy = transport.read(umi::stm32h7::RCC::AHB1ENR{});
        // GPIO clocks for Pod controls (A,B,C,D,G) — most already enabled by init_clocks
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOAEN::Set{});
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOBEN::Set{});
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOCEN::Set{});
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIODEN::Set{});
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOGEN::Set{});
        dummy = transport.read(umi::stm32h7::RCC::AHB4ENR{});

        // Audio callback rate: 48000 / 48 = 1000 Hz
        constexpr float update_rate = static_cast<float>(AUDIO_SAMPLE_RATE) / AUDIO_BLOCK_SIZE;
        pod_hid.init(transport, update_rate);
    }

    // Initialize task stacks
    umi::port::cm7::init_task_context(task_contexts[TASK_AUDIO],
        audio_stack, AUDIO_STACK_SIZE, audio_task_entry, nullptr, true);
    umi::port::cm7::init_task_context(task_contexts[TASK_SYSTEM],
        system_stack, SYSTEM_STACK_SIZE, system_task_entry, nullptr, false);
    umi::port::cm7::init_task_context(task_contexts[TASK_CONTROL],
        control_stack, CONTROL_STACK_SIZE, control_task_entry, nullptr, true);
    umi::port::cm7::init_task_context(task_contexts[TASK_IDLE],
        idle_stack, IDLE_STACK_SIZE, idle_task_entry, nullptr, false);

    // Set PendSV to lowest priority, SysTick low priority
    // SHPR3: PendSV at [23:16], SysTick at [31:24]
    auto* shpr3 = reinterpret_cast<volatile std::uint32_t*>(0xE000ED20);
    *shpr3 = (*shpr3 & 0x0000FFFF) | (0xFF << 16) | (0xF0 << 24);

    // Start audio DMA
    umi::daisy::start_audio();

    // Start scheduler: boot into control task
    current_task = TASK_CONTROL;
    umi_cm7_current_tcb = &task_contexts[TASK_CONTROL];
    umi::port::cm7::start_first_task();  // Never returns
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

extern "C" [[noreturn]] void Reset_Handler() {
    umi::cm7::enable_fpu();
    asm volatile("dsb\nisb" ::: "memory");

    umi::cm7::configure_mpu();
    umi::cm7::enable_icache();
    umi::cm7::enable_dcache();

    std::uint32_t* src = &_sidata;
    std::uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    // Zero-init DTCM BSS (vector table lives here, must be zeroed before irq::init)
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

    // PendSV and SVC via SRAM vector table
    umi::irq::set_handler(exc::PendSV, umi::port::cm7::PendSV_Handler);
    umi::irq::set_handler(exc::SVCall, umi::port::cm7::SVC_Handler);

    // DMA1 Stream 0/1
    umi::irq::set_handler(11, DMA1_Stream0_IRQHandler);
    umi::irq::set_handler(12, DMA1_Stream1_IRQHandler);
    umi::irq::set_priority(11, 0x00);
    umi::irq::set_priority(12, 0x00);
    umi::irq::enable(11);
    umi::irq::enable(12);

    for (void (**p)(void) = __init_array_start; p < __init_array_end; ++p) {
        (*p)();
    }

    main();
    while (true) {}
}
