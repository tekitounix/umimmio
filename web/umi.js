/**
 * UMI-OS Web Interface
 * 
 * Main JavaScript module for browser integration.
 * Handles AudioContext, MIDI, and WASM initialization.
 */

class UmiSynth {
  constructor() {
    this.audioContext = null;
    this.workletNode = null;
    this.wasmModule = null;
    this.isReady = false;
    this.midiAccess = null;
  }
  
  /**
   * Initialize the synthesizer
   * @param {Object} options - Configuration options
   * @param {number} options.sampleRate - Sample rate (default: 48000)
   * @returns {Promise<void>}
   */
  async init(options = {}) {
    const sampleRate = options.sampleRate || 48000;
    
    // Create AudioContext
    this.audioContext = new AudioContext({ sampleRate });
    
    // Load WASM module
    const wasmUrl = options.wasmUrl || '../.build/wasm/umi_synth.js';
    try {
      const module = await import(wasmUrl);
      this.wasmModule = await module.default();
      console.log('WASM module loaded');
    } catch (e) {
      console.error('Failed to load WASM:', e);
      throw e;
    }
    
    // Initialize WASM synth
    this.wasmModule._umi_create(sampleRate);
    
    // Create ScriptProcessor fallback (simpler than AudioWorklet for demo)
    this.setupScriptProcessor();
    
    this.isReady = true;
    console.log('UMI Synth ready');
  }
  
  /**
   * Setup ScriptProcessor for audio output
   * (AudioWorklet version is more complex due to WASM memory sharing)
   */
  setupScriptProcessor() {
    const bufferSize = 256;
    const processor = this.audioContext.createScriptProcessor(bufferSize, 0, 1);
    
    processor.onaudioprocess = (event) => {
      const output = event.outputBuffer.getChannelData(0);
      
      // Get buffer pointer and process
      const bufferPtr = this.wasmModule._umi_get_buffer_ptr(null);
      this.wasmModule._umi_process(null, bufferPtr, bufferSize);
      
      // Copy from WASM memory to output
      const wasmBuffer = new Float32Array(
        this.wasmModule.HEAPF32.buffer,
        bufferPtr,
        bufferSize
      );
      output.set(wasmBuffer);
    };
    
    processor.connect(this.audioContext.destination);
    this.processorNode = processor;
  }
  
  /**
   * Start audio (required after user gesture)
   */
  async start() {
    if (this.audioContext.state === 'suspended') {
      await this.audioContext.resume();
    }
  }
  
  /**
   * Stop audio
   */
  async stop() {
    if (this.audioContext.state === 'running') {
      await this.audioContext.suspend();
    }
  }
  
  /**
   * Send note on
   * @param {number} note - MIDI note number (0-127)
   * @param {number} velocity - Velocity (0-127)
   */
  noteOn(note, velocity = 100) {
    if (!this.isReady) return;
    this.wasmModule._umi_note_on(null, note, velocity);
  }
  
  /**
   * Send note off
   * @param {number} note - MIDI note number (0-127)
   */
  noteOff(note) {
    if (!this.isReady) return;
    this.wasmModule._umi_note_off(null, note);
  }
  
  /**
   * Set parameter
   * @param {number} id - Parameter ID
   * @param {number} value - Parameter value
   */
  setParam(id, value) {
    if (!this.isReady) return;
    this.wasmModule._umi_set_param(null, id, value);
  }
  
  /**
   * Set master volume (0-1)
   */
  setVolume(value) {
    this.setParam(0, value);
  }
  
  /**
   * Set filter cutoff (20-20000 Hz)
   */
  setFilterCutoff(value) {
    this.setParam(1, value);
  }
  
  /**
   * Set filter resonance (0-1)
   */
  setFilterResonance(value) {
    this.setParam(2, value);
  }
  
  /**
   * Initialize Web MIDI
   * @returns {Promise<void>}
   */
  async initMidi() {
    if (!navigator.requestMIDIAccess) {
      console.warn('Web MIDI not supported');
      return;
    }
    
    try {
      this.midiAccess = await navigator.requestMIDIAccess();
      
      // Connect all MIDI inputs
      for (const input of this.midiAccess.inputs.values()) {
        this.connectMidiInput(input);
      }
      
      // Listen for new devices
      this.midiAccess.onstatechange = (event) => {
        if (event.port.type === 'input' && event.port.state === 'connected') {
          this.connectMidiInput(event.port);
        }
      };
      
      console.log('MIDI initialized');
    } catch (e) {
      console.error('MIDI initialization failed:', e);
    }
  }
  
  /**
   * Connect a MIDI input
   */
  connectMidiInput(input) {
    console.log(`MIDI input connected: ${input.name}`);
    
    input.onmidimessage = (event) => {
      const [status, data1, data2] = event.data;
      const command = status & 0xf0;
      
      switch (command) {
        case 0x90: // Note On
          if (data2 > 0) {
            this.noteOn(data1, data2);
          } else {
            this.noteOff(data1);
          }
          break;
        case 0x80: // Note Off
          this.noteOff(data1);
          break;
        case 0xb0: // Control Change
          this.handleCC(data1, data2);
          break;
      }
    };
  }
  
  /**
   * Handle MIDI CC
   */
  handleCC(cc, value) {
    const normalized = value / 127;
    
    switch (cc) {
      case 1:  // Mod wheel -> filter cutoff
        this.setFilterCutoff(20 + normalized * 19980);
        break;
      case 7:  // Volume
        this.setVolume(normalized);
        break;
      case 74: // Cutoff (standard)
        this.setFilterCutoff(20 + normalized * 19980);
        break;
      case 71: // Resonance (standard)
        this.setFilterResonance(normalized);
        break;
    }
  }
  
  /**
   * Cleanup
   */
  destroy() {
    if (this.processorNode) {
      this.processorNode.disconnect();
    }
    if (this.audioContext) {
      this.audioContext.close();
    }
  }
}

// Export for ES modules and global scope
export { UmiSynth };

// Make available globally for non-module scripts
if (typeof window !== 'undefined') {
  window.UmiSynth = UmiSynth;
}
