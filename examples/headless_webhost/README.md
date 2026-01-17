# Headless Web Host

A web-based simulation host for headless embedded applications. This project provides a browser-based environment for running and testing UMI-OS applications without physical hardware.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    BackendManager                           │
│  ┌───────────────────────────────────────────────────────┐  │
│  │                 Unified API                            │  │
│  │  - start()     - stop()      - sendMidi()             │  │
│  │  - setParam()  - getState()  - noteOn/Off()           │  │
│  └───────────────────────────────────────────────────────┘  │
│                              │                              │
│         ┌────────────────────┼────────────────────┐         │
│         ▼                    ▼                    ▼         │
│  ┌────────────┐      ┌────────────┐      ┌────────────┐    │
│  │   WASM     │      │   Renode   │      │  CortexM   │    │
│  │  Backend   │      │  Backend   │      │  Backend   │    │
│  │            │      │            │      │  (planned) │    │
│  │ AudioWork- │      │ WebSocket  │      │            │    │
│  │ let+WASM   │      │ + Bridge   │      │ rp2040js   │    │
│  └────────────┘      └────────────┘      └────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

## Backends

### WASM Backend (Default)
- **Fastest**: Real-time audio processing
- **No server required**: Runs entirely in browser
- Cross-compiled embedded code runs directly as WebAssembly

### Renode Backend
- **Cycle-accurate**: Hardware simulation
- Requires Renode + Web Bridge server running locally
- Best for debugging timing-critical code

### Web Cortex-M Backend (Planned)
- Pure JavaScript Cortex-M emulator
- Runs actual ARM binaries in browser
- No server required

## Build

### Standalone Build
```bash
cd examples/headless_webhost
xmake build
xmake run serve
```

### From Project Root
```bash
xmake build headless_webhost
```

## Project Structure

```
headless_webhost/
├── src/                    # C++ source files
│   ├── synth_sim.cc        # WASM entry point
│   ├── synth_processor.hh  # Audio processor
│   └── synth.hh            # Synth implementation
├── web/                    # Web assets
│   ├── index.html          # Main application
│   ├── backend_manager.js  # Backend abstraction
│   ├── renode_adapter.js   # Renode WebSocket adapter
│   └── *_worklet.js        # AudioWorklet processors
├── build/                  # Build output (generated)
├── xmake.lua               # Build configuration
└── README.md
```

## Usage

1. Build the WASM module:
   ```bash
   xmake build
   ```

2. Start the development server:
   ```bash
   xmake run serve
   ```

3. Open http://localhost:8080/ in your browser

4. Select backend (WASM/Renode) and start simulation
