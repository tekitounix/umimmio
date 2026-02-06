# Usage

[Docs Home](INDEX.md) | [日本語](ja/USAGE.md)

## Core API

- `umi::bench::Runner<Timer>`
- `Runner::calibrate<N>()`
- `Runner::run<N>(func)`
- `Runner::run<N>(iterations, func)`
- `umi::bench::report<Platform>(name, stats)`
- `umi::bench::report_compact<Platform>(name, stats)`

## Recommended Flow

1. Construct `Runner<Platform::Timer>`.
2. Call `calibrate<N>()` once.
3. Execute `run` with desired sample/iteration counts.
4. Emit `report` (or `report_compact`).

## Measurement Semantics

- Calibration uses median-based baseline estimation.
- Corrected samples are clamped at `0`.
- `run<N>(iters, fn)` invokes `fn` exactly `N * iters` times.

## Numeric Behavior

- Reported min/max/median are 64-bit safe.
- Even-sample median avoids overflow (`a + (b - a) / 2`).
- `calibrate<0>()` is rejected at compile time.

## Custom Output / Timer

You can provide your own target backend via `Platform` aliases.

Required contracts:

- `TimerLike`: `enable()`, `now()`, and `Counter`
- `OutputLike`: `init()`, `putc()`, `puts()`, `print_uint(uint64_t)`, `print_double(double)`

See:

- `include/umibench/timer/concept.hh`
- `include/umibench/output/concept.hh`
