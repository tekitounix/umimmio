/**
 * UMI-OS Simulator - Kernel Status Module
 * @module simulator/kernel-status
 */

import { formatUptime } from './utils.js';

/**
 * Kernel status display component
 */
export class KernelStatus {
    /**
     * @param {object} elements - DOM element references
     * @param {object} options
     * @param {function} [options.onMidiActivity] - Callback for MIDI activity flash
     */
    constructor(elements, options = {}) {
        this.el = elements;
        this.options = options;

        // State for activity detection
        this.lastMidiRx = 0;
        this.midiFlashTimeout = null;
    }

    /**
     * Update kernel status display
     * @param {object} data - Kernel state data
     * @param {boolean} isPlaying - Whether audio is playing
     */
    update(data, isPlaying) {
        this._updateKernel(data, isPlaying);
        this._updateTasks(data);
        this._updateAudio(data);
        this._updateDspLoad(data);
        this._updateMidi(data);
        this._updatePower(data);
        this._updateWatchdog(data);
    }

    /**
     * Update audio context status
     * @param {AudioContext} audioContext
     */
    updateAudioContext(audioContext) {
        if (!audioContext) return;

        if (this.el.sampleRate) {
            this.el.sampleRate.textContent = audioContext.sampleRate + ' Hz';
        }
        if (this.el.baseLatency) {
            this.el.baseLatency.textContent =
                audioContext.baseLatency ? (audioContext.baseLatency * 1000).toFixed(2) + ' ms' : 'N/A';
        }
        if (this.el.outputLatency) {
            this.el.outputLatency.textContent =
                audioContext.outputLatency ? (audioContext.outputLatency * 1000).toFixed(2) + ' ms' : 'N/A';
        }
    }

    _updateKernel(data, isPlaying) {
        // Kernel indicator
        if (this.el.kernelIndicator) {
            this.el.kernelIndicator.className = 'indicator ' + (isPlaying ? 'active' : '');
        }

        // Uptime
        if (data.uptime !== undefined && this.el.uptime) {
            this.el.uptime.textContent = formatUptime(data.uptime);
        }

        // Scheduler state
        if (this.el.schedulerState) {
            this.el.schedulerState.textContent = isPlaying ? 'Running' : 'Stopped';
            this.el.schedulerState.className = 'status-value' + (isPlaying ? '' : ' muted');
        }

        // Idle percentage (100 - CPU utilization)
        if (data.cpuUtil !== undefined && this.el.idlePercent) {
            // cpuUtil is in x100 format (e.g., 1500 = 15.00%)
            const cpuPercent = data.cpuUtil / 100;
            const idlePercent = Math.max(0, 100 - cpuPercent);
            this.el.idlePercent.textContent = idlePercent.toFixed(0) + '%';
            // Color based on idle (low idle = warning)
            this.el.idlePercent.className = 'status-value' +
                (idlePercent < 10 ? ' error' : idlePercent < 25 ? ' warning' : '');
        }

        // RTC
        if (data.rtc !== undefined && this.el.rtcTime) {
            const date = new Date(data.rtc * 1000);
            this.el.rtcTime.textContent = date.toLocaleTimeString();
        }

        // Kernel version (static, set once)
        if (this.el.kernelVersion && !this._versionSet) {
            this.el.kernelVersion.textContent = '0.3.0';  // TODO: get from WASM
            this._versionSet = true;
        }
    }

    _updateTasks(data) {
        if (data.taskCount !== undefined && this.el.taskInfo) {
            // Compact format: "4 (3R/1B)" instead of "4 (3 ready, 1 blocked)"
            this.el.taskInfo.textContent =
                `${data.taskCount} (${data.taskReady}R/${data.taskBlocked}B)`;
        }

        if (data.ctxSwitches !== undefined && this.el.ctxSwitches) {
            this.el.ctxSwitches.textContent = data.ctxSwitches.toLocaleString();
        }
    }

    _updateAudio(data) {
        // Audio indicator
        if (this.el.audioIndicator) {
            this.el.audioIndicator.className = 'indicator ' + (data.audioRunning ? 'active' : '');
        }
        if (this.el.engineState) {
            this.el.engineState.textContent = data.audioRunning ? 'Running' : 'Stopped';
        }

        // Buffer counts
        if (data.bufferCount !== undefined && this.el.bufferCount) {
            this.el.bufferCount.textContent = data.bufferCount.toLocaleString();
        }
        if (data.dropCount !== undefined && this.el.dropCount) {
            this.el.dropCount.textContent = data.dropCount;
            this.el.dropCount.className = 'status-value' + (data.dropCount > 0 ? ' warning' : '');
        }
    }

    _updateDspLoad(data) {
        if (data.dspLoad !== undefined) {
            const loadPercent = (data.dspLoad / 100).toFixed(1);
            if (this.el.dspLoad) {
                this.el.dspLoad.textContent = loadPercent + '%';
            }
            if (this.el.dspLoadBar) {
                this.el.dspLoadBar.style.width = Math.min(100, data.dspLoad / 100) + '%';
                this.el.dspLoadBar.className = 'load-bar-fill' +
                    (data.dspLoad > 8000 ? ' critical' : data.dspLoad > 5000 ? ' warning' : '');
            }
        }
        if (data.dspPeak !== undefined && this.el.dspPeak) {
            this.el.dspPeak.textContent = (data.dspPeak / 100).toFixed(1) + '%';
        }
    }

    _updateMidi(data) {
        if (data.midiRx !== undefined && this.el.midiRx) {
            this.el.midiRx.textContent = data.midiRx;

            // Flash MIDI indicator on activity
            if (this.el.midiIndicator && data.midiRx !== this.lastMidiRx) {
                this.el.midiIndicator.className = 'indicator active';
                clearTimeout(this.midiFlashTimeout);
                this.midiFlashTimeout = setTimeout(() => {
                    this.el.midiIndicator.className = 'indicator';
                }, 100);
                this.lastMidiRx = data.midiRx;
            }
        }
        if (data.midiTx !== undefined && this.el.midiTx) {
            this.el.midiTx.textContent = data.midiTx;
        }
    }

    _updatePower(data) {
        if (data.batteryPercent !== undefined) {
            // Power indicator
            if (this.el.powerIndicator) {
                this.el.powerIndicator.className = 'indicator ' +
                    (data.batteryCharging ? 'active' : data.batteryPercent < 20 ? 'warning' : '');
            }

            // Battery level
            if (this.el.batteryLevel) {
                this.el.batteryLevel.textContent =
                    data.batteryPercent + '%' + (data.batteryCharging ? ' (Charging)' : '');
            }

            // Battery bar with color based on level
            if (this.el.batteryBar) {
                this.el.batteryBar.style.width = data.batteryPercent + '%';
                const styles = getComputedStyle(document.documentElement);
                this.el.batteryBar.style.background = data.batteryPercent < 20 ?
                    (styles.getPropertyValue('--umi-error').trim() || '#e74c3c') :
                    data.batteryPercent < 50 ?
                    (styles.getPropertyValue('--umi-warning').trim() || '#f39c12') :
                    (styles.getPropertyValue('--umi-success').trim() || '#27ae60');
            }
        }

        if (data.batteryVoltage !== undefined && this.el.batteryVoltage) {
            this.el.batteryVoltage.textContent = (data.batteryVoltage / 1000).toFixed(2) + ' V';
        }

        if (data.usbConnected !== undefined && this.el.usbStatus) {
            this.el.usbStatus.textContent = data.usbConnected ? 'Connected' : 'Disconnected';
        }
    }

    _updateWatchdog(data) {
        if (data.wdtEnabled === undefined) return;

        if (this.el.wdtIndicator) {
            this.el.wdtIndicator.className = 'indicator ' +
                (data.wdtExpired ? 'error' : data.wdtEnabled ? 'active' : '');
        }
        if (this.el.wdtStatus) {
            this.el.wdtStatus.textContent =
                data.wdtExpired ? 'EXPIRED!' : data.wdtEnabled ? 'Active' : 'Disabled';
        }
        if (this.el.wdtTimeout) {
            this.el.wdtTimeout.textContent =
                data.wdtEnabled ? data.wdtTimeout + ' ms' : '--';
        }
    }

    /**
     * Cleanup
     */
    destroy() {
        if (this.midiFlashTimeout) {
            clearTimeout(this.midiFlashTimeout);
        }
    }
}
