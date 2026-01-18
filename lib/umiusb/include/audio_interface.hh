// SPDX-License-Identifier: MIT
// UMI-USB: Professional Audio Interface Class
// Supports UAC1/UAC2 with Asynchronous/Adaptive/Sync modes
#pragma once

#include <cstdint>
#include <span>
#include <array>
#include "types.hh"
#include "audio_types.hh"

namespace umiusb {

// ============================================================================
// USB Audio Interface Class
// ============================================================================

/// Professional USB Audio Interface with full sync mode support
/// Can be configured as Audio-only, MIDI-only, or Audio+MIDI
/// Supports both UAC1 (wide compatibility) and UAC2 (high performance)
template<UacVersion Version = UacVersion::Uac1,
         bool AudioEnabled = true,
         uint32_t SampleRate = 48000,
         uint8_t Channels = 2,
         uint8_t BitDepth = 16,
         uint8_t AudioEp = 1,
         uint8_t FeedbackEp = 2,
         AudioSyncMode DefaultMode = AudioSyncMode::Async,
         bool MidiEnabled = false,
         uint8_t MidiEp = 3,
         uint16_t MidiPacketSz = 64>
class AudioInterface {
public:
    // Version and configuration
    static constexpr UacVersion UAC_VERSION = Version;
    static constexpr bool IS_UAC2 = (Version == UacVersion::Uac2);

    // Audio configuration
    static constexpr bool HAS_AUDIO = AudioEnabled;
    static constexpr uint32_t SAMPLE_RATE = SampleRate;
    static constexpr uint8_t CHANNELS = Channels;
    static constexpr uint8_t BIT_DEPTH = BitDepth;
    static constexpr uint16_t BYTES_PER_FRAME = Channels * (BitDepth / 8);
    static constexpr uint16_t AUDIO_PACKET_SIZE = HAS_AUDIO ? ((SampleRate / 1000) + 1) * BYTES_PER_FRAME : 0;
    static constexpr uint8_t EP_AUDIO = AudioEp;
    static constexpr uint8_t EP_FEEDBACK = FeedbackEp;
    static constexpr AudioSyncMode DEFAULT_SYNC_MODE = DefaultMode;

    // MIDI configuration
    static constexpr bool HAS_MIDI = MidiEnabled;
    static constexpr uint8_t EP_MIDI = MidiEp;
    static constexpr uint16_t MIDI_PACKET_SIZE = MidiPacketSz;

    // UAC constants
    static constexpr uint8_t SUBCLASS_AUDIOCONTROL = 0x01;
    static constexpr uint8_t SUBCLASS_AUDIOSTREAMING = 0x02;
    static constexpr uint8_t SUBCLASS_MIDISTREAMING = 0x03;

    // Callbacks
    using StatusCallback = void(*)(bool streaming);
    using MidiCallback = void(*)(uint8_t cable, const uint8_t* data, uint8_t len);
    using SysExCallback = void(*)(const uint8_t* data, uint16_t len);

    StatusCallback on_streaming_change = nullptr;

    void set_midi_callback(MidiCallback cb) { midi_processor_.on_midi = cb; }
    void set_sysex_callback(SysExCallback cb) { midi_processor_.on_sysex = cb; }

private:
    // ========================================================================
    // Descriptor Builder
    // ========================================================================

    static constexpr std::size_t calc_descriptor_size(AudioSyncMode mode) {
        std::size_t size = 9;  // Configuration

        if constexpr (HAS_AUDIO) {
            if constexpr (IS_UAC2) {
                // UAC2 Audio Control Interface
                size += 9;   // Interface (AC)
                size += 9;   // AC Header
                size += 8;   // Clock Source
                size += 17;  // Input Terminal
                size += 12;  // Output Terminal

                // UAC2 Audio Streaming Interface
                size += 9;   // Alt 0
                size += 9;   // Alt 1
                size += 16;  // AS General
                size += 6;   // AS Format Type I
                size += 7;   // Audio Endpoint
                size += 8;   // CS Audio Endpoint

                if (mode == AudioSyncMode::Async) {
                    size += 7;  // Feedback Endpoint
                }
            } else {
                // UAC1 Audio Control Interface
                size += 9;   // Interface
                size += 9;   // AC Header
                size += 12;  // Input Terminal
                size += 10;  // Feature Unit (stereo: 7 + 3)
                size += 9;   // Output Terminal

                // UAC1 Audio Streaming Interface
                size += 9;   // Alt 0
                size += 9;   // Alt 1
                size += 7;   // AS General
                size += 11;  // AS Format Type I
                size += 9;   // Audio Endpoint
                size += 7;   // CS Audio Endpoint

                if (mode == AudioSyncMode::Async) {
                    size += 9;  // Feedback Endpoint
                }
            }
        }

        // MIDI Streaming interface (same for UAC1 and UAC2)
        if constexpr (HAS_MIDI) {
            size += 9;   // Interface
            size += 7;   // MS Header
            size += static_cast<std::size_t>(6 * 2);  // IN Jacks
            size += static_cast<std::size_t>(9 * 2);  // OUT Jacks
            size += 9 + 5;  // OUT Endpoint + CS
            size += 9 + 5;  // IN Endpoint + CS
        }

        return size;
    }

    static constexpr std::size_t MAX_DESC_SIZE = calc_descriptor_size(AudioSyncMode::Async);

    static constexpr auto build_descriptor(AudioSyncMode mode) {
        std::array<uint8_t, MAX_DESC_SIZE> desc{};
        std::size_t p = 0;

        auto w = [&desc, &p](auto... bytes) {
            ((desc[p++] = static_cast<uint8_t>(bytes)), ...);
        };

        auto w16 = [&desc, &p](uint16_t val) {
            desc[p++] = val & 0xFF;
            desc[p++] = val >> 8;
        };

        auto w24 = [&desc, &p](uint32_t val) {
            desc[p++] = val & 0xFF;
            desc[p++] = (val >> 8) & 0xFF;
            desc[p++] = (val >> 16) & 0xFF;
        };

        auto w32 = [&desc, &p](uint32_t val) {
            desc[p++] = val & 0xFF;
            desc[p++] = (val >> 8) & 0xFF;
            desc[p++] = (val >> 16) & 0xFF;
            desc[p++] = (val >> 24) & 0xFF;
        };

        bool has_feedback = HAS_AUDIO && (mode == AudioSyncMode::Async);
        uint8_t num_audio_eps = has_feedback ? 2 : 1;
        std::size_t total_size = calc_descriptor_size(mode);
        uint8_t num_interfaces = (HAS_AUDIO ? 2 : 0) + (HAS_MIDI ? 1 : 0);
        uint8_t midi_interface = HAS_AUDIO ? 2 : 0;

        // === Configuration Descriptor ===
        w(9, bDescriptorType::Configuration);
        w16(total_size);
        w(num_interfaces);
        w(1);           // bConfigurationValue
        w(0);           // iConfiguration
        w(0x80);        // bmAttributes
        w(100);         // bMaxPower (200mA)

        if constexpr (HAS_AUDIO) {
            if constexpr (IS_UAC2) {
                // ============================================================
                // UAC2 Descriptors
                // ============================================================

                // Interface 0: Audio Control
                w(9, bDescriptorType::Interface);
                w(0);  // bInterfaceNumber
                w(0);  // bAlternateSetting
                w(0);  // bNumEndpoints
                w(bDeviceClass::Audio);
                w(SUBCLASS_AUDIOCONTROL);
                w(uac::uac2::IP_VERSION_02_00);  // bInterfaceProtocol
                w(0);  // iInterface

                // AC Header (UAC2)
                constexpr uint16_t ac_total = 9 + 8 + 17 + 12;
                w(9, bDescriptorType::CsInterface, uac::ac::HEADER);
                w16(0x0200);  // bcdADC = 2.0
                w(uac::uac2::FUNCTION_SUBCLASS);  // bCategory
                w16(ac_total);  // wTotalLength
                w(0x00);  // bmControls

                // Clock Source (UAC2)
                w(8, bDescriptorType::CsInterface, uac::ac::CLOCK_SOURCE);
                w(1);  // bClockID
                w(uac::uac2::CLOCK_INTERNAL_FIXED);  // bmAttributes
                w(0x07);  // bmControls (freq r/w, validity r)
                w(0);  // bAssocTerminal
                w(0);  // iClockSource

                // Input Terminal (UAC2)
                w(17, bDescriptorType::CsInterface, uac::ac::INPUT_TERMINAL);
                w(2);  // bTerminalID
                w16(uac::TERMINAL_USB_STREAMING);
                w(0);  // bAssocTerminal
                w(1);  // bCSourceID (clock)
                w(CHANNELS);  // bNrChannels
                w32(CHANNELS == 2 ? 0x00000003 : 0x00000000);  // bmChannelConfig
                w(0);  // iChannelNames
                w16(0x0000);  // bmControls
                w(0);  // iTerminal

                // Output Terminal (UAC2)
                w(12, bDescriptorType::CsInterface, uac::ac::OUTPUT_TERMINAL);
                w(3);  // bTerminalID
                w16(uac::TERMINAL_SPEAKER);
                w(0);  // bAssocTerminal
                w(2);  // bSourceID
                w(1);  // bCSourceID (clock)
                w16(0x0000);  // bmControls
                w(0);  // iTerminal

                // Interface 1: Audio Streaming (Alt 0 - zero bandwidth)
                w(9, bDescriptorType::Interface);
                w(1);
                w(0);  // bAlternateSetting
                w(0);  // bNumEndpoints
                w(bDeviceClass::Audio);
                w(SUBCLASS_AUDIOSTREAMING);
                w(uac::uac2::IP_VERSION_02_00);
                w(0);

                // Interface 1: Audio Streaming (Alt 1 - active)
                w(9, bDescriptorType::Interface);
                w(1);
                w(1);  // bAlternateSetting
                w(num_audio_eps);
                w(bDeviceClass::Audio);
                w(SUBCLASS_AUDIOSTREAMING);
                w(uac::uac2::IP_VERSION_02_00);
                w(0);

                // AS General (UAC2)
                w(16, bDescriptorType::CsInterface, uac::as::GENERAL);
                w(2);  // bTerminalLink
                w(0x00);  // bmControls
                w(uac::FORMAT_TYPE_I);  // bFormatType
                w32(0x00000001);  // bmFormats (PCM)
                w(CHANNELS);  // bNrChannels
                w32(CHANNELS == 2 ? 0x00000003 : 0x00000000);  // bmChannelConfig
                w(0);  // iChannelNames

                // AS Format Type I (UAC2)
                w(6, bDescriptorType::CsInterface, uac::as::FORMAT_TYPE);
                w(uac::FORMAT_TYPE_I);
                w(BIT_DEPTH / 8);  // bSubslotSize
                w(BIT_DEPTH);  // bBitResolution

                // Audio OUT Endpoint (UAC2)
                w(7, bDescriptorType::Endpoint);
                w(EP_AUDIO);
                w(static_cast<uint8_t>(mode));
                w16(AUDIO_PACKET_SIZE);
                w(1);  // bInterval
                w(0);  // unused

                // CS Audio Endpoint (UAC2)
                w(8, bDescriptorType::CsEndpoint, uac::as::GENERAL);
                w(0x00);  // bmAttributes
                w(0x00);  // bmControls
                w(0);  // bLockDelayUnits
                w16(0);  // wLockDelay

                // Feedback Endpoint (UAC2, Async only)
                if (has_feedback) {
                    w(7, bDescriptorType::Endpoint);
                    w(0x80 | EP_FEEDBACK);
                    w(0x11);  // Iso, Feedback
                    w16(4);   // UAC2: 4 bytes (16.16 format)
                    w(4);     // bInterval = 2^4 = 16 SOFs
                    w(0);
                }
            } else {
                // ============================================================
                // UAC1 Descriptors
                // ============================================================

                // Interface 0: Audio Control
                w(9, bDescriptorType::Interface);
                w(0);
                w(0);
                w(0);
                w(bDeviceClass::Audio);
                w(SUBCLASS_AUDIOCONTROL);
                w(0);
                w(0);

                // AC Header (UAC1)
                // Signal path: IT(1) -> FU(2) -> OT(3)
                constexpr std::size_t ac_total = 9 + 12 + 10 + 9;  // Header + IT + FU + OT = 40
                w(9, bDescriptorType::CsInterface, uac::ac::HEADER);
                w16(0x0100);  // bcdADC = 1.0
                w16(ac_total);
                w(1);  // bInCollection
                w(1);  // baInterfaceNr

                // Input Terminal (UAC1) - ID=1
                w(12, bDescriptorType::CsInterface, uac::ac::INPUT_TERMINAL);
                w(1);  // bTerminalID
                w16(uac::TERMINAL_USB_STREAMING);
                w(0);  // bAssocTerminal
                w(CHANNELS);
                w16(CHANNELS == 2 ? 0x0003 : 0x0000);  // wChannelConfig
                w(0);  // iChannelNames
                w(0);  // iTerminal

                // Feature Unit (UAC1) - ID=2, Source=IT(1)
                // bLength = 7 + (ch+1) * bControlSize = 7 + 3*1 = 10
                w(10, bDescriptorType::CsInterface, uac::ac::FEATURE_UNIT);
                w(2);     // bUnitID
                w(1);     // bSourceID (Input Terminal)
                w(1);     // bControlSize
                w(0x03);  // bmaControls[0] Master: Mute + Volume
                w(0x00);  // bmaControls[1] Ch1: none
                w(0x00);  // bmaControls[2] Ch2: none
                w(0);     // iFeature

                // Output Terminal (UAC1) - ID=3, Source=FU(2)
                w(9, bDescriptorType::CsInterface, uac::ac::OUTPUT_TERMINAL);
                w(3);  // bTerminalID
                w16(uac::TERMINAL_SPEAKER);
                w(0);  // bAssocTerminal
                w(2);  // bSourceID (Feature Unit)
                w(0);  // iTerminal

                // Interface 1: Audio Streaming (Alt 0)
                w(9, bDescriptorType::Interface);
                w(1);
                w(0);
                w(0);
                w(bDeviceClass::Audio);
                w(SUBCLASS_AUDIOSTREAMING);
                w(0);
                w(0);

                // Interface 1: Audio Streaming (Alt 1)
                w(9, bDescriptorType::Interface);
                w(1);
                w(1);
                w(num_audio_eps);
                w(bDeviceClass::Audio);
                w(SUBCLASS_AUDIOSTREAMING);
                w(0);
                w(0);

                // AS General (UAC1)
                w(7, bDescriptorType::CsInterface, uac::as::GENERAL);
                w(1);  // bTerminalLink
                w(1);  // bDelay
                w16(uac::FORMAT_PCM);

                // AS Format Type I (UAC1)
                w(11, bDescriptorType::CsInterface, uac::as::FORMAT_TYPE);
                w(uac::FORMAT_TYPE_I);
                w(CHANNELS);
                w(BIT_DEPTH / 8);  // bSubFrameSize
                w(BIT_DEPTH);  // bBitResolution
                w(1);  // bSamFreqType (1 discrete frequency)
                w24(SAMPLE_RATE);

                // Audio OUT Endpoint (UAC1)
                w(9, bDescriptorType::Endpoint);
                w(EP_AUDIO);
                w(static_cast<uint8_t>(mode));
                w16(AUDIO_PACKET_SIZE);
                w(1);  // bInterval
                w(0);  // bRefresh
                w(has_feedback ? (0x80 | EP_FEEDBACK) : 0);  // bSynchAddress

                // CS Audio Endpoint (UAC1)
                w(7, bDescriptorType::CsEndpoint, uac::as::GENERAL);
                w(0x00);  // bmAttributes
                w(0);  // bLockDelayUnits
                w16(0);  // wLockDelay

                // Feedback Endpoint (UAC1, Async only)
                if (has_feedback) {
                    w(9, bDescriptorType::Endpoint);
                    w(0x80 | EP_FEEDBACK);
                    w(0x11);  // Iso, Feedback
                    w16(3);   // UAC1 FS: 3 bytes (10.14 format)
                    w(1);     // bInterval
                    w(4);     // bRefresh = 2^4 = 16ms
                    w(0);     // bSynchAddress
                }
            }
        }

        // === MIDI Streaming Interface (same for UAC1/UAC2) ===
        if constexpr (HAS_MIDI) {
            w(9, bDescriptorType::Interface);
            w(midi_interface);
            w(0);
            w(2);  // bNumEndpoints
            w(bDeviceClass::Audio);
            w(SUBCLASS_MIDISTREAMING);
            w(0);
            w(0);

            // MS Header
            constexpr uint16_t ms_total = 7 + (6*2) + (9*2) + ((9+5)*2);
            w(7, bDescriptorType::CsInterface, uac::ms::HEADER);
            w16(0x0100);  // bcdMSC
            w16(ms_total);

            // MIDI IN Jack - Embedded
            w(6, bDescriptorType::CsInterface, uac::ms::MIDI_IN_JACK);
            w(uac::JACK_EMBEDDED);
            w(1);  // bJackID
            w(0);  // iJack

            // MIDI IN Jack - External
            w(6, bDescriptorType::CsInterface, uac::ms::MIDI_IN_JACK);
            w(uac::JACK_EXTERNAL);
            w(2);  // bJackID
            w(0);  // iJack

            // MIDI OUT Jack - Embedded
            w(9, bDescriptorType::CsInterface, uac::ms::MIDI_OUT_JACK);
            w(uac::JACK_EMBEDDED);
            w(3);  // bJackID
            w(1);  // bNrInputPins
            w(2);  // baSourceID
            w(1);  // baSourcePin
            w(0);  // iJack

            // MIDI OUT Jack - External
            w(9, bDescriptorType::CsInterface, uac::ms::MIDI_OUT_JACK);
            w(uac::JACK_EXTERNAL);
            w(4);  // bJackID
            w(1);  // bNrInputPins
            w(1);  // baSourceID
            w(1);  // baSourcePin
            w(0);  // iJack

            // Bulk OUT Endpoint
            w(9, bDescriptorType::Endpoint);
            w(EP_MIDI);
            w(static_cast<uint8_t>(TransferType::Bulk));
            w16(MIDI_PACKET_SIZE);
            w(0);
            w(0);
            w(0);

            // CS MS Bulk OUT
            w(5, bDescriptorType::CsEndpoint, uac::ms::GENERAL);
            w(1);  // bNumEmbMIDIJack
            w(1);  // baAssocJackID

            // Bulk IN Endpoint
            w(9, bDescriptorType::Endpoint);
            w(0x80 | EP_MIDI);
            w(static_cast<uint8_t>(TransferType::Bulk));
            w16(MIDI_PACKET_SIZE);
            w(0);
            w(0);
            w(0);

            // CS MS Bulk IN
            w(5, bDescriptorType::CsEndpoint, uac::ms::GENERAL);
            w(1);  // bNumEmbMIDIJack
            w(3);  // baAssocJackID
        }

        return desc;
    }

    // Async mode only - industry standard for quality audio
    static constexpr auto descriptor_ = build_descriptor(AudioSyncMode::Async);

    // State (Async mode fixed)
    bool audio_streaming_ = false;
    bool midi_configured_ = false;
    bool feedback_pending_ = false;

    // Feature Unit state (UAC1)
    bool fu_mute_ = false;
    // Volume: 1/256 dB units. 0x0000 = 0dB, 0x8100 = -127dB
    // Initial value: 0dB (maximum)
    int16_t fu_volume_ = 0;

    // Pending SET request info (for EP0 data stage)
    uint8_t pending_set_ctrl_ = 0;  // Control selector (0=none, 1=mute, 2=volume)
    uint8_t pending_set_len_ = 0;   // Expected data length

    // Audio processing
    AudioRingBuffer<256, CHANNELS> ring_buffer_;
    FeedbackCalculator<Version> feedback_calc_;
    PllRateController pll_controller_;
    MidiProcessor midi_processor_;

public:
    // ========================================================================
    // Initialization
    // ========================================================================

    AudioInterface() {
        feedback_calc_.reset(SAMPLE_RATE);
        pll_controller_.reset();
    }

    void reset() {
        ring_buffer_.reset();
        feedback_calc_.reset(SAMPLE_RATE);
        pll_controller_.reset();
        audio_streaming_ = false;
        midi_configured_ = false;
        feedback_pending_ = false;
    }

    // ========================================================================
    // USB Class Interface
    // ========================================================================

    [[nodiscard]] std::span<const uint8_t> config_descriptor() const {
        constexpr std::size_t size = calc_descriptor_size(AudioSyncMode::Async);
        return {descriptor_.data(), size};
    }

    void on_configured(bool configured) {
        if (!configured) {
            audio_streaming_ = false;
            midi_configured_ = false;
            feedback_pending_ = false;
            if (on_streaming_change) {
                on_streaming_change(false);
            }
        }
    }

    template<typename HalT>
    void configure_endpoints(HalT& hal) {
        if constexpr (HAS_MIDI) {
            hal.ep_configure({EP_MIDI, Direction::Out, TransferType::Bulk, MIDI_PACKET_SIZE});
            hal.ep_configure({EP_MIDI, Direction::In, TransferType::Bulk, MIDI_PACKET_SIZE});
            midi_configured_ = true;
        }
    }

    template<typename HalT>
    void set_interface(HalT& hal, uint8_t interface, uint8_t alt_setting) {
        if constexpr (!HAS_AUDIO) {
            (void)hal; (void)interface; (void)alt_setting;
            return;
        } else {
            if (interface != 1) return;

            bool was_streaming = audio_streaming_;

            if (alt_setting == 1) {
                hal.ep_configure({EP_AUDIO, Direction::Out,
                                 TransferType::Isochronous, AUDIO_PACKET_SIZE});

                // Async mode: always configure feedback endpoint
                constexpr uint16_t fb_size = IS_UAC2 ? 4 : 3;
                hal.ep_configure({EP_FEEDBACK, Direction::In,
                                 TransferType::Isochronous, fb_size});
                feedback_pending_ = true;

                audio_streaming_ = true;
                ring_buffer_.reset();
                feedback_calc_.reset(SAMPLE_RATE);
                pll_controller_.reset();
            } else {
                audio_streaming_ = false;
                feedback_pending_ = false;
            }

            if (was_streaming != audio_streaming_ && on_streaming_change) {
                on_streaming_change(audio_streaming_);
            }
        }
    }

    bool handle_request(const SetupPacket& setup, std::span<uint8_t>& response) {
        if constexpr (IS_UAC2 && HAS_AUDIO) {
            // UAC2 Clock Source requests
            if ((setup.bmRequestType & 0x1F) == 0x01) {  // Interface request
                uint8_t ctrl = setup.wValue >> 8;
                uint8_t entity = setup.wIndex >> 8;

                if (entity == 1) {  // Clock Source
                    if (ctrl == 0x01) {  // CUR - Sampling Frequency
                        if (setup.bRequest == 0x01) {  // GET CUR
                            if (response.size() >= 4) {
                                response[0] = SAMPLE_RATE & 0xFF;
                                response[1] = (SAMPLE_RATE >> 8) & 0xFF;
                                response[2] = (SAMPLE_RATE >> 16) & 0xFF;
                                response[3] = (SAMPLE_RATE >> 24) & 0xFF;
                                response = response.subspan(0, 4);
                                return true;
                            }
                        }
                    } else if (ctrl == 0x02) {  // RANGE
                        if (setup.bRequest == 0x02) {  // GET RANGE
                            if (response.size() >= 14) {
                                // wNumSubRanges = 1
                                response[0] = 1; response[1] = 0;
                                // dMIN
                                response[2] = SAMPLE_RATE & 0xFF;
                                response[3] = (SAMPLE_RATE >> 8) & 0xFF;
                                response[4] = (SAMPLE_RATE >> 16) & 0xFF;
                                response[5] = (SAMPLE_RATE >> 24) & 0xFF;
                                // dMAX
                                response[6] = SAMPLE_RATE & 0xFF;
                                response[7] = (SAMPLE_RATE >> 8) & 0xFF;
                                response[8] = (SAMPLE_RATE >> 16) & 0xFF;
                                response[9] = (SAMPLE_RATE >> 24) & 0xFF;
                                // dRES
                                response[10] = 0; response[11] = 0;
                                response[12] = 0; response[13] = 0;
                                response = response.subspan(0, 14);
                                return true;
                            }
                        }
                    }
                }
            }
        }

        // UAC1 Audio Control requests
        // bmRequestType for UAC1: 0x21/0xA1 (Class + Interface)
        if constexpr (!IS_UAC2 && HAS_AUDIO) {
            uint8_t entity = setup.wIndex >> 8;
            uint8_t ctrl = setup.wValue >> 8;  // Control Selector

            // UAC1 Audio Class Request Codes (Table 5-1)
            // GET requests have direction bit (0x80) set
            constexpr uint8_t kSetCur = 0x01;
            constexpr uint8_t kGetCur = 0x81;
            constexpr uint8_t kGetMin = 0x82;
            constexpr uint8_t kGetMax = 0x83;
            constexpr uint8_t kGetRes = 0x84;
            bool is_get = (setup.bmRequestType & 0x80) != 0;

            // Feature Unit (entity == 2)
            if (entity == 2) {
                if (is_get) {
                    // Mute control (CS=0x01)
                    if (ctrl == 0x01) {
                        if (setup.bRequest == kGetCur) {
                            response[0] = fu_mute_ ? 1 : 0;
                            response = response.subspan(0, 1);
                            return true;
                        }
                        // Mute doesn't have MIN/MAX/RES, but return 0/1/1 if asked
                        if (setup.bRequest == kGetMin) {
                            response[0] = 0;
                            response = response.subspan(0, 1);
                            return true;
                        }
                        if (setup.bRequest == kGetMax) {
                            response[0] = 1;
                            response = response.subspan(0, 1);
                            return true;
                        }
                        if (setup.bRequest == kGetRes) {
                            response[0] = 1;
                            response = response.subspan(0, 1);
                            return true;
                        }
                    }
                    // Volume control (CS=0x02)
                    // USB Audio 1.0: signed 16-bit, 1/256 dB units
                    // 0x0000 = 0dB, 0x8000 = silence, 0x8001 = -127.9961dB
                    if (ctrl == 0x02) {
                        if (setup.bRequest == kGetCur) {
                            response[0] = fu_volume_ & 0xFF;
                            response[1] = (fu_volume_ >> 8) & 0xFF;
                            response = response.subspan(0, 2);
                            return true;
                        }
                        if (setup.bRequest == kGetMin) {
                            // 0x8100 = -127dB (minimum, avoiding 0x8000 silence marker)
                            response[0] = 0x00; response[1] = 0x81;
                            response = response.subspan(0, 2);
                            return true;
                        }
                        if (setup.bRequest == kGetMax) {
                            // 0x0000 = 0dB
                            response[0] = 0x00; response[1] = 0x00;
                            response = response.subspan(0, 2);
                            return true;
                        }
                        if (setup.bRequest == kGetRes) {
                            // 0x0100 = 1dB resolution
                            response[0] = 0x00; response[1] = 0x01;
                            response = response.subspan(0, 2);
                            return true;
                        }
                    }
                } else {
                    // SET requests
                    if (setup.bRequest == kSetCur) {
                        pending_set_ctrl_ = ctrl;
                        pending_set_len_ = (ctrl == 0x01) ? 1 : 2;
                        response = response.subspan(0, 0);  // No response data
                        return true;
                    }
                }
            }

            // For other entities (IT=1, OT=3), return STALL is OK
            // But some hosts may probe all entities, so accept any GET with zeros
            if (entity != 0 && is_get && setup.wLength > 0) {
                uint16_t len = setup.wLength;
                if (len > response.size()) len = static_cast<uint16_t>(response.size());
                for (uint16_t i = 0; i < len; ++i) response[i] = 0;
                response = response.subspan(0, len);
                return true;
            }
        }

        (void)setup; (void)response;
        return false;
    }

    /// Handle EP0 data received (for SET_CUR requests)
    void on_ep0_rx(std::span<const uint8_t> data) {
        if constexpr (!IS_UAC2 && HAS_AUDIO) {
            if (pending_set_ctrl_ != 0 && data.size() >= pending_set_len_) {
                if (pending_set_ctrl_ == 0x01) {
                    // Mute control
                    fu_mute_ = (data[0] != 0);
                } else if (pending_set_ctrl_ == 0x02 && data.size() >= 2) {
                    // Volume control
                    fu_volume_ = static_cast<int16_t>(data[0] | (data[1] << 8));
                }
                pending_set_ctrl_ = 0;
                pending_set_len_ = 0;
            }
        }
    }

    void on_rx(uint8_t ep, std::span<const uint8_t> data) {
        if constexpr (HAS_AUDIO) {
            if (ep == EP_AUDIO && audio_streaming_) {
                uint16_t frame_count = data.size() / BYTES_PER_FRAME;
                ring_buffer_.write(reinterpret_cast<const int16_t*>(data.data()), frame_count);
            }
        }

        if constexpr (HAS_MIDI) {
            if (ep == EP_MIDI) {
                for (std::size_t i = 0; i + 3 < data.size(); i += 4) {
                    midi_processor_.process_packet(data[i], data[i+1], data[i+2], data[i+3]);
                }
            }
        }
    }

    // ========================================================================
    // SOF and Feedback Handling
    // ========================================================================

    template<typename HalT>
    void on_sof(HalT& hal) {
        if constexpr (!HAS_AUDIO) {
            (void)hal;
            return;
        }

        if (!audio_streaming_) {
            return;
        }

        feedback_calc_.on_sof();
        if (feedback_pending_) {
            auto fb = feedback_calc_.get_feedback_bytes();
            hal.ep_write(EP_FEEDBACK, fb.data(), fb.size());
        }
    }

    void on_samples_consumed(uint32_t frame_count) {
        if constexpr (!HAS_AUDIO) return;
        feedback_calc_.add_consumed_samples(frame_count);
    }

    // ========================================================================
    // Audio Buffer Access
    // ========================================================================

    uint32_t read_audio(int16_t* dest, uint32_t frame_count) {
        uint32_t read = ring_buffer_.read(dest, frame_count);

        // Apply Mute from Feature Unit
        if (fu_mute_ && read > 0) {
            __builtin_memset(dest, 0, static_cast<size_t>(read) * BYTES_PER_FRAME);
            return read;
        }

        // Apply Volume from Feature Unit
        // fu_volume_ is in 1/256 dB: 0x0000=0dB, negative values = attenuation
        // Range: 0 (0dB) to -32512 (0x8100 = -127dB)
        // macOS sends dB values linearly mapped from slider position
        // Use proper dB to linear conversion: gain = 10^(dB/20)
        // But limit the range to -48dB for practical use
        if (fu_volume_ < 0 && read > 0) {
            int32_t neg_vol = -static_cast<int32_t>(fu_volume_);

            // Scale: macOS sends 0 to -32512, map to 0 to -48dB range
            // -48dB = 1/256 amplitude, good practical range
            // neg_vol / 32512 * 48 * 256 = neg_vol * 48 / 127 in 1/256 dB
            // Convert to actual dB (1/256 units): neg_vol * 48 / 127
            int32_t db_256 = (neg_vol * 48) / 127;  // 0 to 12288 (48*256)

            // Below -48dB, treat as mute
            if (db_256 >= 12288) {
                __builtin_memset(dest, 0, static_cast<size_t>(read) * BYTES_PER_FRAME);
                return read;
            }

            // dB to linear: gain = 10^(-db/20) = 2^(-db/6.0206)
            // In 1/256 dB: gain = 2^(-db_256 / 1541)
            // Use 6dB steps (each step halves amplitude)
            int32_t shift = db_256 / 1541;  // Integer 6dB steps
            int32_t frac = db_256 % 1541;   // Fractional part

            // Base gain from integer shifts (max shift ~8 for -48dB)
            int32_t gain = 32768 >> shift;
            // Linear interpolation for fractional part
            gain = gain - ((gain * frac) / 3082);  // Smooth transition
            if (gain < 1) gain = 1;

            uint32_t samples = read * CHANNELS;
            for (uint32_t i = 0; i < samples; ++i) {
                int32_t sample = dest[i];
                sample = (sample * gain) >> 15;
                dest[i] = static_cast<int16_t>(sample);
            }
        }

        return read;
    }

    /// Read audio with ASRC (Asynchronous Sample Rate Conversion)
    /// Uses cubic hermite interpolation based on buffer level
    /// Call this instead of read_audio() for automatic drift compensation
    uint32_t read_audio_asrc(int16_t* dest, uint32_t frame_count) {
        // Calculate rate based on buffer level (PI controller output)
        int32_t level = ring_buffer_.buffer_level();
        int32_t ppm = pll_controller_.update(level);
        uint32_t rate = AudioRingBuffer<256, CHANNELS>::ppm_to_rate_q16(ppm);

        uint32_t read = ring_buffer_.read_interpolated(dest, frame_count, rate);

        // Notify feedback calculator
        feedback_calc_.add_consumed_samples(read);

        // Apply Mute
        if (fu_mute_ && read > 0) {
            __builtin_memset(dest, 0, static_cast<size_t>(read) * BYTES_PER_FRAME);
            return read;
        }

        // Apply Volume (same as read_audio)
        if (fu_volume_ < 0 && read > 0) {
            int32_t neg_vol = -static_cast<int32_t>(fu_volume_);
            int32_t db_256 = (neg_vol * 48) / 127;

            if (db_256 >= 12288) {
                __builtin_memset(dest, 0, static_cast<size_t>(read) * BYTES_PER_FRAME);
                return read;
            }

            int32_t shift = db_256 / 1541;
            int32_t frac = db_256 % 1541;
            int32_t gain = 32768 >> shift;
            gain = gain - ((gain * frac) / 3082);
            if (gain < 1) gain = 1;

            uint32_t samples = read * CHANNELS;
            for (uint32_t i = 0; i < samples; ++i) {
                int32_t sample = dest[i];
                sample = (sample * gain) >> 15;
                dest[i] = static_cast<int16_t>(sample);
            }
        }

        return read;
    }

    /// Get current ASRC rate (Q16.16, 0x10000 = 1.0)
    [[nodiscard]] uint32_t current_asrc_rate() const {
        return AudioRingBuffer<256, CHANNELS>::ppm_to_rate_q16(pll_controller_.current_ppm());
    }

    // ========================================================================
    // MIDI API
    // ========================================================================

    template<typename HalT>
    void send_midi(HalT& hal, uint8_t cable, uint8_t status,
                   uint8_t data1 = 0, uint8_t data2 = 0) {
        if constexpr (!HAS_MIDI) return;

        uint8_t cin = MidiProcessor::status_to_cin(status);
        std::array<uint8_t, 4> packet = {
            static_cast<uint8_t>((cable << 4) | cin),
            status, data1, data2
        };
        hal.ep_write(EP_MIDI, packet.data(), 4);
    }

    template<typename HalT>
    void send_note_on(HalT& hal, uint8_t ch, uint8_t note, uint8_t vel, uint8_t cable = 0) {
        send_midi(hal, cable, 0x90 | (ch & 0x0F), note & 0x7F, vel & 0x7F);
    }

    template<typename HalT>
    void send_note_off(HalT& hal, uint8_t ch, uint8_t note, uint8_t vel = 0, uint8_t cable = 0) {
        send_midi(hal, cable, 0x80 | (ch & 0x0F), note & 0x7F, vel & 0x7F);
    }

    template<typename HalT>
    void send_cc(HalT& hal, uint8_t ch, uint8_t cc, uint8_t val, uint8_t cable = 0) {
        send_midi(hal, cable, 0xB0 | (ch & 0x0F), cc & 0x7F, val & 0x7F);
    }

    // Sync mode is fixed to Async (industry standard)
    [[nodiscard]] static constexpr AudioSyncMode sync_mode() {
        return AudioSyncMode::Async;
    }

    // ========================================================================
    // Status
    // ========================================================================

    [[nodiscard]] bool is_streaming() const { return audio_streaming_; }
    [[nodiscard]] bool is_midi_configured() const { return midi_configured_; }
    [[nodiscard]] bool is_playback_started() const { return ring_buffer_.is_playback_started(); }
    [[nodiscard]] uint32_t buffered_frames() const { return ring_buffer_.buffered_frames(); }
    [[nodiscard]] uint32_t underrun_count() const { return ring_buffer_.underrun_count(); }
    [[nodiscard]] uint32_t overrun_count() const { return ring_buffer_.overrun_count(); }
    [[nodiscard]] uint32_t current_feedback() const { return feedback_calc_.get_feedback(); }
    [[nodiscard]] float feedback_rate() const { return feedback_calc_.get_feedback_rate(); }
    [[nodiscard]] int32_t pll_adjustment_ppm() const { return pll_controller_.current_ppm(); }

    // Feature Unit status (UAC1)
    [[nodiscard]] bool is_muted() const { return fu_mute_; }
    [[nodiscard]] int16_t volume_db256() const { return fu_volume_; }  // Volume in 1/256 dB
};

// ============================================================================
// Convenience Type Aliases
// ============================================================================

// UAC1 (wide compatibility)
// Template: Version, AudioEnabled, SampleRate, Channels, BitDepth, AudioEp, FeedbackEp, Mode, MidiEnabled, MidiEp, MidiPacketSz

/// UAC1 48kHz stereo Async (studio-grade, wide compatibility)
using AudioInterface48kAsync = AudioInterface<UacVersion::Uac1, true, 48000, 2, 16, 1, 2, AudioSyncMode::Async, false>;

/// UAC1 48kHz stereo Async + MIDI
using AudioMidiInterface48k = AudioInterface<UacVersion::Uac1, true, 48000, 2, 16, 1, 2, AudioSyncMode::Async, true, 3, 64>;

/// UAC1 44.1kHz stereo Async
using AudioInterface44kAsync = AudioInterface<UacVersion::Uac1, true, 44100, 2, 16, 1, 2, AudioSyncMode::Async, false>;

/// UAC1 MIDI only
using MidiInterface = AudioInterface<UacVersion::Uac1, false, 48000, 2, 16, 1, 2, AudioSyncMode::Async, true, 1, 64>;

// UAC2 (high performance, modern OS)

/// UAC2 48kHz stereo Async (low latency, Win10+/macOS/Linux)
using AudioInterface48kAsyncV2 = AudioInterface<UacVersion::Uac2, true, 48000, 2, 16, 1, 2, AudioSyncMode::Async, false>;

/// UAC2 96kHz stereo Async (high sample rate)
using AudioInterface96kAsyncV2 = AudioInterface<UacVersion::Uac2, true, 96000, 2, 24, 1, 2, AudioSyncMode::Async, false>;

/// UAC2 48kHz stereo Async + MIDI
using AudioMidiInterface48kV2 = AudioInterface<UacVersion::Uac2, true, 48000, 2, 16, 1, 2, AudioSyncMode::Async, true, 3, 64>;

/// UAC2 MIDI only
using MidiInterfaceV2 = AudioInterface<UacVersion::Uac2, false, 48000, 2, 16, 1, 2, AudioSyncMode::Async, true, 1, 64>;

}  // namespace umiusb
