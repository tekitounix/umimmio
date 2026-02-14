/**
 * UMI-OS Simulator - Hardware Settings Module
 * @module simulator/hw-settings
 */

/**
 * Hardware presets for different MCU targets
 */
export const HW_PRESETS = {
    'STM32F4': {
        cpuFreq: 168, isrOverhead: 100, baseCycles: 20, voiceCycles: 200,
        sramKb: 128, heapKb: 48, dataBssKb: 8, mainStackKb: 2,
        taskStackBytes: 1024, taskCount: 4, dmaKb: 2, sharedKb: 8,
        flashKb: 512, flashUsedKb: 64
    },
    'RP2040': {
        cpuFreq: 133, isrOverhead: 80, baseCycles: 15, voiceCycles: 180,
        sramKb: 264, heapKb: 64, dataBssKb: 16, mainStackKb: 4,
        taskStackBytes: 2048, taskCount: 2, dmaKb: 4, sharedKb: 16,
        flashKb: 2048, flashUsedKb: 128
    },
    'ESP32': {
        cpuFreq: 240, isrOverhead: 150, baseCycles: 25, voiceCycles: 250,
        sramKb: 520, heapKb: 128, dataBssKb: 32, mainStackKb: 8,
        taskStackBytes: 4096, taskCount: 8, dmaKb: 8, sharedKb: 32,
        flashKb: 4096, flashUsedKb: 256
    }
};

/**
 * Hardware settings manager for UMI-OS simulator
 */
export class HwSettings {
    /**
     * @param {object} elements - DOM element references for settings inputs
     * @param {object} options
     * @param {function} [options.onApply] - Callback when settings are applied
     * @param {function} [options.log] - Logger function
     */
    constructor(elements, options = {}) {
        this.el = elements;
        this.onApply = options.onApply || null;
        this.log = options.log || console.log;
    }

    /**
     * Apply a hardware preset
     * @param {string} presetName - Preset name (e.g., 'STM32F4')
     */
    applyPreset(presetName) {
        const p = HW_PRESETS[presetName];
        if (!p) {
            this.log(`Unknown preset: ${presetName}`, 'error');
            return;
        }

        // CPU
        this._setInput('hwCpuFreq', p.cpuFreq);
        this._setInput('hwIsrOverhead', p.isrOverhead);
        this._setInput('hwBaseCycles', p.baseCycles);
        this._setInput('hwVoiceCycles', p.voiceCycles);

        // Memory
        this._setInput('hwSramKb', p.sramKb);
        this._setInput('hwHeapKb', p.heapKb);
        this._setInput('hwDataBssKb', p.dataBssKb);
        this._setInput('hwMainStackKb', p.mainStackKb);
        this._setInput('hwTaskStackBytes', p.taskStackBytes);
        this._setInput('hwTaskCount', p.taskCount);
        this._setInput('hwDmaKb', p.dmaKb);
        this._setInput('hwSharedKb', p.sharedKb);

        // Flash
        this._setInput('hwFlashKb', p.flashKb);
        this._setInput('hwFlashUsedKb', p.flashUsedKb);

        this.apply();
        this.log(`Applied HW preset: ${presetName} (${p.cpuFreq}MHz, ${p.sramKb}KB RAM)`);
    }

    /**
     * Apply current settings
     * @returns {object} Settings object
     */
    apply() {
        const sramKb = this._getInput('hwSramKb', 128);

        const settings = {
            // CPU
            cpuFreq: this._getInput('hwCpuFreq', 168),
            isrOverhead: this._getInput('hwIsrOverhead', 100),
            baseCycles: this._getInput('hwBaseCycles', 20),
            voiceCycles: this._getInput('hwVoiceCycles', 200),

            // Memory
            sramTotalKb: sramKb,
            heapSizeKb: this._getInput('hwHeapKb', 48),
            dataBssKb: this._getInput('hwDataBssKb', 8),
            mainStackKb: this._getInput('hwMainStackKb', 2),
            taskStackBytes: this._getInput('hwTaskStackBytes', 1024),
            taskCount: this._getInput('hwTaskCount', 4),
            dmaBufferKb: this._getInput('hwDmaKb', 2),
            sharedMemKb: this._getInput('hwSharedKb', 8),

            // Flash
            flashTotalKb: this._getInput('hwFlashKb', 512),
            flashUsedKb: this._getInput('hwFlashUsedKb', 64),

            // Peripherals
            batteryPercent: this._getInput('hwBatteryPercent', 100),
            batteryVoltage: this._getInput('hwBatteryVoltage', 4200),
            batteryCharging: this._getCheckbox('hwBatteryCharging'),
            usbConnected: this._getCheckbox('hwUsbConnected'),
            wdtEnabled: this._getCheckbox('hwWdtEnabled'),
            wdtTimeout: this._getInput('hwWdtTimeout', 0),
        };

        if (this.onApply) {
            this.onApply(settings);
        }

        this.log(`HW settings applied: CPU=${settings.cpuFreq}MHz, RAM=${sramKb}KB`);
        return settings;
    }

    /**
     * Get current settings object
     * @returns {object}
     */
    getSettings() {
        return {
            cpuFreq: this._getInput('hwCpuFreq', 168),
            isrOverhead: this._getInput('hwIsrOverhead', 100),
            baseCycles: this._getInput('hwBaseCycles', 20),
            voiceCycles: this._getInput('hwVoiceCycles', 200),
            sramTotalKb: this._getInput('hwSramKb', 128),
            heapSizeKb: this._getInput('hwHeapKb', 48),
            dataBssKb: this._getInput('hwDataBssKb', 8),
            mainStackKb: this._getInput('hwMainStackKb', 2),
            taskStackBytes: this._getInput('hwTaskStackBytes', 1024),
            taskCount: this._getInput('hwTaskCount', 4),
            dmaBufferKb: this._getInput('hwDmaKb', 2),
            sharedMemKb: this._getInput('hwSharedKb', 8),
            flashTotalKb: this._getInput('hwFlashKb', 512),
            flashUsedKb: this._getInput('hwFlashUsedKb', 64),
            batteryPercent: this._getInput('hwBatteryPercent', 100),
            batteryVoltage: this._getInput('hwBatteryVoltage', 4200),
            batteryCharging: this._getCheckbox('hwBatteryCharging'),
            usbConnected: this._getCheckbox('hwUsbConnected'),
            wdtEnabled: this._getCheckbox('hwWdtEnabled'),
            wdtTimeout: this._getInput('hwWdtTimeout', 0),
        };
    }

    /**
     * Update battery voltage based on percent (for linked slider)
     * @param {number} percent
     */
    updateBatteryVoltage(percent) {
        // Map 0-100% to 3.0V-4.2V
        const voltage = Math.round(3000 + (percent / 100) * 1200);
        this._setInput('hwBatteryVoltage', voltage);
    }

    _getInput(id, defaultValue) {
        const el = this.el[id] || document.getElementById(id);
        return parseInt(el?.value) || defaultValue;
    }

    _getCheckbox(id) {
        const el = this.el[id] || document.getElementById(id);
        return el?.checked || false;
    }

    _setInput(id, value) {
        const el = this.el[id] || document.getElementById(id);
        if (el) el.value = value;
    }
}
