/**
 * UMI-OS Synth Simulator - AudioWorklet Processor
 *
 * Runs the WASM synth in the audio worklet thread for glitch-free audio.
 * Uses raw WASM instantiation (no Emscripten runtime in worklet).
 */

class SynthWorkletProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();

        this.wasmReady = false;
        this.wasmInstance = null;
        this.wasmMemory = null;
        this.fn = null;
        this.kernel = null;  // Kernel state functions
        this.sysex = null;   // SysEx IO functions
        this.shell = null;   // Shell functions
        this.inputPtr = 65536;  // Fixed buffer location
        this.outputPtr = 65536 + 128 * 4;
        this.sysexBuffer = new Uint8Array(4096);
        this.shellInputPtr = 65536 + 128 * 4 + 128 * 4;  // After output buffer

        // HW configuration (simulated values)
        this.hwConfig = {
            sramTotalKb: 128,
            heapKb: 48,
            dataBssKb: 8,
            mainStackKb: 2,
            taskStackBytes: 1024,
            taskCount: 4,
            dmaBufferKb: 2,
            sharedMemKb: 1
        };

        // Handle messages from main thread
        this.port.onmessage = (e) => this.handleMessage(e.data);
    }

    handleMessage(msg) {
        switch (msg.type) {
            case 'init':
                this.initWasm(msg.wasmBytes);
                break;
            case 'note-on':
                if (this.wasmReady && this.fn) {
                    console.log('[SynthWorklet] noteOn:', msg.note, msg.velocity);
                    this.fn.noteOn(msg.note, msg.velocity);
                }
                break;
            case 'note-off':
                if (this.wasmReady && this.fn) {
                    console.log('[SynthWorklet] noteOff:', msg.note);
                    this.fn.noteOff(msg.note);
                }
                break;
            case 'midi':
                if (this.wasmReady && this.fn) {
                    this.fn.midi(msg.status, msg.data1, msg.data2);
                }
                break;
            case 'set-param':
                if (this.wasmReady && this.fn) {
                    if (this.fn.setParam) {
                        this.fn.setParam(msg.id, msg.value);
                    } else {
                        console.error('[SynthWorklet] setParam function not available!');
                    }
                }
                break;
            case 'get-params':
                if (this.wasmReady && this.fn) {
                    this.sendParamInfo();
                }
                break;
            case 'get-kernel-state':
                if (this.wasmReady) {
                    this.sendKernelState();
                }
                break;
            case 'reset-dsp-peak':
                if (this.wasmReady && this.kernel && this.kernel.resetDspPeak) {
                    this.kernel.resetDspPeak();
                }
                break;
            case 'set-rtc':
                if (this.wasmReady && this.kernel && this.kernel.setRtc) {
                    const lo = msg.epoch & 0xFFFFFFFF;
                    const hi = Math.floor(msg.epoch / 0x100000000);
                    this.kernel.setRtc(lo, hi);
                }
                break;
            case 'reset':
                if (this.wasmReady && this.fn && this.fn.reset) {
                    this.fn.reset();
                }
                break;
            case 'shell-input':
                console.log('[SynthWorklet] shell-input received:', msg.text, 'wasmReady=', this.wasmReady, 'shell=', !!this.shell);
                if (this.wasmReady && this.shell) {
                    this.handleShellInput(msg.text);
                } else {
                    console.warn('[SynthWorklet] Shell not available: wasmReady=', this.wasmReady, 'shell=', this.shell);
                }
                break;
            case 'get-sysex':
                if (this.wasmReady && this.sysex) {
                    this.sendSysExData();
                }
                break;
            case 'hw-settings':
                if (this.wasmReady && this.hw) {
                    this.applyHwSettings(msg.settings);
                }
                break;
            case 'get-irq-state':
                if (this.wasmReady && this.irq) {
                    this.sendIrqState();
                }
                break;
            case 'trigger-irq':
                if (this.wasmReady && this.irq && this.irq.trigger) {
                    this.irq.trigger(msg.flag);
                }
                break;
            case 'set-adc':
                if (this.wasmReady && this.hwState && this.hwState.setAdc) {
                    this.hwState.setAdc(msg.channel, msg.value);
                }
                break;
            case 'set-encoder':
                if (this.wasmReady && this.hwState && this.hwState.setEncoder) {
                    this.hwState.setEncoder(msg.encoder, msg.position);
                }
                break;
            case 'trigger-gpio':
                if (this.wasmReady && this.hwState && this.hwState.triggerGpio) {
                    this.hwState.triggerGpio(msg.pinMask);
                }
                break;
            case 'send-notify':
                if (this.wasmReady && this.notify && this.notify.send) {
                    this.notify.send(msg.task, msg.bits);
                }
                break;
        }
    }

    applyHwSettings(settings) {
        const hw = this.hw;
        if (!hw) return;

        // CPU timing parameters
        if (hw.setCpuFreq) hw.setCpuFreq(settings.cpuFreq || 168);
        if (hw.setIsrOverhead) hw.setIsrOverhead(settings.isrOverhead || 100);
        if (hw.setBaseCycles) hw.setBaseCycles(settings.baseCycles || 20);
        if (hw.setVoiceCycles) hw.setVoiceCycles(settings.voiceCycles || 200);

        // Battery state
        if (this.kernel && this.kernel.setBattery) {
            this.kernel.setBattery(
                settings.batteryPercent || 100,
                settings.batteryCharging ? 1 : 0,
                settings.batteryVoltage || 4200
            );
        }

        // Watchdog
        if (this.kernel) {
            if (settings.wdtEnabled && settings.wdtTimeout > 0) {
                if (this.kernel.wdtInit) {
                    this.kernel.wdtInit(settings.wdtTimeout);
                }
            }
        }

        // Update hwConfig for memory layout simulation
        if (settings.sramTotalKb) this.hwConfig.sramTotalKb = settings.sramTotalKb;
        if (settings.heapKb) this.hwConfig.heapKb = settings.heapKb;
        if (settings.dataBssKb) this.hwConfig.dataBssKb = settings.dataBssKb;
        if (settings.mainStackKb) this.hwConfig.mainStackKb = settings.mainStackKb;
        if (settings.taskStackBytes) this.hwConfig.taskStackBytes = settings.taskStackBytes;
        if (settings.taskCount) this.hwConfig.taskCount = settings.taskCount;
        if (settings.dmaBufferKb) this.hwConfig.dmaBufferKb = settings.dmaBufferKb;
        if (settings.sharedMemKb) this.hwConfig.sharedMemKb = settings.sharedMemKb;
    }

    handleShellInput(text) {
        console.log('[SynthWorklet] handleShellInput:', text);
        if (!this.shell || !this.wasmMemory) {
            console.warn('[SynthWorklet] handleShellInput: shell or memory not available');
            return;
        }

        // Write input string to WASM memory at shellInputPtr
        // Note: TextEncoder is not available in AudioWorklet, use manual UTF-8 encoding
        const str = text + '\n';
        const bytes = new Uint8Array(str.length);
        for (let i = 0; i < str.length; i++) {
            bytes[i] = str.charCodeAt(i) & 0xFF;  // ASCII only
        }
        const heap = new Uint8Array(this.wasmMemory.buffer);

        // Find a safe location in WASM memory (after output buffer)
        // Use a fixed address that's beyond our buffers
        const inputPtr = 0x20000;  // 128KB - safe area

        heap.set(bytes, inputPtr);

        // Send to shell
        if (this.shell.input) {
            console.log('[SynthWorklet] Calling shell.input with ptr=', inputPtr, 'len=', bytes.length);
            this.shell.input(inputPtr, bytes.length);
        } else {
            console.warn('[SynthWorklet] shell.input not available');
        }

        // Check if command is ready and execute
        console.log('[SynthWorklet] Checking hasCommand...');
        if (this.shell.hasCommand && this.shell.hasCommand() !== 0) {
            console.log('[SynthWorklet] Executing command...');
            const resultPtr = this.shell.execute();
            const result = this.readCString(resultPtr);
            console.log('[SynthWorklet] Shell result:', result);
            this.port.postMessage({ type: 'shell-output', text: result });
        } else {
            console.log('[SynthWorklet] No command ready, hasCommand=', !!this.shell.hasCommand);
        }
    }

    sendSysExData() {
        if (!this.sysex || !this.sysex.available) return;

        const available = this.sysex.available();
        if (available === 0) return;

        // Read SysEx data from WASM
        const toRead = Math.min(available, this.sysexBuffer.length);
        const heap = new Uint8Array(this.wasmMemory.buffer);

        // Use a fixed location in WASM memory for reading SysEx data
        const tempPtr = 0x21000;  // 132KB - safe area for SysEx buffer
        const bytesRead = this.sysex.read(tempPtr, toRead);

        if (bytesRead > 0) {
            // Copy to our buffer
            const data = heap.slice(tempPtr, tempPtr + bytesRead);
            this.port.postMessage({ type: 'sysex-data', data: data });
        }
    }

    sendParamInfo() {
        const count = this.fn.paramCount();
        const params = [];
        for (let i = 0; i < count; i++) {
            // Read string from WASM memory
            const namePtr = this.fn.paramName(i);
            const name = this.readCString(namePtr);
            params.push({
                id: i,
                name: name,
                min: this.fn.paramMin(i),
                max: this.fn.paramMax(i),
                default: this.fn.paramDefault(i),
                value: this.fn.getParam(i),
                curve: this.fn.paramCurve ? this.fn.paramCurve(i) : 0,  // 0=Linear, 1=Log, 2=Exp
            });
        }
        this.port.postMessage({ type: 'params', params, sampleRate: this.fn.sampleRate() });
    }

    sendKernelState() {
        const k = this.kernel || {};  // Use empty object if kernel not available

        // Combine 32-bit hi/lo pairs into 64-bit values
        const uptime = k.uptimeLo ? (k.uptimeHi() * 0x100000000 + k.uptimeLo()) : 0;
        const rtc = k.rtcLo ? (k.rtcHi() * 0x100000000 + k.rtcLo()) : 0;
        const idleTime = k.idleTimeLo ? (k.idleTimeHi() * 0x100000000 + k.idleTimeLo()) : 0;

        // Build task list
        const taskCount = k.taskCount ? k.taskCount() : 4;
        const tasks = [];
        for (let i = 0; i < taskCount; i++) {
            const namePtr = k.taskName ? k.taskName(i) : 0;
            const name = namePtr ? this.readCString(namePtr) : `Task${i}`;
            const runTime = k.taskRunTimeLo
                ? (k.taskRunTimeHi(i) * 0x100000000 + k.taskRunTimeLo(i))
                : 0;
            const waitReason = this.notify && this.notify.waitReason
                ? this.notify.waitReason(i) : 0;
            tasks.push({
                id: i,
                name: name,
                state: k.taskState ? k.taskState(i) : 0,
                runTime: runTime,
                runCount: k.taskRunCount ? k.taskRunCount(i) : 0,
                waitReason: waitReason,
            });
        }

        this.port.postMessage({
            type: 'kernel-state',
            // Time
            uptime: uptime,
            rtc: rtc,
            // Audio
            bufferCount: k.audioBuffers ? k.audioBuffers() : 0,
            dropCount: k.audioDrops ? k.audioDrops() : 0,
            dspLoad: k.dspLoad ? k.dspLoad() : 0,
            dspPeak: k.dspPeak ? k.dspPeak() : 0,
            audioRunning: k.audioRunning ? k.audioRunning() !== 0 : false,
            // MIDI
            midiRx: k.midiRx ? k.midiRx() : 0,
            midiTx: k.midiTx ? k.midiTx() : 0,
            // Power
            batteryPercent: k.batteryPercent ? k.batteryPercent() : 100,
            batteryCharging: k.batteryCharging ? k.batteryCharging() !== 0 : false,
            batteryVoltage: k.batteryVoltage ? k.batteryVoltage() : 4200,
            usbConnected: k.usbConnected ? k.usbConnected() !== 0 : true,
            // Watchdog
            wdtEnabled: k.wdtEnabled ? k.wdtEnabled() !== 0 : false,
            wdtTimeout: k.wdtTimeout ? k.wdtTimeout() : 0,
            wdtExpired: k.wdtExpired ? k.wdtExpired() !== 0 : false,
            // Tasks
            taskCount: taskCount,
            taskReady: k.taskReady ? k.taskReady() : 1,
            taskBlocked: k.taskBlocked ? k.taskBlocked() : 0,
            ctxSwitches: k.ctxSwitches ? k.ctxSwitches() : 0,
            currentTask: k.currentTask ? k.currentTask() : 0,
            tasks: tasks,
            idleTime: idleTime,
            cpuUtil: k.cpuUtil ? k.cpuUtil() : 0,
            // Memory
            heapUsed: k.heapUsed ? k.heapUsed() : 0,
            heapTotal: k.heapTotal ? k.heapTotal() : this.hwConfig.heapKb * 1024,
            heapPeak: k.heapPeak ? k.heapPeak() : 0,
            stackUsed: k.stackUsed ? k.stackUsed() : 1024,
            stackTotal: k.stackTotal ? k.stackTotal() : this.hwConfig.mainStackKb * 1024,
            sramTotal: k.sramTotal ? k.sramTotal() : this.hwConfig.sramTotalKb * 1024,
            freeGap: this.getMemoryLayout().free,
            flashTotal: k.flashTotal ? k.flashTotal() : 524288,  // 512KB
            flashUsed: k.flashUsed ? k.flashUsed() : 65536,  // 64KB
            // Memory layout for visualization
            memLayout: this.getMemoryLayout(),
            memDetails: {
                dataBss: this.hwConfig.dataBssKb * 1024,
                taskStacks: this.hwConfig.taskStackBytes * this.hwConfig.taskCount,
                mainStack: this.hwConfig.mainStackKb * 1024,
                dma: this.hwConfig.dmaBufferKb * 1024,
                shared: this.hwConfig.sharedMemKb * 1024,
            },
            // IRQ Statistics
            irqCount: k.irqCount ? k.irqCount() : 0,
            audioIrqCount: k.audioIrqCount ? k.audioIrqCount() : 0,
            timerIrqCount: k.timerIrqCount ? k.timerIrqCount() : 0,
            dmaIrqCount: k.dmaIrqCount ? k.dmaIrqCount() : 0,
            systickCount: k.systickCount ? k.systickCount() : 0,
            // Error Statistics
            hardfaultCount: k.hardfaultCount ? k.hardfaultCount() : 0,
            stackOverflowCount: k.stackOverflowCount ? k.stackOverflowCount() : 0,
            mallocFailCount: k.mallocFailCount ? k.mallocFailCount() : 0,
            wdtResetCount: k.wdtResetCount ? k.wdtResetCount() : 0,
        });
    }

    getMemoryLayout() {
        const cfg = this.hwConfig;
        const sramTotal = cfg.sramTotalKb * 1024;
        const dataBss = cfg.dataBssKb * 1024;
        const heap = cfg.heapKb * 1024;
        const taskStacks = cfg.taskStackBytes * cfg.taskCount;
        const mainStack = cfg.mainStackKb * 1024;
        const dma = (cfg.dmaBufferKb || 2) * 1024;
        const shared = (cfg.sharedMemKb || 1) * 1024;

        // Calculate free space
        const used = dataBss + heap + taskStacks + mainStack + dma + shared;
        const free = Math.max(0, sramTotal - used);

        return {
            dataBss,
            heap,
            free,
            taskStacks,
            mainStack,
            dma,
            shared
        };
    }

    readCString(ptr) {
        if (!ptr) return '';
        const heap = new Uint8Array(this.wasmMemory.buffer);
        let end = ptr;
        while (heap[end] !== 0 && end < heap.length) end++;
        // Manual UTF-8 decode (TextDecoder not available in AudioWorklet)
        let str = '';
        for (let i = ptr; i < end; i++) {
            str += String.fromCharCode(heap[i]);
        }
        return str;
    }

    async initWasm(wasmBytes) {
        try {
            // ASM_CONSTS table for EM_ASM calls
            // Address 5372 => location.reload() (from synth_sim.js for synth archive WASM)
            // Note: Different WASM builds may use different addresses
            const ASM_CONSTS = {
                5372: () => { /* location.reload() - no-op in worklet */ },
                4612: () => { /* location.reload() - no-op in worklet */ }
            };

            // Import object for WASM
            // With -sSTANDALONE_WASM=1, imports use 'env' module with full names
            const importObject = {
                env: {
                    // emscripten_asm_const_int - for EM_ASM calls
                    emscripten_asm_const_int: (code, sigPtr, argbuf) => {
                        // Execute ASM_CONST function if available
                        if (ASM_CONSTS[code]) {
                            return ASM_CONSTS[code]();
                        }
                        return 0;
                    }
                },
                // Fallback for minified imports (older builds)
                a: {
                    a: (requestedSize) => 0,
                    b: (code, sigPtr, argbuf) => {
                        if (ASM_CONSTS[code]) return ASM_CONSTS[code]();
                        return 0;
                    }
                }
            };

            // Instantiate WASM
            const result = await WebAssembly.instantiate(wasmBytes, importObject);
            this.wasmInstance = result.instance;
            const exports = this.wasmInstance.exports;

            // Debug: log all exports
            console.log('[SynthWorklet] WASM exports:', Object.keys(exports).sort().join(', '));

            // Get memory (check both minified and full names)
            this.wasmMemory = exports.memory || exports.c;
            console.log('[SynthWorklet] Memory found:', !!this.wasmMemory);

            // Initialize Emscripten runtime
            // With STANDALONE_WASM: _initialize instead of __wasm_call_ctors
            // With minified: exports.d
            const wasmInit = exports._initialize || exports.d;
            if (wasmInit) {
                console.log('[SynthWorklet] Calling WASM initializer...');
                wasmInit();
                console.log('[SynthWorklet] WASM runtime initialized');
            } else {
                console.warn('[SynthWorklet] WASM initializer not found!');
            }

            // Create export function mappings based on synth_sim.js assignWasmExports()
            // d=__wasm_call_ctors (init runtime), e=umi_sim_init, f=umi_sim_reset, etc.
            this.fn = {
                init: exports.e || exports.umi_sim_init || exports._umi_sim_init,
                reset: exports.f || exports.umi_sim_reset || exports._umi_sim_reset,
                process: exports.g || exports.umi_sim_process || exports._umi_sim_process,
                noteOn: exports.h || exports.umi_sim_note_on || exports._umi_sim_note_on,
                noteOff: exports.i || exports.umi_sim_note_off || exports._umi_sim_note_off,
                cc: exports.j || exports.umi_sim_cc || exports._umi_sim_cc,
                midi: exports.k || exports.umi_sim_midi || exports._umi_sim_midi,
                load: exports.l || exports.umi_sim_load || exports._umi_sim_load,
                positionLo: exports.m || exports.umi_sim_position_lo || exports._umi_sim_position_lo,
                positionHi: exports.n || exports.umi_sim_position_hi || exports._umi_sim_position_hi,
                sampleRate: exports.o || exports.umi_sim_sample_rate || exports._umi_sim_sample_rate,
                paramCount: exports.p || exports.umi_sim_param_count || exports._umi_sim_param_count,
                setParam: exports.q || exports.umi_sim_set_param || exports._umi_sim_set_param,
                getParam: exports.r || exports.umi_sim_get_param || exports._umi_sim_get_param,
                paramName: exports.s || exports.umi_sim_param_name || exports._umi_sim_param_name,
                paramMin: exports.t || exports.umi_sim_param_min || exports._umi_sim_param_min,
                paramMax: exports.u || exports.umi_sim_param_max || exports._umi_sim_param_max,
                paramDefault: exports.v || exports.umi_sim_param_default || exports._umi_sim_param_default,
                paramCurve: exports.umi_get_param_curve || exports._umi_get_param_curve,
            };

            // Kernel state functions (may have different minified names)
            // We'll try to find them by scanning exports
            this.kernel = this.mapKernelExports(exports);

            // SysEx IO functions
            this.sysex = this.mapSysExExports(exports);

            // Shell functions
            this.shell = this.mapShellExports(exports);

            // Debug: log all exports containing 'shell'
            const shellExports = Object.keys(exports).filter(k => k.toLowerCase().includes('shell'));
            console.log('[SynthWorklet] Shell-related exports:', shellExports);

            // Debug: verify shell function mappings
            console.log('[SynthWorklet] Shell function mappings:',
                'input=', !!this.shell?.input,
                'hasCommand=', !!this.shell?.hasCommand,
                'getCommand=', !!this.shell?.getCommand,
                'execute=', !!this.shell?.execute);

            // HW simulation parameter functions
            this.hw = this.mapHwExports(exports);

            // IRQ flag functions
            this.irq = this.mapIrqExports(exports);

            // Task notification functions
            this.notify = this.mapNotifyExports(exports);

            // HW state (shared memory) functions
            this.hwState = this.mapHwStateExports(exports);

            // Shared memory functions
            this.shm = this.mapShmExports(exports);

            // Debug: verify core function mappings
            console.log('[SynthWorklet] Core function mappings:',
                'init=', !!this.fn.init,
                'reset=', !!this.fn.reset,
                'process=', !!this.fn.process,
                'noteOn=', !!this.fn.noteOn,
                'noteOff=', !!this.fn.noteOff,
                'setParam=', !!this.fn.setParam,
                'getParam=', !!this.fn.getParam,
                'paramCount=', !!this.fn.paramCount);

            // Debug: log actual export names found
            const paramExports = Object.keys(exports).filter(k => k.includes('param'));
            console.log('[SynthWorklet] Param-related exports:', paramExports);

            // Initialize synth
            if (this.fn.init) {
                console.log('[SynthWorklet] Calling init()...');
                this.fn.init();
                console.log('[SynthWorklet] init() completed');
            } else {
                console.error('[SynthWorklet] init function not found!');
            }

            this.wasmReady = true;
            this.port.postMessage({ type: 'ready' });

            // Send initial parameter info
            this.sendParamInfo();

        } catch (err) {
            console.error('[SynthWorklet] Init error:', err);
            this.port.postMessage({ type: 'error', message: err.message });
        }
    }

    mapKernelExports(exports) {
        // Based on synth_sim.js assignWasmExports() mapping
        // Updated for new memory/IRQ/error exports
        // Note: exports use umi_ prefix (no leading underscore)
        const kernelFns = {
            // Time
            uptimeLo: exports.z || exports.umi_kernel_uptime_lo || exports._umi_kernel_uptime_lo,
            uptimeHi: exports.A || exports.umi_kernel_uptime_hi || exports._umi_kernel_uptime_hi,
            rtcLo: exports.B || exports.umi_kernel_rtc_lo || exports._umi_kernel_rtc_lo,
            rtcHi: exports.C || exports.umi_kernel_rtc_hi || exports._umi_kernel_rtc_hi,
            setRtc: exports.D || exports.umi_kernel_set_rtc || exports._umi_kernel_set_rtc,
            // Audio
            audioBuffers: exports.E || exports.umi_kernel_audio_buffers || exports._umi_kernel_audio_buffers,
            audioDrops: exports.F || exports.umi_kernel_audio_drops || exports._umi_kernel_audio_drops,
            dspLoad: exports.G || exports.umi_kernel_dsp_load || exports._umi_kernel_dsp_load,
            dspPeak: exports.H || exports.umi_kernel_dsp_peak || exports._umi_kernel_dsp_peak,
            resetDspPeak: exports.I || exports.umi_kernel_reset_dsp_peak || exports._umi_kernel_reset_dsp_peak,
            audioRunning: exports.J || exports.umi_kernel_audio_running || exports._umi_kernel_audio_running,
            // MIDI
            midiRx: exports.K || exports.umi_kernel_midi_rx || exports._umi_kernel_midi_rx,
            midiTx: exports.L || exports.umi_kernel_midi_tx || exports._umi_kernel_midi_tx,
            // Power
            batteryPercent: exports.M || exports.umi_kernel_battery_percent || exports._umi_kernel_battery_percent,
            batteryCharging: exports.N || exports.umi_kernel_battery_charging || exports._umi_kernel_battery_charging,
            usbConnected: exports.O || exports.umi_kernel_usb_connected || exports._umi_kernel_usb_connected,
            batteryVoltage: exports.P || exports.umi_kernel_battery_voltage || exports._umi_kernel_battery_voltage,
            setBattery: exports.Q || exports.umi_kernel_set_battery || exports._umi_kernel_set_battery,
            // Watchdog
            wdtEnabled: exports.R || exports.umi_kernel_watchdog_enabled || exports._umi_kernel_watchdog_enabled,
            wdtTimeout: exports.S || exports.umi_kernel_watchdog_timeout || exports._umi_kernel_watchdog_timeout,
            wdtExpired: exports.T || exports.umi_kernel_watchdog_expired || exports._umi_kernel_watchdog_expired,
            wdtInit: exports.U || exports.umi_kernel_watchdog_init || exports._umi_kernel_watchdog_init,
            wdtFeed: exports.V || exports.umi_kernel_watchdog_feed || exports._umi_kernel_watchdog_feed,
            // Tasks (basic)
            taskCount: exports.W || exports.umi_kernel_task_count || exports._umi_kernel_task_count,
            taskReady: exports.X || exports.umi_kernel_task_ready || exports._umi_kernel_task_ready,
            taskBlocked: exports.Y || exports.umi_kernel_task_blocked || exports._umi_kernel_task_blocked,
            ctxSwitches: exports.Z || exports.umi_kernel_context_switches || exports._umi_kernel_context_switches,
            // Tasks (extended)
            currentTask: exports._ || exports.umi_kernel_current_task || exports._umi_kernel_current_task,
            taskName: exports.$ || exports.umi_kernel_task_name || exports._umi_kernel_task_name,
            taskState: exports.aa || exports.umi_kernel_task_state || exports._umi_kernel_task_state,
            taskRunTimeLo: exports.ba || exports.umi_kernel_task_run_time_lo || exports._umi_kernel_task_run_time_lo,
            taskRunTimeHi: exports.ca || exports.umi_kernel_task_run_time_hi || exports._umi_kernel_task_run_time_hi,
            taskRunCount: exports.da || exports.umi_kernel_task_run_count || exports._umi_kernel_task_run_count,
            idleTimeLo: exports.ea || exports.umi_kernel_idle_time_lo || exports._umi_kernel_idle_time_lo,
            idleTimeHi: exports.fa || exports.umi_kernel_idle_time_hi || exports._umi_kernel_idle_time_hi,
            cpuUtil: exports.ga || exports.umi_kernel_cpu_util || exports._umi_kernel_cpu_util,
            // Memory
            heapUsed: exports.ha || exports.umi_kernel_heap_used || exports._umi_kernel_heap_used,
            heapTotal: exports.ia || exports.umi_kernel_heap_total || exports._umi_kernel_heap_total,
            heapPeak: exports.ja || exports.umi_kernel_heap_peak || exports._umi_kernel_heap_peak,
            stackUsed: exports.ka || exports.umi_kernel_stack_used || exports._umi_kernel_stack_used,
            stackTotal: exports.la || exports.umi_kernel_stack_total || exports._umi_kernel_stack_total,
            sramTotal: exports.ma || exports.umi_kernel_sram_total || exports._umi_kernel_sram_total,
            flashTotal: exports.na || exports.umi_kernel_flash_total || exports._umi_kernel_flash_total,
            flashUsed: exports.oa || exports.umi_kernel_flash_used || exports._umi_kernel_flash_used,
            // IRQ Statistics
            irqCount: exports.pa || exports.umi_kernel_irq_count || exports._umi_kernel_irq_count,
            audioIrqCount: exports.qa || exports.umi_kernel_audio_irq_count || exports._umi_kernel_audio_irq_count,
            timerIrqCount: exports.ra || exports.umi_kernel_timer_irq_count || exports._umi_kernel_timer_irq_count,
            dmaIrqCount: exports.sa || exports.umi_kernel_dma_irq_count || exports._umi_kernel_dma_irq_count,
            systickCount: exports.ta || exports.umi_kernel_systick_count || exports._umi_kernel_systick_count,
            // Error Statistics
            hardfaultCount: exports.ua || exports.umi_kernel_hardfault_count || exports._umi_kernel_hardfault_count,
            stackOverflowCount: exports.va || exports.umi_kernel_stack_overflow_count || exports._umi_kernel_stack_overflow_count,
            mallocFailCount: exports.wa || exports.umi_kernel_malloc_fail_count || exports._umi_kernel_malloc_fail_count,
            wdtResetCount: exports.xa || exports.umi_kernel_watchdog_reset_count || exports._umi_kernel_watchdog_reset_count,
            // Log
            logLevel: exports.ya || exports.umi_kernel_log_level || exports._umi_kernel_log_level,
            setLogLevel: exports.za || exports.umi_kernel_set_log_level || exports._umi_kernel_set_log_level,
            logCount: exports.Aa || exports.umi_kernel_log_count || exports._umi_kernel_log_count,
            // System
            reset: exports.Ba || exports.umi_kernel_reset || exports._umi_kernel_reset,
        };

        // Count valid mappings
        const validCount = Object.values(kernelFns).filter(f => f).length;
        console.log('[SynthWorklet] Mapped', validCount, 'kernel exports');

        // Debug: log available kernel exports
        const kernelExportNames = Object.keys(exports).filter(k => k.includes('kernel'));
        console.log('[SynthWorklet] Available kernel exports:', kernelExportNames.slice(0, 10));

        // If no valid mappings, return null to indicate kernel not available
        if (validCount === 0) {
            console.warn('[SynthWorklet] No kernel exports found');
            return null;
        }

        return kernelFns;
    }

    mapSysExExports(exports) {
        // SysEx IO exports - check both minified and unminified names
        return {
            available: exports.Ca || exports.umi_sysex_available || exports._umi_sysex_available,
            read: exports.Da || exports.umi_sysex_read || exports._umi_sysex_read,
            clear: exports.Ea || exports.umi_sysex_clear || exports._umi_sysex_clear,
            logWrite: exports.Fa || exports.umi_log_write || exports._umi_log_write,
        };
    }

    mapShellExports(exports) {
        // Shell exports - check both minified and unminified names
        return {
            input: exports.Ga || exports.umi_shell_input || exports._umi_shell_input,
            hasCommand: exports.Ha || exports.umi_shell_has_command || exports._umi_shell_has_command,
            getCommand: exports.Ia || exports.umi_shell_get_command || exports._umi_shell_get_command,
            execute: exports.Ja || exports.umi_shell_execute || exports._umi_shell_execute,
        };
    }

    mapHwExports(exports) {
        // HW simulation parameter exports - check both minified and unminified names
        return {
            // CPU timing
            cpuFreq: exports._a || exports.umi_hw_cpu_freq || exports._umi_hw_cpu_freq,
            setCpuFreq: exports.$a || exports.umi_hw_set_cpu_freq || exports._umi_hw_set_cpu_freq,
            isrOverhead: exports.ab || exports.umi_hw_isr_overhead || exports._umi_hw_isr_overhead,
            setIsrOverhead: exports.bb || exports.umi_hw_set_isr_overhead || exports._umi_hw_set_isr_overhead,
            baseCycles: exports.cb || exports.umi_hw_base_cycles || exports._umi_hw_base_cycles,
            setBaseCycles: exports.db || exports.umi_hw_set_base_cycles || exports._umi_hw_set_base_cycles,
            voiceCycles: exports.eb || exports.umi_hw_voice_cycles || exports._umi_hw_voice_cycles,
            setVoiceCycles: exports.fb || exports.umi_hw_set_voice_cycles || exports._umi_hw_set_voice_cycles,
            eventCycles: exports.gb || exports.umi_hw_event_cycles || exports._umi_hw_event_cycles,
            setEventCycles: exports.hb || exports.umi_hw_set_event_cycles || exports._umi_hw_set_event_cycles,
            // Memory configuration
            sramTotalKb: exports.ib || exports.umi_hw_sram_total_kb || exports._umi_hw_sram_total_kb,
            setSramTotalKb: exports.jb || exports.umi_hw_set_sram_total_kb || exports._umi_hw_set_sram_total_kb,
            heapSizeKb: exports.kb || exports.umi_hw_heap_size_kb || exports._umi_hw_heap_size_kb,
            setHeapSizeKb: exports.lb || exports.umi_hw_set_heap_size_kb || exports._umi_hw_set_heap_size_kb,
            taskStackBytes: exports.mb || exports.umi_hw_task_stack_bytes || exports._umi_hw_task_stack_bytes,
            setTaskStackBytes: exports.nb || exports.umi_hw_set_task_stack_bytes || exports._umi_hw_set_task_stack_bytes,
            flashTotalKb: exports.ob || exports.umi_hw_flash_total_kb || exports._umi_hw_flash_total_kb,
            setFlashTotalKb: exports.pb || exports.umi_hw_set_flash_total_kb || exports._umi_hw_set_flash_total_kb,
            flashUsedKb: exports.qb || exports.umi_hw_flash_used_kb || exports._umi_hw_flash_used_kb,
            setFlashUsedKb: exports.rb || exports.umi_hw_set_flash_used_kb || exports._umi_hw_set_flash_used_kb,
        };
    }

    mapIrqExports(exports) {
        // IRQ flag exports - check both minified and unminified names
        return {
            pending: exports.Ka || exports.umi_irq_flag_pending || exports._umi_irq_flag_pending,
            count: exports.La || exports.umi_irq_flag_count || exports._umi_irq_flag_count,
            trigger: exports.Ma || exports.umi_irq_trigger || exports._umi_irq_trigger,
        };
    }

    mapNotifyExports(exports) {
        // Task notification exports - check both minified and unminified names
        return {
            pending: exports.Na || exports.umi_notify_pending || exports._umi_notify_pending,
            send: exports.Oa || exports.umi_notify_send || exports._umi_notify_send,
            waitReason: exports.Pa || exports.umi_task_wait_reason || exports._umi_task_wait_reason,
        };
    }

    mapHwStateExports(exports) {
        // HW state (shared memory) exports - check both minified and unminified names
        return {
            sequence: exports.Ta || exports.umi_hw_state_sequence || exports._umi_hw_state_sequence,
            adcValue: exports.Ua || exports.umi_hw_adc_value || exports._umi_hw_adc_value,
            setAdc: exports.Va || exports.umi_hw_set_adc || exports._umi_hw_set_adc,
            encoderPos: exports.Wa || exports.umi_hw_encoder_pos || exports._umi_hw_encoder_pos,
            setEncoder: exports.Xa || exports.umi_hw_set_encoder || exports._umi_hw_set_encoder,
            gpioInput: exports.Ya || exports.umi_hw_gpio_input || exports._umi_hw_gpio_input,
            triggerGpio: exports.Za || exports.umi_hw_trigger_gpio || exports._umi_hw_trigger_gpio,
        };
    }

    mapShmExports(exports) {
        // Shared memory exports - check both minified and unminified names
        return {
            allocate: exports.Qa || exports.umi_shm_allocate || exports._umi_shm_allocate,
            size: exports.Ra || exports.umi_shm_size || exports._umi_shm_size,
            poolUsed: exports.Sa || exports.umi_shm_pool_used || exports._umi_shm_pool_used,
        };
    }

    sendIrqState() {
        const irq = this.irq;
        const notify = this.notify;
        const shm = this.shm;
        const hwState = this.hwState;

        if (!irq) return;

        // IRQ flag names matching the enum
        const irqFlagNames = [
            'DmaHalfTransfer', 'DmaComplete', 'SysTick', 'Timer2', 'Timer3',
            'AdcComplete', 'GpioExti', 'Usart1Rx', 'Usart1Tx', 'UsbSof'
        ];

        const irqFlags = irqFlagNames.map((name, i) => ({
            name,
            pending: irq.pending ? irq.pending(i) !== 0 : false,
            count: irq.count ? irq.count(i) : 0,
        }));

        // Task notification state
        const taskNotify = [];
        for (let i = 0; i < 4; i++) {
            taskNotify.push({
                pending: notify.pending ? notify.pending(i) : 0,
                waitReason: notify.waitReason ? notify.waitReason(i) : 0,
            });
        }

        // HW state from shared memory
        const adcValues = [];
        for (let i = 0; i < 8; i++) {
            adcValues.push(hwState && hwState.adcValue ? hwState.adcValue(i) : 0);
        }

        const encoderPos = [];
        for (let i = 0; i < 4; i++) {
            encoderPos.push(hwState && hwState.encoderPos ? hwState.encoderPos(i) : 0);
        }

        this.port.postMessage({
            type: 'irq-state',
            irqFlags,
            taskNotify,
            hwState: {
                sequence: hwState && hwState.sequence ? hwState.sequence() : 0,
                gpioInput: hwState && hwState.gpioInput ? hwState.gpioInput() : 0,
                adcValues,
                encoderPos,
            },
            shm: {
                poolUsed: shm && shm.poolUsed ? shm.poolUsed() : 0,
            },
        });
    }

    process(inputs, outputs, parameters) {
        const output = outputs[0];
        if (!output || output.length === 0) return true;

        const channel = output[0];
        const frames = channel.length;

        if (!this.wasmReady || !this.wasmInstance) {
            channel.fill(0);
            return true;
        }

        try {
            if (!this.fn.process) {
                console.error('[SynthWorklet] process function is undefined!');
                channel.fill(0);
                return true;
            }

            // Process audio
            this.fn.process(
                this.inputPtr,
                this.outputPtr,
                frames,
                Math.round(sampleRate)
            );

            // Copy from WASM heap to output
            const heap = new Float32Array(this.wasmMemory.buffer);
            const start = this.outputPtr / 4;
            channel.set(heap.subarray(start, start + frames));

            // Debug: check first sample every 100 process calls
            if (!this._debugCount) this._debugCount = 0;
            this._debugCount++;
            if (this._debugCount % 500 === 1) {
                const sample = heap[start];
                console.log('[SynthWorklet] process output sample[0]:', sample, 'frames:', frames);
            }

        } catch (err) {
            console.error('[SynthWorklet] process error:', err);
            channel.fill(0);
        }

        return true;
    }
}

registerProcessor('synth-worklet-processor', SynthWorkletProcessor);
