/**
 * UMI Web - Waveform Component
 *
 * Real-time waveform visualization using AnalyserNode.
 *
 * @module umi_web/components/waveform
 */

/**
 * Get CSS variable value from document root
 * @param {string} name - CSS variable name (e.g., '--umi-bg')
 * @param {string} fallback - Fallback value
 * @returns {string}
 */
function getCSSVar(name, fallback) {
    const value = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
    return value || fallback;
}

/**
 * Waveform display component
 */
export class Waveform {
    /**
     * @param {HTMLCanvasElement} canvas - Canvas element for drawing
     * @param {object} options
     * @param {string} [options.backgroundColor] - Background color (default: CSS var --umi-bg or '#0a0a15')
     * @param {string} [options.strokeColor] - Stroke color (default: CSS var --umi-accent or '#4ecca3')
     * @param {number} [options.lineWidth=2]
     * @param {boolean} [options.useCSSVariables=true] - Read colors from CSS variables dynamically
     */
    constructor(canvas, options = {}) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');

        this._explicitBackgroundColor = options.backgroundColor || null;
        this._explicitStrokeColor = options.strokeColor || null;
        this.lineWidth = options.lineWidth || 2;
        this.useCSSVariables = options.useCSSVariables !== false;

        /** @type {AnalyserNode|null} */
        this.analyser = null;
        /** @type {Uint8Array|null} */
        this.dataArray = null;

        this._animationId = null;
        this._isRunning = false;
    }

    /**
     * Get current background color (from CSS variable or explicit)
     * @returns {string}
     */
    get backgroundColor() {
        if (this._explicitBackgroundColor) {
            return this._explicitBackgroundColor;
        }
        if (this.useCSSVariables) {
            return getCSSVar('--umi-bg', '#0a0a15');
        }
        return '#0a0a15';
    }

    /**
     * Get current stroke color (from CSS variable or explicit)
     * @returns {string}
     */
    get strokeColor() {
        if (this._explicitStrokeColor) {
            return this._explicitStrokeColor;
        }
        if (this.useCSSVariables) {
            return getCSSVar('--umi-accent', '#4ecca3');
        }
        return '#4ecca3';
    }

    /**
     * Set background color explicitly
     * @param {string} color
     */
    set backgroundColor(color) {
        this._explicitBackgroundColor = color;
    }

    /**
     * Set stroke color explicitly
     * @param {string} color
     */
    set strokeColor(color) {
        this._explicitStrokeColor = color;
    }

    /**
     * Connect to an AnalyserNode
     * @param {AnalyserNode} analyser
     */
    connect(analyser) {
        this.analyser = analyser;
        this.dataArray = new Uint8Array(analyser.frequencyBinCount);
    }

    /**
     * Disconnect from analyser
     */
    disconnect() {
        this.analyser = null;
        this.dataArray = null;
    }

    /**
     * Start rendering
     */
    start() {
        if (!this.analyser || this._isRunning) return;

        this._isRunning = true;
        this._draw();
    }

    /**
     * Stop rendering
     */
    stop() {
        this._isRunning = false;
        if (this._animationId) {
            cancelAnimationFrame(this._animationId);
            this._animationId = null;
        }
        this._clear();
    }

    /**
     * Check if running
     * @returns {boolean}
     */
    isRunning() {
        return this._isRunning;
    }

    _draw() {
        if (!this._isRunning || !this.analyser) return;

        this._animationId = requestAnimationFrame(() => this._draw());

        this.analyser.getByteTimeDomainData(this.dataArray);

        // Handle canvas resize
        const rect = this.canvas.getBoundingClientRect();
        if (this.canvas.width !== rect.width || this.canvas.height !== rect.height) {
            this.canvas.width = rect.width;
            this.canvas.height = rect.height;
        }

        const width = this.canvas.width;
        const height = this.canvas.height;
        const bufferLength = this.dataArray.length;

        // Clear
        this.ctx.fillStyle = this.backgroundColor;
        this.ctx.fillRect(0, 0, width, height);

        // Draw waveform
        this.ctx.lineWidth = this.lineWidth;
        this.ctx.strokeStyle = this.strokeColor;
        this.ctx.beginPath();

        const sliceWidth = width / bufferLength;
        let x = 0;

        for (let i = 0; i < bufferLength; i++) {
            const v = this.dataArray[i] / 128.0;
            const y = (v * height) / 2;

            if (i === 0) {
                this.ctx.moveTo(x, y);
            } else {
                this.ctx.lineTo(x, y);
            }
            x += sliceWidth;
        }

        this.ctx.lineTo(width, height / 2);
        this.ctx.stroke();
    }

    _clear() {
        const width = this.canvas.width;
        const height = this.canvas.height;
        this.ctx.fillStyle = this.backgroundColor;
        this.ctx.fillRect(0, 0, width, height);

        // Draw center line
        this.ctx.strokeStyle = this.strokeColor;
        this.ctx.globalAlpha = 0.3;
        this.ctx.beginPath();
        this.ctx.moveTo(0, height / 2);
        this.ctx.lineTo(width, height / 2);
        this.ctx.stroke();
        this.ctx.globalAlpha = 1.0;
    }
}
