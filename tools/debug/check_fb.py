#!/usr/bin/env python3
"""Check feedback counters while playing audio."""

import sounddevice as sd
import numpy as np
import time
from pyocd.core.helpers import ConnectHelper

# Generate test tone
duration = 5
sample_rate = 48000
freq = 440
t = np.linspace(0, duration, int(duration * sample_rate), endpoint=False)
stereo = np.column_stack((0.3 * np.sin(2 * np.pi * freq * t), 
                          0.3 * np.sin(2 * np.pi * freq * t))).astype(np.float32)

# Find UMI device
umi_idx = None
for i, d in enumerate(sd.query_devices()):
    if 'UMI' in d['name']:
        umi_idx = i
        break

if umi_idx is None:
    print("UMI device not found!")
    exit(1)

print(f'Playing to device {umi_idx}')
sd.play(stereo, samplerate=sample_rate, device=umi_idx, blocking=False)
time.sleep(1.0)  # Wait for streaming to start

with ConnectHelper.session_with_chosen_probe(target_override='stm32f407vg') as session:
    target = session.target
    target.halt()
    
    # Read debug counters
    fb_count = target.read32(0x200010d8)    # dbg_fb_count
    fb_actual = target.read32(0x200010dc)   # dbg_fb_actual
    fb_sent = target.read32(0x200010e0)     # dbg_fb_sent
    streaming = target.read32(0x200010c0)   # dbg_streaming
    feedback = target.read32(0x200010d4)    # dbg_feedback
    sof_count = target.read32(0x200010d0)   # dbg_sof_count
    
    target.resume()
    
    print(f'dbg_streaming: {streaming}')
    print(f'dbg_sof_count: {sof_count}')
    print(f'dbg_feedback: 0x{feedback:06x} ({feedback / 16384:.3f} kHz)')
    print(f'dbg_fb_sent (AudioInterface): {fb_sent}')
    print(f'dbg_fb_count (HAL ep_write): {fb_count}')
    print(f'dbg_fb_actual: 0x{fb_actual:06x}')

sd.stop()
print("Done")
