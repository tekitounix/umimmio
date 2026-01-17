# umidsp - Dependency-free DSP building blocks

Audio synthesis and processing components for embedded and real-time use.

## Features

- No external dependencies (pure C++ standard library)
- Inlinable `tick()` methods for hot path optimization
- No virtual functions or heap allocations

## Quick Start

```cpp
#include <dsp.hh>

using namespace umi::dsp;

SawBL osc;
ADSR env;
env.set_params(10, 100, 0.5f, 200);  // ms: attack, decay, sustain, release

float sample_rate = 48000.0f;
float freq_norm = 440.0f / sample_rate;

env.trigger();
float sample = soft_clip(osc.tick(freq_norm) * env.tick(1.0f / sample_rate));
```

## Build

```bash
xmake build umidsp_test
xmake run umidsp_test
```
