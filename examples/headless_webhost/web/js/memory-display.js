/**
 * UMI-OS Simulator - Memory Display Module
 * @module simulator/memory-display
 */

import { formatBytes } from './utils.js';

/**
 * Memory warning messages
 */
export const MEM_WARNING_MESSAGES = {
    0: '',
    1: 'Low memory',
    2: 'Memory fragmented',
    3: 'Heap near limit',
    4: 'Stack near limit',
    5: 'Critical: Out of memory',
    6: 'Critical: Stack overflow'
};

/**
 * Memory display component for UMI-OS simulator
 */
export class MemoryDisplay {
    /**
     * @param {object} elements - DOM element references
     * @param {HTMLElement} elements.heapUsage
     * @param {HTMLElement} elements.stackUsage
     * @param {HTMLElement} elements.memUsageHeap - Heap section of combined bar
     * @param {HTMLElement} elements.memUsageFree - Free section of combined bar
     * @param {HTMLElement} elements.memUsageStack - Stack section of combined bar
     * @param {HTMLElement} elements.memStatusBadge - Safe/Warning/Critical badge
     * @param {HTMLElement} elements.sramTotal
     * @param {HTMLElement} elements.memTotalLabel
     * @param {HTMLElement} elements.memFreeGap
     * @param {HTMLElement} elements.memWarning
     * @param {HTMLElement} elements.memWarningText
     * @param {HTMLElement} elements.memIndicator
     * @param {HTMLElement} [elements.memDataBssSize]
     * @param {HTMLElement} [elements.memTaskStacksSize]
     * @param {HTMLElement} [elements.memMainStackSize]
     * @param {HTMLElement} [elements.memDmaSize]
     * @param {HTMLElement} [elements.memSharedSize]
     * @param {HTMLElement} [elements.ccmRow]
     * @param {HTMLElement} [elements.memCcm]
     */
    constructor(elements) {
        this.el = elements;
    }

    /**
     * Update memory display with new data
     * @param {object} data - Memory data from kernel
     */
    update(data) {
        this._updateHeapStack(data);
        this._updateSramTotal(data);
        this._updateFreeGap(data);
        this._updateMemoryMap(data);
        this._updateWarning(data);
        this._updateDetails(data);
    }

    _updateHeapStack(data) {
        // Calculate dynamic memory pool total (heap + free + stack area)
        const heapUsed = data.heapUsed || 0;
        const stackUsed = data.stackUsed || 0;
        const freeGap = data.freeGap || 0;
        const dynamicTotal = heapUsed + freeGap + stackUsed;

        if (dynamicTotal > 0) {
            // Update combined usage bar (Heap | Free | Stack)
            const heapFlex = (heapUsed / dynamicTotal * 100);
            const freeFlex = (freeGap / dynamicTotal * 100);
            const stackFlex = (stackUsed / dynamicTotal * 100);

            if (this.el.memUsageHeap) {
                this.el.memUsageHeap.style.flex = heapFlex;
                this.el.memUsageHeap.title = `Heap: ${formatBytes(heapUsed)}`;
            }
            if (this.el.memUsageFree) {
                this.el.memUsageFree.style.flex = freeFlex;
                this.el.memUsageFree.title = `Free: ${formatBytes(freeGap)}`;
            }
            if (this.el.memUsageStack) {
                this.el.memUsageStack.style.flex = stackFlex;
                this.el.memUsageStack.title = `Stack: ${formatBytes(stackUsed)}`;
            }

            // Update text labels
            this.el.heapUsage.textContent = formatBytes(heapUsed);
            this.el.stackUsage.textContent = formatBytes(stackUsed);

            // Update status badge based on free gap percentage
            if (this.el.memStatusBadge) {
                const freePercent = (freeGap / dynamicTotal * 100);
                if (freePercent < 10) {
                    this.el.memStatusBadge.textContent = 'Critical';
                    this.el.memStatusBadge.className = 'mem-status-badge critical';
                } else if (freePercent < 25) {
                    this.el.memStatusBadge.textContent = 'Warning';
                    this.el.memStatusBadge.className = 'mem-status-badge warning';
                } else {
                    this.el.memStatusBadge.textContent = 'Safe';
                    this.el.memStatusBadge.className = 'mem-status-badge';
                }
            }
        }
    }

    _updateSramTotal(data) {
        if (data.sramTotal !== undefined) {
            this.el.sramTotal.textContent = formatBytes(data.sramTotal);
            this.el.memTotalLabel.textContent = formatBytes(data.sramTotal);
        }
    }

    _updateFreeGap(data) {
        if (data.freeGap !== undefined) {
            // Show free gap with "Free:" prefix for clarity in the center
            this.el.memFreeGap.textContent = `Free: ${formatBytes(data.freeGap)}`;
        }
    }

    _updateMemoryMap(data) {
        if (!data.memLayout || !data.sramTotal) return;

        const layout = data.memLayout;
        const total = data.sramTotal;

        const regions = [
            { id: 'memDataBss', size: layout.dataBss || 0 },
            { id: 'memHeap', size: layout.heap || 0 },
            { id: 'memFree', size: layout.free || 0 },
            { id: 'memTaskStacks', size: layout.taskStacks || 0 },
            { id: 'memMainStack', size: layout.mainStack || 0 },
            { id: 'memDma', size: layout.dma || 0 },
            { id: 'memShared', size: layout.shared || 0 }
        ];

        for (const region of regions) {
            const el = document.getElementById(region.id);
            if (el) {
                const percent = (region.size / total * 100);
                el.style.flex = percent > 0 ? percent : '0';
                el.title = `${el.title.split(':')[0]}: ${formatBytes(region.size)}`;
                const label = el.querySelector('.mem-region-label');
                if (label) {
                    label.style.display = percent < 5 ? 'none' : '';
                }
            }
        }
    }

    _updateWarning(data) {
        if (data.memWarning === undefined) return;

        if (data.memWarning > 0) {
            this.el.memWarning.style.display = 'flex';
            this.el.memWarningText.textContent =
                MEM_WARNING_MESSAGES[data.memWarning] || 'Unknown warning';
            this.el.memWarning.className = 'mem-warning' + (data.memWarning >= 5 ? ' critical' : '');
            this.el.memIndicator.className = 'indicator ' + (data.memWarning >= 5 ? 'error' : 'warning');
        } else {
            this.el.memWarning.style.display = 'none';
            this.el.memIndicator.className = 'indicator active';
        }
    }

    _updateDetails(data) {
        if (!data.memDetails) return;

        const d = data.memDetails;

        if (this.el.memDataBssSize) {
            this.el.memDataBssSize.textContent = formatBytes(d.dataBss || 0);
        }
        if (this.el.memTaskStacksSize) {
            this.el.memTaskStacksSize.textContent = formatBytes(d.taskStacks || 0);
        }
        if (this.el.memMainStackSize) {
            this.el.memMainStackSize.textContent = formatBytes(d.mainStack || 0);
        }
        if (this.el.memDmaSize) {
            this.el.memDmaSize.textContent = formatBytes(d.dma || 0);
        }
        if (this.el.memSharedSize) {
            this.el.memSharedSize.textContent = formatBytes(d.shared || 0);
        }

        // CCM RAM (only show if present)
        if (this.el.ccmRow && this.el.memCcm) {
            if (d.ccmTotal !== undefined && d.ccmTotal > 0) {
                this.el.ccmRow.style.display = 'flex';
                this.el.memCcm.textContent =
                    `${formatBytes(d.ccmUsed || 0)} / ${formatBytes(d.ccmTotal)}`;
            } else {
                this.el.ccmRow.style.display = 'none';
            }
        }
    }
}
