/**
 * UMI-OS Simulator - Utility Functions
 * @module simulator/utils
 */

/**
 * Format bytes to human-readable string
 * @param {number} bytes
 * @returns {string}
 */
export function formatBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(2) + ' MB';
}

/**
 * Format uptime (microseconds) to human-readable string
 * @param {number} us - Microseconds
 * @returns {string}
 */
export function formatUptime(us) {
    const sec = Math.floor(us / 1_000_000);
    const min = Math.floor(sec / 60);
    const hr = Math.floor(min / 60);
    if (hr > 0) {
        return `${hr}h ${min % 60}m ${sec % 60}s`;
    } else if (min > 0) {
        return `${min}m ${sec % 60}s`;
    }
    return `${sec}s`;
}

/**
 * Escape HTML special characters
 * @param {string} text
 * @returns {string}
 */
export function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

/**
 * Simple logger with level support
 */
export class Logger {
    /**
     * @param {HTMLElement} container - Log viewer container
     * @param {object} options
     * @param {boolean} [options.console=true] - Also log to console
     * @param {number} [options.maxEntries=1000] - Maximum log entries
     */
    constructor(container, options = {}) {
        this.container = container;
        this.console = options.console !== false;
        this.maxEntries = options.maxEntries || 1000;
    }

    /**
     * Log a message
     * @param {string} message
     * @param {'info'|'warn'|'error'|'debug'} [level='info']
     */
    log(message, level = 'info') {
        const time = new Date().toLocaleTimeString();
        const entry = document.createElement('div');
        entry.className = `log-entry ${level}`;
        entry.textContent = `[${time}] ${message}`;
        this.container.appendChild(entry);

        // Trim old entries
        while (this.container.children.length > this.maxEntries) {
            this.container.removeChild(this.container.firstChild);
        }

        // Auto-scroll
        this.container.scrollTop = this.container.scrollHeight;

        // Console output
        if (this.console) {
            const fn = level === 'error' ? console.error :
                       level === 'warn' ? console.warn : console.log;
            fn(`[UMI] ${message}`);
        }
    }

    /**
     * Clear all log entries
     * @param {string} [message] - Optional message to show after clear
     */
    clear(message) {
        this.container.innerHTML = '';
        if (message) {
            this.log(message, 'info');
        }
    }
}

/**
 * Status indicator manager
 */
export class StatusManager {
    /**
     * @param {HTMLElement} statusElement - Status text element
     */
    constructor(statusElement) {
        this.element = statusElement;
    }

    /**
     * Set status text and class
     * @param {string} text
     * @param {'connected'|'error'|'warning'|''} [className='']
     */
    set(text, className = '') {
        this.element.textContent = text;
        this.element.className = 'status ' + className;
    }
}
