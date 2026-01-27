/**
 * UMI Web - Hardware Backend
 *
 * Connects to real UMI hardware via Web MIDI API.
 * Audio comes from the hardware via USB Audio.
 * Shell interaction via SysEx messages.
 *
 * @module umi_web/core/backends/hardware
 */

import { BackendInterface } from '../backend.js';
import { Command, buildMessage, parseMessage } from '../protocol.js';

// Default parameters for hardware synth (matches umisynth/synth.hh)
// ParamId: Attack=0, Decay=1, Sustain=2, Release=3, Cutoff=4, Resonance=5, Volume=6
// CC: 21=Attack, 22=Decay, 23=Sustain, 24=Release, 25=Cutoff, 26=Resonance, 27=Volume
const DEFAULT_HARDWARE_PARAMS = [
    { id: 0, name: 'Attack', cc: 21, default: 0.1, unit: 'ms' },
    { id: 1, name: 'Decay', cc: 22, default: 0.3, unit: 'ms' },
    { id: 2, name: 'Sustain', cc: 23, default: 0.7, unit: '%' },
    { id: 3, name: 'Release', cc: 24, default: 0.4, unit: 'ms' },
    { id: 4, name: 'Cutoff', cc: 25, default: 0.5, unit: 'Hz' },
    { id: 5, name: 'Resonance', cc: 26, default: 0.3, unit: '%' },
    { id: 6, name: 'Volume', cc: 27, default: 0.8, unit: '%' },
];

/**
 * Hardware Backend
 *
 * Connects to UMI hardware via USB MIDI.
 * - MIDI IN/OUT for note/CC control
 * - SysEx for shell communication
 * - USB Audio for waveform (via AudioContext)
 */
export class HardwareBackend extends BackendInterface {
    /**
     * @param {object} options
     * @param {string} [options.deviceNameFilter='UMI'] - Filter for device names
     * @param {string} [options.audioInputDeviceId] - Audio input device ID for USB Audio
     */
    constructor(options = {}) {
        super();
        this.deviceNameFilter = options.deviceNameFilter || 'UMI';
        this.audioInputDeviceId = options.audioInputDeviceId || null;

        /** @type {MIDIAccess|null} */
        this.midiAccess = null;
        /** @type {MIDIInput|null} */
        this.input = null;
        /** @type {MIDIOutput|null} */
        this.output = null;

        /** @type {AudioContext|null} */
        this.audioContext = null;
        /** @type {AnalyserNode|null} */
        this.analyzerNode = null;
        /** @type {MediaStreamAudioSourceNode|null} */
        this.audioSource = null;
        /** @type {MediaStream|null} */
        this.audioStream = null;

        this.connected = false;
        this.txSequence = 0;
        this.deviceName = null;

        // Parameters for hardware
        this.params = [...DEFAULT_HARDWARE_PARAMS];

        // Callbacks for shell output
        /** @type {function|null} */
        this.onShellOutput = null;
    }

    /**
     * Check if Web MIDI is supported
     * @returns {boolean}
     */
    static isSupported() {
        return typeof navigator !== 'undefined' && 'requestMIDIAccess' in navigator;
    }

    /**
     * Check if hardware backend is available
     * @returns {Promise<boolean>}
     */
    static async isAvailable() {
        if (!HardwareBackend.isSupported()) return false;
        try {
            const access = await navigator.requestMIDIAccess({ sysex: true });
            // Check if any UMI device is connected
            for (const input of access.inputs.values()) {
                if (input.name && input.name.includes('UMI')) {
                    return true;
                }
            }
            return false;
        } catch {
            return false;
        }
    }

    /**
     * Get available UMI devices
     * @returns {Promise<Array<{input: MIDIInput, output: MIDIOutput, name: string}>>}
     */
    async getDevices() {
        if (!this.midiAccess) {
            await this._requestAccess();
        }
        if (!this.midiAccess) return [];

        const devices = [];
        const inputs = new Map();
        const outputs = new Map();

        // Collect inputs/outputs
        for (const input of this.midiAccess.inputs.values()) {
            if (input.name && input.name.includes(this.deviceNameFilter)) {
                inputs.set(input.name, input);
            }
        }
        for (const output of this.midiAccess.outputs.values()) {
            if (output.name && output.name.includes(this.deviceNameFilter)) {
                outputs.set(output.name, output);
            }
        }

        // Match pairs
        for (const [name, input] of inputs) {
            const output = outputs.get(name);
            if (output) {
                devices.push({ input, output, name });
            }
        }

        return devices;
    }

    /**
     * Request MIDI access
     * @private
     */
    async _requestAccess() {
        if (!HardwareBackend.isSupported()) {
            throw new Error('Web MIDI API not supported');
        }
        this.midiAccess = await navigator.requestMIDIAccess({ sysex: true });
        this.midiAccess.onstatechange = (e) => this._onStateChange(e);
    }

    /**
     * Start the backend - connect to first available device
     * @returns {Promise<boolean>}
     */
    async start() {
        const devices = await this.getDevices();
        if (devices.length === 0) {
            if (this.onError) {
                this.onError(new Error('No UMI device found'));
            }
            return false;
        }
        return this.connectDevice(devices[0].name);
    }

    /**
     * Connect to a specific device by name
     * @param {string} deviceName
     * @returns {Promise<boolean>}
     */
    async connectDevice(deviceName) {
        const devices = await this.getDevices();
        const device = devices.find(d => d.name === deviceName);

        if (!device) {
            if (this.onError) {
                this.onError(new Error(`Device not found: ${deviceName}`));
            }
            return false;
        }

        this.input = device.input;
        this.output = device.output;
        this.deviceName = deviceName;

        // Set up MIDI message handler
        this.input.onmidimessage = (e) => this._onMidiMessage(e);

        this.connected = true;
        console.log('[HardwareBackend] Connected to:', deviceName);

        // Initialize USB Audio input for waveform
        await this._initAudioInput();

        if (this.onReady) {
            this.onReady();
        }

        // Send params to UI
        if (this.onMessage) {
            this.onMessage({ type: 'params', params: this.params });
        }

        // Send initial command to trigger shell prompt
        this._sendShellInput('\r');

        // Request initial kernel state
        this.requestKernelState();

        return true;
    }

    /**
     * Initialize USB Audio input for waveform display
     * @private
     */
    async _initAudioInput() {
        try {
            // Find UMI audio input device
            const devices = await navigator.mediaDevices.enumerateDevices();
            const audioInputs = devices.filter(d => d.kind === 'audioinput');

            // Try to find UMI audio device
            let deviceId = this.audioInputDeviceId;
            if (!deviceId) {
                const umiAudio = audioInputs.find(d =>
                    d.label && (d.label.includes('UMI') || d.label.includes('Synth'))
                );
                if (umiAudio) {
                    deviceId = umiAudio.deviceId;
                    console.log('[HardwareBackend] Found UMI audio input:', umiAudio.label);
                }
            }

            if (!deviceId && audioInputs.length > 0) {
                // Use first available if no UMI device found
                console.log('[HardwareBackend] Using default audio input');
            }

            // Create AudioContext
            this.audioContext = new AudioContext({ sampleRate: 48000 });

            // Get audio stream
            const constraints = {
                audio: deviceId ? { deviceId: { exact: deviceId } } : true,
                video: false
            };
            this.audioStream = await navigator.mediaDevices.getUserMedia(constraints);

            // Create source and analyzer
            this.audioSource = this.audioContext.createMediaStreamSource(this.audioStream);
            this.analyzerNode = this.audioContext.createAnalyser();
            this.analyzerNode.fftSize = 2048;

            // Connect source to analyzer (not to destination to avoid feedback)
            this.audioSource.connect(this.analyzerNode);

            console.log('[HardwareBackend] Audio input initialized');
        } catch (err) {
            console.warn('[HardwareBackend] Failed to initialize audio input:', err);
            // Continue without audio - MIDI still works
        }
    }

    /**
     * Stop the backend
     */
    stop() {
        // Stop MIDI
        if (this.input) {
            this.input.onmidimessage = null;
            this.input = null;
        }
        this.output = null;

        // Stop audio
        if (this.audioStream) {
            this.audioStream.getTracks().forEach(t => t.stop());
            this.audioStream = null;
        }
        if (this.audioSource) {
            this.audioSource.disconnect();
            this.audioSource = null;
        }
        if (this.analyzerNode) {
            this.analyzerNode.disconnect();
            this.analyzerNode = null;
        }
        if (this.audioContext) {
            this.audioContext.close();
            this.audioContext = null;
        }

        this.connected = false;
        this.deviceName = null;
        console.log('[HardwareBackend] Disconnected');
    }

    /**
     * Get AudioContext
     * @returns {AudioContext|null}
     */
    getAudioContext() {
        return this.audioContext;
    }

    /**
     * Get AnalyserNode for waveform
     * @returns {AnalyserNode|null}
     */
    getAnalyzer() {
        return this.analyzerNode;
    }

    /**
     * Send raw MIDI data
     * @param {Uint8Array|number[]} data
     */
    sendMidi(data) {
        if (!this.connected || !this.output) return;
        this.output.send(data);
    }

    /**
     * Send Note On
     * @param {number} note
     * @param {number} velocity
     */
    noteOn(note, velocity) {
        this.sendMidi([0x90, note & 0x7F, velocity & 0x7F]);
    }

    /**
     * Send Note Off
     * @param {number} note
     */
    noteOff(note) {
        this.sendMidi([0x80, note & 0x7F, 0]);
    }

    /**
     * Set parameter value (CC)
     * @param {number} id - Parameter ID
     * @param {number} value - Normalized value (0-1)
     */
    setParam(id, value) {
        // Find parameter definition
        const param = this.params.find(p => p.id === id);
        const cc = param?.cc ?? id;

        // Convert 0-1 to 0-127
        const ccValue = Math.round(Math.max(0, Math.min(1, value)) * 127);
        this.sendMidi([0xB0, cc & 0x7F, ccValue]);
    }

    /**
     * Send shell command
     * @param {string} text
     */
    sendShellCommand(text) {
        this._sendShellInput(text + '\r');
    }

    /**
     * Send shell input data
     * @private
     */
    _sendShellInput(text) {
        if (!this.connected || !this.output) return;

        const encoder = new TextEncoder();
        const data = encoder.encode(text);
        const msg = buildMessage(Command.STDIN_DATA, this.txSequence++, data);
        console.log('[HardwareBackend] Sending SysEx:', Array.from(msg).map(b => b.toString(16).padStart(2, '0')).join(' '));
        this.output.send(msg);
    }

    /**
     * Send ping to check connection
     */
    sendPing() {
        if (!this.connected || !this.output) return;
        const msg = buildMessage(Command.PING, this.txSequence++, []);
        this.output.send(msg);
    }

    /**
     * Send reset command
     */
    reset() {
        if (!this.connected || !this.output) return;
        const msg = buildMessage(Command.RESET, this.txSequence++, []);
        this.output.send(msg);
    }

    /**
     * Get application info
     * @returns {object}
     */
    getAppInfo() {
        return {
            name: this.deviceName || 'Hardware',
            vendor: 'UMI',
            version: '1.0.0'
        };
    }

    /**
     * Check if connected
     * @returns {boolean}
     */
    isPlaying() {
        return this.connected;
    }

    /**
     * Get state - request via SysEx
     */
    getState() {
        this.requestKernelState();
        return null;
    }

    /**
     * Request kernel state via SysEx
     */
    requestKernelState() {
        if (!this.connected || !this.output) return;
        const msg = buildMessage(Command.STATUS_REQUEST, this.txSequence++, []);
        this.output.send(msg);
    }

    /**
     * Handle MIDI messages
     * @private
     */
    _onMidiMessage(event) {
        const data = event.data;
        if (!data || data.length === 0) return;

        // Check for SysEx
        if (data[0] === 0xF0) {
            console.log('[HardwareBackend] Received SysEx:', Array.from(data).map(b => b.toString(16).padStart(2, '0')).join(' '));
            const msg = parseMessage(data);
            console.log('[HardwareBackend] Parsed message:', msg);
            if (msg) {
                this._handleSysEx(msg);
            } else {
                console.warn('[HardwareBackend] Failed to parse SysEx');
            }
            return;
        }

        // Regular MIDI - forward to onMessage
        if (this.onMessage) {
            this.onMessage({ type: 'midi', data: Array.from(data) });
        }
    }

    /**
     * Handle SysEx messages
     * @private
     */
    _handleSysEx(msg) {
        switch (msg.command) {
            case Command.STDOUT_DATA: {
                const text = new TextDecoder().decode(new Uint8Array(msg.payload));
                if (this.onShellOutput) {
                    this.onShellOutput(text, 'stdout');
                }
                if (this.onMessage) {
                    this.onMessage({ type: 'shell', stream: 'stdout', text });
                }
                break;
            }
            case Command.STDERR_DATA: {
                const text = new TextDecoder().decode(new Uint8Array(msg.payload));
                if (this.onShellOutput) {
                    this.onShellOutput(text, 'stderr');
                }
                if (this.onMessage) {
                    this.onMessage({ type: 'shell', stream: 'stderr', text });
                }
                break;
            }
            case Command.PONG: {
                if (this.onMessage) {
                    this.onMessage({ type: 'pong' });
                }
                break;
            }
            case Command.VERSION: {
                if (msg.payload.length >= 2) {
                    const version = `${msg.payload[0]}.${msg.payload[1]}`;
                    if (this.onMessage) {
                        this.onMessage({ type: 'version', version });
                    }
                }
                break;
            }
            case Command.STATUS_RESPONSE: {
                // Parse kernel status from payload
                // Format: [dspLoad_hi, dspLoad_lo, sampleRate/1000, bufferSize/16, uptime_bytes...]
                if (msg.payload.length >= 4) {
                    const dspLoad = (msg.payload[0] << 8) | msg.payload[1];
                    const sampleRate = msg.payload[2] * 1000;
                    const bufferSize = msg.payload[3] * 16;
                    let uptime = 0;
                    if (msg.payload.length >= 8) {
                        uptime = (msg.payload[4] << 24) | (msg.payload[5] << 16) |
                                 (msg.payload[6] << 8) | msg.payload[7];
                    }
                    if (this.onMessage) {
                        this.onMessage({
                            type: 'kernel-state',
                            dspLoad,
                            sampleRate,
                            bufferSize,
                            uptime,
                            state: 'running'
                        });
                    }
                }
                break;
            }
        }
    }

    /**
     * Handle MIDI state changes
     * @private
     */
    _onStateChange(event) {
        const port = event.port;
        if (port.state === 'disconnected') {
            if (this.input && port.id === this.input.id) {
                this.stop();
                if (this.onError) {
                    this.onError(new Error('Device disconnected'));
                }
            }
        }
    }
}
