/**
 * UMI Web - TargetSelector Component
 *
 * Unified target selection combining backend type and application selection.
 * Supports simulators (WASM), emulators (Renode), and hardware (USB MIDI).
 *
 * @module umi_web/components/target-selector
 */

/**
 * Target types
 */
export const TargetType = {
  SIMULATOR: 'simulator',
  EMULATOR: 'emulator',
  HARDWARE: 'hardware',
  HARDWARE_SCAN: 'hardware-scan',
};

/**
 * Target configuration
 * @typedef {Object} Target
 * @property {string} id - Unique identifier
 * @property {string} name - Display name
 * @property {string} icon - Emoji icon
 * @property {TargetType} type - Target type
 * @property {string} backend - Backend type (umim, umios, renode, hardware)
 * @property {object} [app] - Application config (for simulators)
 * @property {string} [deviceName] - MIDI device name (for hardware)
 * @property {boolean} [available] - Whether target is available
 */

/**
 * TargetSelector - Unified target selection component
 */
export class TargetSelector {
  /**
   * @param {HTMLElement} container - Container element (typically a select or custom dropdown)
   * @param {object} options
   * @param {BackendManager} options.backendManager - Backend manager instance
   * @param {Function} [options.onChange] - Callback when target changes
   */
  constructor(container, options = {}) {
    this.container = container;
    this.backendManager = options.backendManager;
    this.onChange = options.onChange;

    /** @type {Target[]} */
    this.targets = [];

    /** @type {Target|null} */
    this.selectedTarget = null;

    this._init();
  }

  async _init() {
    await this.loadTargets();
    this._render();
    this._setupEvents();
  }

  /**
   * Load all available targets
   */
  async loadTargets() {
    this.targets = [];

    try {
      // Load apps from manifest
      const apps = await this.backendManager.loadApplications();

      // Add simulator targets
      for (const app of apps) {
        this.targets.push({
          id: app.id,
          name: app.name,
          icon: this._getIcon(app.backend),
          type: TargetType.SIMULATOR,
          backend: app.backend,
          app: app,
          available: true,
        });
      }

      // Check special backends
      const backends = await this.backendManager.getAvailableBackends();

      // Renode emulator
      const renode = backends.find(b => b.type === 'renode');
      if (renode) {
        this.targets.push({
          id: 'renode',
          name: 'Renode Emulator',
          icon: '🔧',
          type: TargetType.EMULATOR,
          backend: 'renode',
          available: renode.available,
        });
      }

      // Hardware (scan option)
      const hw = backends.find(b => b.type === 'hardware');
      if (hw?.available) {
        this.targets.push({
          id: 'hardware-scan',
          name: 'Scan USB Devices...',
          icon: '🔌',
          type: TargetType.HARDWARE_SCAN,
          backend: 'hardware',
          available: true,
        });
      }

      // Set default selection
      const defaultApp = apps.find(a => a.default) || apps[0];
      if (defaultApp) {
        this.selectedTarget = this.targets.find(t => t.id === defaultApp.id) || null;
      }
    } catch (err) {
      console.error('[TargetSelector] Failed to load targets:', err);
    }
  }

  /**
   * Scan for hardware devices
   */
  async scanHardware() {
    try {
      const devices = await this.backendManager.getHardwareDevices();

      // Remove old hardware targets (keep scan option)
      this.targets = this.targets.filter(t =>
        t.type !== TargetType.HARDWARE
      );

      // Add detected devices
      for (const device of devices) {
        const id = `hw-${device.name.replace(/\s+/g, '-').toLowerCase()}`;
        this.targets.push({
          id,
          name: `USB: ${device.name}`,
          icon: '🔌',
          type: TargetType.HARDWARE,
          backend: 'hardware',
          deviceName: device.name,
          available: true,
        });
      }

      this._render();

      // Select first device if any
      if (devices.length > 0) {
        const firstHw = this.targets.find(t => t.type === TargetType.HARDWARE);
        if (firstHw) {
          this.select(firstHw.id);
        }
      }

      return devices;
    } catch (err) {
      console.error('[TargetSelector] Hardware scan failed:', err);
      throw err;
    }
  }

  /**
   * Select target by ID
   * @param {string} targetId
   */
  select(targetId) {
    const target = this.targets.find(t => t.id === targetId);
    if (!target) return;

    // Handle scan action
    if (target.type === TargetType.HARDWARE_SCAN) {
      this.scanHardware();
      return;
    }

    this.selectedTarget = target;

    // Update UI
    if (this.container.tagName === 'SELECT') {
      this.container.value = targetId;
    }

    // Notify
    this.onChange?.(target);
  }

  /**
   * Get currently selected target
   * @returns {Target|null}
   */
  getSelected() {
    return this.selectedTarget;
  }

  /**
   * Get all targets
   * @returns {Target[]}
   */
  getTargets() {
    return this.targets;
  }

  /**
   * Get targets by type
   * @param {TargetType} type
   * @returns {Target[]}
   */
  getTargetsByType(type) {
    return this.targets.filter(t => t.type === type);
  }

  _getIcon(backend) {
    switch (backend) {
      case 'umim':
      case 'umios':
        return '🖥️';
      case 'renode':
        return '🔧';
      case 'hardware':
        return '🔌';
      default:
        return '📦';
    }
  }

  _render() {
    if (this.container.tagName === 'SELECT') {
      this._renderSelect();
    } else {
      this._renderCustom();
    }
  }

  _renderSelect() {
    const select = this.container;
    select.innerHTML = '';

    const groups = {
      [TargetType.SIMULATOR]: { label: 'Simulators', targets: [] },
      [TargetType.EMULATOR]: { label: 'Emulators', targets: [] },
      [TargetType.HARDWARE]: { label: 'Hardware', targets: [] },
      [TargetType.HARDWARE_SCAN]: { label: 'Hardware', targets: [] },
    };

    // Group targets
    for (const target of this.targets) {
      const groupKey = target.type === TargetType.HARDWARE_SCAN
        ? TargetType.HARDWARE
        : target.type;
      groups[groupKey].targets.push(target);
    }

    // Render groups
    for (const [type, group] of Object.entries(groups)) {
      if (group.targets.length === 0) continue;

      const optgroup = document.createElement('optgroup');
      optgroup.label = group.label;

      for (const target of group.targets) {
        const option = document.createElement('option');
        option.value = target.id;
        option.textContent = `${target.icon} ${target.name}`;
        option.disabled = !target.available;

        if (target.id === this.selectedTarget?.id) {
          option.selected = true;
        }

        optgroup.appendChild(option);
      }

      select.appendChild(optgroup);
    }
  }

  _renderCustom() {
    // Custom dropdown implementation (for future use)
    this.container.innerHTML = `
      <div class="target-selector">
        <button class="target-selector-toggle">
          ${this.selectedTarget ? `${this.selectedTarget.icon} ${this.selectedTarget.name}` : 'Select Target'}
        </button>
        <div class="target-selector-menu" hidden>
          ${this._renderMenuItems()}
        </div>
      </div>
    `;
  }

  _renderMenuItems() {
    return this.targets.map(t => `
      <div class="target-selector-item ${!t.available ? 'disabled' : ''}" data-id="${t.id}">
        <span class="target-icon">${t.icon}</span>
        <span class="target-name">${t.name}</span>
        ${!t.available ? '<span class="target-status">unavailable</span>' : ''}
      </div>
    `).join('');
  }

  _setupEvents() {
    if (this.container.tagName === 'SELECT') {
      this.container.addEventListener('change', (e) => {
        this.select(e.target.value);
      });
    } else {
      // Custom dropdown events
      const toggle = this.container.querySelector('.target-selector-toggle');
      const menu = this.container.querySelector('.target-selector-menu');

      toggle?.addEventListener('click', () => {
        menu.hidden = !menu.hidden;
      });

      this.container.querySelectorAll('.target-selector-item').forEach(item => {
        item.addEventListener('click', () => {
          if (!item.classList.contains('disabled')) {
            this.select(item.dataset.id);
            menu.hidden = true;
          }
        });
      });

      // Close on outside click
      document.addEventListener('click', (e) => {
        if (!this.container.contains(e.target)) {
          menu.hidden = true;
        }
      });
    }
  }

  /**
   * Refresh targets list
   */
  async refresh() {
    await this.loadTargets();
    this._render();
  }
}

export default TargetSelector;
