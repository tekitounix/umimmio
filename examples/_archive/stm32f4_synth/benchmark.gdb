# GDB script to read benchmark results
# Usage: arm-none-eabi-gdb -x benchmark.gdb build/stm32f4_synth/debug/stm32f4_synth

target extended-remote :3333
monitor reset halt
load
monitor reset run

# Wait for some audio processing
shell sleep 5

# Halt to read values
monitor halt

# Read benchmark structures
# BenchmarkStats layout:
#   uint32_t last_cycles    (offset 0)
#   uint32_t min_cycles     (offset 4)
#   uint32_t max_cycles     (offset 8)
#   uint32_t total_cycles   (offset 12)
#   uint32_t count          (offset 16)

printf "\n=== Benchmark Results (after 5 seconds) ===\n"
printf "64 frames @ 48kHz = 1.33ms per call\n"
printf "168MHz CPU = 168000 cycles per ms\n"
printf "Available budget: ~224000 cycles per fill_audio_buffer call\n\n"

printf "PI+ASRC Mode (cubic hermite interpolation):\n"
set $pi_base = (uint32_t*)&bench_pi_asrc
printf "  Last:  %u cycles (%.2f us)\n", $pi_base[0], $pi_base[0] / 168.0
printf "  Min:   %u cycles (%.2f us)\n", $pi_base[1], $pi_base[1] / 168.0
printf "  Max:   %u cycles (%.2f us)\n", $pi_base[2], $pi_base[2] / 168.0
printf "  Avg:   %u cycles (%.2f us)\n", $pi_base[3] / $pi_base[4], ($pi_base[3] / $pi_base[4]) / 168.0
printf "  Count: %u calls\n", $pi_base[4]
printf "  CPU%%:  %.1f%%\n\n", ($pi_base[3] / $pi_base[4]) * 100.0 / 224000.0

printf "XMOS Mode (no interpolation, feedback only):\n"
set $xmos_base = (uint32_t*)&bench_xmos
printf "  Last:  %u cycles (%.2f us)\n", $xmos_base[0], $xmos_base[0] / 168.0
printf "  Min:   %u cycles (%.2f us)\n", $xmos_base[1], $xmos_base[1] / 168.0
printf "  Max:   %u cycles (%.2f us)\n", $xmos_base[2], $xmos_base[2] / 168.0
printf "  Avg:   %u cycles (%.2f us)\n", $xmos_base[3] / $xmos_base[4], ($xmos_base[3] / $xmos_base[4]) / 168.0
printf "  Count: %u calls\n", $xmos_base[4]
printf "  CPU%%:  %.1f%%\n\n", ($xmos_base[3] / $xmos_base[4]) * 100.0 / 224000.0

# Resume
monitor reset run
quit
