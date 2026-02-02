// SPDX-License-Identifier: MIT
// AudioDriver concept sequence tests
// Tests start/stop/configure flow using stub implementation

#include "test_common.hh"

#include <cstdint>
#include <expected>
#include <functional>
#include <span>

#include "hal/result.hh"
#include "hal/audio.hh"
#include "hal/codec.hh"

// ============================================================================
// Stub AudioDevice with state tracking
// ============================================================================

namespace {

using hal::ErrorCode;
using hal::Result;

struct TestAudioDev {
    hal::audio::Config config_{};
    hal::audio::State state_ = hal::audio::State::STOPPED;
    int configure_count = 0;
    int start_count = 0;
    int stop_count = 0;

    Result<void> configure(const hal::audio::Config& cfg) {
        config_ = cfg;
        ++configure_count;
        return {};
    }
    hal::audio::Config get_config() const { return config_; }
    bool is_config_supported(const hal::audio::Config& cfg) const {
        return cfg.sample_rate == 48000 || cfg.sample_rate == 44100;
    }
    Result<void> start() {
        state_ = hal::audio::State::RUNNING;
        ++start_count;
        return {};
    }
    Result<void> stop() {
        state_ = hal::audio::State::STOPPED;
        ++stop_count;
        return {};
    }
    Result<void> pause() {
        state_ = hal::audio::State::PAUSED;
        return {};
    }
    Result<void> resume() {
        state_ = hal::audio::State::RUNNING;
        return {};
    }
    hal::audio::State get_state() const { return state_; }
    std::size_t get_buffer_size() const { return config_.buffer_size; }
    std::span<const std::uint16_t> get_available_buffer_sizes() const {
        static constexpr std::uint16_t sizes[] = {64, 128, 256, 512};
        return sizes;
    }
    std::uint32_t get_latency() const { return config_.buffer_size * 1'000'000u / config_.sample_rate; }
};

static_assert(hal::audio::AudioDevice<TestAudioDev>);

// ============================================================================
// Stub AudioCodec
// ============================================================================

struct TestCodec {
    bool initialized_ = false;
    int volume_db_ = -10;
    bool muted_ = false;
    bool powered_ = false;

    bool init() { initialized_ = true; return true; }
    void power_on() { powered_ = true; }
    void power_off() { powered_ = false; }
    void set_volume(int vol_db) { volume_db_ = vol_db; }
    void mute(bool state) { muted_ = state; }
};

static_assert(hal::AudioCodec<TestCodec>);

} // namespace

int main() {
    SECTION("AudioDevice configure → start → stop sequence");
    {
        TestAudioDev dev;
        hal::audio::Config cfg{};
        cfg.sample_rate = 48000;
        cfg.buffer_size = 256;
        cfg.channels = 2;

        CHECK(dev.configure(cfg).has_value(), "configure");
        CHECK_EQ(static_cast<int>(dev.get_state()), static_cast<int>(hal::audio::State::STOPPED), "stopped after configure");
        CHECK(dev.start().has_value(), "start");
        CHECK_EQ(static_cast<int>(dev.get_state()), static_cast<int>(hal::audio::State::RUNNING), "running after start");
        CHECK(dev.stop().has_value(), "stop");
        CHECK_EQ(static_cast<int>(dev.get_state()), static_cast<int>(hal::audio::State::STOPPED), "stopped after stop");
    }

    SECTION("AudioDevice pause → resume");
    {
        TestAudioDev dev;
        hal::audio::Config cfg{};
        dev.configure(cfg);
        dev.start();
        CHECK(dev.pause().has_value(), "pause");
        CHECK_EQ(static_cast<int>(dev.get_state()), static_cast<int>(hal::audio::State::PAUSED), "paused");
        CHECK(dev.resume().has_value(), "resume");
        CHECK_EQ(static_cast<int>(dev.get_state()), static_cast<int>(hal::audio::State::RUNNING), "running");
    }

    SECTION("AudioDevice config supported check");
    {
        TestAudioDev dev;
        hal::audio::Config cfg48k{};
        cfg48k.sample_rate = 48000;
        CHECK(dev.is_config_supported(cfg48k), "48kHz supported");

        hal::audio::Config cfg96k{};
        cfg96k.sample_rate = 96000;
        CHECK(!dev.is_config_supported(cfg96k), "96kHz not supported");
    }

    SECTION("AudioDevice buffer sizes");
    {
        TestAudioDev dev;
        auto sizes = dev.get_available_buffer_sizes();
        CHECK_EQ(static_cast<int>(sizes.size()), 4, "4 buffer sizes");
        CHECK_EQ(static_cast<int>(sizes[0]), 64, "min 64");
        CHECK_EQ(static_cast<int>(sizes[3]), 512, "max 512");
    }

    SECTION("AudioDevice latency calculation");
    {
        TestAudioDev dev;
        hal::audio::Config cfg{};
        cfg.sample_rate = 48000;
        cfg.buffer_size = 256;
        dev.configure(cfg);
        // 256 / 48000 ≈ 5333us
        CHECK_EQ(static_cast<int>(dev.get_latency()), 5333, "latency ~5.3ms");
    }

    SECTION("AudioDevice call counts");
    {
        TestAudioDev dev;
        hal::audio::Config cfg{};
        dev.configure(cfg);
        dev.configure(cfg);
        dev.start();
        dev.stop();
        dev.start();
        CHECK_EQ(dev.configure_count, 2, "configured twice");
        CHECK_EQ(dev.start_count, 2, "started twice");
        CHECK_EQ(dev.stop_count, 1, "stopped once");
    }

    SECTION("AudioCodec init → power_on → set_volume → mute");
    {
        TestCodec codec;
        CHECK(!codec.initialized_, "not initialized");
        CHECK(codec.init(), "init returns true");
        CHECK(codec.initialized_, "initialized");
        codec.power_on();
        CHECK(codec.powered_, "powered on");
        codec.set_volume(-6);
        CHECK_EQ(codec.volume_db_, -6, "volume = -6dB");
        codec.mute(true);
        CHECK(codec.muted_, "muted");
        codec.mute(false);
        CHECK(!codec.muted_, "unmuted");
        codec.power_off();
        CHECK(!codec.powered_, "powered off");
    }

    TEST_SUMMARY();
}
