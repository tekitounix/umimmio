/**
 * UMI-OS Simulator - Auto Play Module
 * @module simulator/auto-play
 */

/**
 * Auto-play pattern definitions
 */
export const AUTO_PATTERNS = {
    arpeggio: {
        name: 'Arpeggio',
        notes: [0, 4, 7, 12, 7, 4],  // Major arpeggio
        duration: 0.25  // beats
    },
    chord: {
        name: 'Chords',
        chords: [[0, 4, 7], [5, 9, 12], [7, 11, 14], [0, 4, 7]],  // I-IV-V-I
        duration: 1
    },
    random: {
        name: 'Random',
        range: 24,
        duration: 0.125
    },
    scale: {
        name: 'Scale',
        notes: [0, 2, 4, 5, 7, 9, 11, 12, 11, 9, 7, 5, 4, 2],
        duration: 0.125
    }
};

/**
 * Auto-play controller for testing synth
 */
export class AutoPlayer {
    /**
     * @param {object} options
     * @param {function} options.noteOn - Note on callback (note, velocity)
     * @param {function} options.noteOff - Note off callback (note)
     * @param {function} [options.log] - Logger function
     */
    constructor(options) {
        this.noteOn = options.noteOn;
        this.noteOff = options.noteOff;
        this.log = options.log || console.log;

        this.interval = null;
        this.step = 0;
        this.isPlaying = false;

        // Current settings
        this.pattern = 'arpeggio';
        this.tempo = 120;
        this.baseNote = 48;
    }

    /**
     * Start auto-play
     * @param {object} [options]
     * @param {string} [options.pattern='arpeggio']
     * @param {number} [options.tempo=120]
     * @param {number} [options.baseNote=48]
     */
    start(options = {}) {
        if (this.isPlaying) return;

        this.pattern = options.pattern || this.pattern;
        this.tempo = options.tempo || this.tempo;
        this.baseNote = options.baseNote || this.baseNote;

        const patternData = AUTO_PATTERNS[this.pattern];
        if (!patternData) {
            this.log(`Unknown pattern: ${this.pattern}`, 'error');
            return;
        }

        const beatMs = 60000 / this.tempo;
        const intervalMs = beatMs * patternData.duration;

        this.step = 0;
        this.isPlaying = true;

        this.interval = setInterval(() => {
            this._playStep(patternData, intervalMs);
        }, intervalMs);

        this.log(`Auto play started: ${this.pattern} at ${this.tempo} BPM`);
    }

    /**
     * Stop auto-play
     */
    stop() {
        if (!this.isPlaying) return;

        if (this.interval) {
            clearInterval(this.interval);
            this.interval = null;
        }

        // All notes off
        for (let n = 0; n < 128; n++) {
            this.noteOff(n);
        }

        this.isPlaying = false;
        this.log('Auto play stopped');
    }

    /**
     * Check if playing
     * @returns {boolean}
     */
    playing() {
        return this.isPlaying;
    }

    /**
     * Set tempo
     * @param {number} bpm
     */
    setTempo(bpm) {
        this.tempo = bpm;
        if (this.isPlaying) {
            // Restart with new tempo
            this.stop();
            this.start();
        }
    }

    /**
     * Set base note
     * @param {number} note
     */
    setBaseNote(note) {
        this.baseNote = note;
    }

    /**
     * Set pattern
     * @param {string} pattern
     */
    setPattern(pattern) {
        this.pattern = pattern;
        if (this.isPlaying) {
            this.stop();
            this.start();
        }
    }

    _playStep(patternData, intervalMs) {
        const pattern = this.pattern;
        const baseNote = this.baseNote;

        if (pattern === 'arpeggio' || pattern === 'scale') {
            const notes = patternData.notes;
            const prevNote = baseNote + notes[(this.step - 1 + notes.length) % notes.length];
            const note = baseNote + notes[this.step % notes.length];

            this.noteOff(prevNote);
            this.noteOn(note, 100);
            this.step++;

        } else if (pattern === 'chord') {
            const chords = patternData.chords;
            const prevChord = chords[(this.step - 1 + chords.length) % chords.length];
            const chord = chords[this.step % chords.length];

            prevChord.forEach(n => this.noteOff(baseNote + n));
            chord.forEach(n => this.noteOn(baseNote + n, 100));
            this.step++;

        } else if (pattern === 'random') {
            const note = baseNote + Math.floor(Math.random() * patternData.range);
            const velocity = 80 + Math.floor(Math.random() * 47);

            this.noteOn(note, velocity);
            setTimeout(() => {
                this.noteOff(note);
            }, intervalMs * 0.8);
        }
    }

    /**
     * Cleanup
     */
    destroy() {
        this.stop();
    }
}
