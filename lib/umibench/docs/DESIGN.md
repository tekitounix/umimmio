# umibench Design

[日本語](ja/DESIGN.md)

## 1. Vision

`umibench` is a cross-target benchmark library with a single user-facing style:

1. Benchmark code is written as ordinary C++ `main`.
2. The same benchmark source is reused across host, wasm, and embedded targets.
3. Target replacement is done by build configuration, not user-side conditional compilation.
4. Hardware/bootstrap details are hidden before user `main`.
5. Output supports both quick human reading and machine-consumable analytics.

---

## 2. Non-Negotiable Requirements

### 2.1 Common `main` Form

User benchmark code should stay target-agnostic:

```cpp
#include <umibench/bench.hh>
#include <umibench/platform.hh>

int main() {
    using Timer = umi::bench::Platform::Timer;

    umi::bench::Runner<Timer> runner;
    runner.calibrate<64>();

    auto stats = runner.run<64>(100, [] {
        // benchmark body
    });

    umi::bench::report<umi::bench::Platform>("sample", stats);
    return 0;
}
```

Rules:

1. No target-specific include in benchmark source.
2. No target-specific type names in benchmark source.
3. No explicit `Platform::init()` call in user `main`.

### 2.2 Header Resolution Strategy

`<umibench/platform.hh>` is a stable logical include path. Build system include order resolves it to the target implementation.

1. Host build resolves to host platform implementation.
2. Wasm build resolves to wasm platform implementation.
3. Embedded build resolves to board/SoC implementation.

### 2.3 Target Scope

`platforms/` is architecture-extensible and not STM32F4-only.

1. ARM Cortex-M family
2. ARM Cortex-A family
3. Xtensa
4. RISC-V

Board/SoC variants will grow. The platform contract must remain uniform.

### 2.4 Dependency Boundaries

Layering is strict:

1. `platforms/*` depends on `umimmio` for low-level register/peripheral access.
2. `tests/*` depends on `umitest` for assertions and test reporting.
3. `include/umibench/*` is public API and must not depend on `platforms/*`.

Reference dependency graph:

```text
benchmark app -> umibench/include API
umibench/platforms/* -> umimmio
umibench/tests/* -> umitest
```

### 2.5 Initialization Boundary

Hardware/bootstrap initialization is hidden before user `main`.

1. Cortex-M/A targets: reset/startup code performs required initialization.
2. Host/wasm targets: runtime hook or loader path performs initialization.
3. Internal `Platform::init()` may exist, but it is not a user API requirement.

---

## 3. Current Layout

```text
lib/umibench/
├── README.md
├── xmake.lua
├── docs/
│   └── DESIGN.md
├── examples/
│   ├── minimal.cc
│   ├── function_style.cc
│   ├── lambda_style.cc
│   └── instruction_bench.cc
├── include/umibench/
│   ├── bench.hh
│   ├── core/
│   │   ├── measure.hh
│   │   ├── runner.hh
│   │   └── stats.hh
│   ├── output/
│   │   ├── concept.hh
│   │   └── null.hh
│   └── timer/
│       └── concept.hh
├── platforms/
│   ├── host/
│   │   ├── chrono.hh
│   │   ├── stdout.hh
│   │   └── umibench/platform.hh
│   ├── wasm/
│   │   ├── umibench/platform.hh
│   │   └── xmake.lua
│   └── arm/cortex-m/
│       ├── cortex_m_mmio.hh
│       ├── dwt.hh
│       └── stm32f4/
│           ├── linker.ld
│           ├── startup.cc
│           ├── syscalls.cc
│           ├── umibench/platform.hh
│           ├── xmake.lua
│           ├── examples/
│           │   └── instruction_bench_cortexm.cc
│           └── renode/bench_stm32f4.resc
└── tests/
    ├── test_main.cc
    ├── test_timer_measure.cc
    ├── test_stats_runner.cc
    ├── test_platform_output_report.cc
    ├── test_integration.cc
    ├── test_fixture.hh
    ├── compile_fail/
    │   └── calibrate_zero.cc
    └── xmake.lua
```

---

## 4. Growth Layout

```text
lib/umibench/
├── include/umibench/                   # Public API only (target-agnostic)
│   ├── bench.hh
│   ├── core/
│   ├── timer/
│   │   └── concept.hh
│   └── output/
│       ├── concept.hh
│       ├── null.hh
│       └── data_sink.hh               # Future: structured export
├── platforms/
│   ├── host/
│   │   └── umibench/platform.hh
│   ├── wasm/
│   │   └── umibench/platform.hh
│   ├── arm/
│   │   ├── cortex-m/                   # Shared DWT/CoreSight layer
│   │   │   ├── stm32f4/
│   │   │   ├── stm32h7/
│   │   │   └── rp2040/
│   │   └── cortex-a/
│   ├── xtensa/
│   └── riscv/
├── examples/
│   ├── minimal.cc
│   ├── function_style.cc
│   ├── lambda_style.cc
│   ├── instruction_bench.cc
│   └── advanced_output.cc             # Future: structured export demo
└── tests/
    ├── test_main.cc
    ├── test_*.cc
    ├── test_fixture.hh
    ├── compile_fail/
    │   └── calibrate_zero.cc
    └── xmake.lua
```

Notes:

1. Public API headers stay under `include/umibench` — no target-specific headers.
2. Target-specific implementation details (timers, output backends, startup) stay under `platforms/*`.
3. Target hierarchy is architecture-based: DWT/CoreSight is shared across all Cortex-M regardless of vendor.
4. Target-specific examples may be added under `platforms/<arch>/<board>/examples/`.
5. `<umibench/platform.hh>` is resolved by build-system include path, not by physical file in `include/`.

---

## 5. Programming Model

### 5.0 API Reference

Public entrypoint: `include/umibench/bench.hh`

Core API:

- `umi::bench::Runner<Timer>` — benchmark runner
- `Runner::calibrate<N>()` — baseline calibration
- `Runner::run<N>(func)` — measure with default iterations
- `Runner::run<N>(iterations, func)` — measure with explicit iterations
- `umi::bench::report<Platform>(name, stats)` — full report
- `umi::bench::report_compact<Platform>(name, stats)` — compact report

Core headers:

- `include/umibench/core/measure.hh`
- `include/umibench/core/runner.hh`
- `include/umibench/core/stats.hh`

Concepts:

- `include/umibench/timer/concept.hh` — `TimerLike`: `enable()`, `now()`, `Counter`
- `include/umibench/output/concept.hh` — `OutputLike`: `init()`, `putc()`, `puts()`, `print_uint(uint64_t)`, `print_double(double)`

### 5.1 Minimal Path

Required minimal flow:

1. Construct `Runner<Timer>`.
2. Optionally call `calibrate`.
3. Execute `run`.
4. Emit via `report`.
5. Return from `main`.

### 5.2 Function and Lambda Equivalence

Both callable styles are valid:

```cpp
void process();
runner.run<64>(100, process);
runner.run<64>(100, [] { process(); });
```

### 5.3 Advanced Path

Advanced usage includes:

1. configurable sample count and iteration count,
2. rich statistical analysis,
3. output sink selection for text and data export,
4. optional raw sample retention for jitter/outlier analysis.

---

## 6. Benchmark Algorithm Specification

### 6.1 Run Pipeline

For one benchmark case:

1. Timer backend is available before user `main`.
2. Optional warmup path runs to reduce cold-start bias.
3. Optional calibration estimates measurement overhead baseline.
4. `N` samples are measured with `M` iterations each.
5. Baseline correction is applied per sample.
6. Aggregate statistics are computed and reported.

### 6.2 Baseline Estimation

Baseline should use a robust estimator to reduce jitter impact.

1. Measure an empty or near-empty callable repeatedly.
2. Use median (default) or another robust estimator.
3. Persist baseline in runner state for later runs.
4. Clamp corrected sample values at zero.

Default corrected value:

```text
corrected = max(raw - baseline, 0)
```

### 6.3 Callable Execution Semantics

For `run<N>(iters, fn)`:

1. Sample count is exactly `N`.
2. `fn` invocation count is exactly `N * iters`.
3. No hidden extra invocation is allowed in the measured loop.
4. Any warmup invocation must occur outside measured sample window.

### 6.4 Timer Semantics

Timer contract for implementations:

1. `now()` is monotonic for benchmark duration.
2. Arithmetic handles wrap-around according to timer type rules.
3. Units are explicit (`cycles`, `ns`, etc.) and reported in metadata.
4. Resolution limitations are documented per target backend.

### 6.5 Statistics Semantics

`Stats` minimum set:

1. `min`, `max`, `median`
2. `mean`
3. `stddev` (population or sample must be explicitly defined)
4. `cv = stddev / mean` (defined as `0` when `mean == 0`)
5. `samples`, `iterations`
6. optional `sum`, optional `raw_samples`

Numerical behavior:

1. use stable accumulation strategy to avoid overflow where possible,
2. document precision limits for integer-based timers,
3. keep deterministic ordering for median when `N` is even.

### 6.6 Stability and Portability Constraints

Tests and API behavior should avoid brittle timing assumptions:

1. do not assert fixed absolute latency across targets,
2. assert semantic invariants (non-negativity, counts, ordering),
3. keep host/wasm/embedded timer resolution differences explicit.

---

## 7. Output Model Specification

### 7.1 Human-Readable Report

Default report must remain compact and comparable:

1. benchmark name,
2. target name,
3. timer unit,
4. `samples` and `iterations`,
5. `min/max/median/mean/stddev/cv`.

Recommended single-line format:

```text
name=sample target=host unit=ns n=64 iters=100 min=... max=... median=... mean=... stddev=... cv=...
```

### 7.2 Structured Data Export

Every human report must have equivalent structured representation.

Required schema fields:

1. `schema_version`
2. `name`
3. `target`
4. `timer_unit`
5. `samples`
6. `iterations`
7. `min`
8. `max`
9. `median`
10. `mean`
11. `stddev`
12. `cv`

Optional schema fields:

1. `raw_samples`
2. `baseline`
3. `toolchain`
4. `opt_level`
5. `git_rev`
6. `timestamp`

Supported formats:

1. CSV
2. JSON Lines
3. optional binary/event stream

### 7.3 Graph/Data-Science Readiness

Library itself does not draw charts, but emitted data must allow:

1. jitter/outlier plots from per-sample values,
2. trend/diff dashboards across commits and targets,
3. reproducible cross-target comparison via metadata.

---

## 8. Test Strategy

1. Core test logic is split by concern under `tests/test_*.cc` with a single `test_main.cc` entrypoint.
2. Each test target reuses the same test sources and swaps platform backend via include/build config.
3. `tests/*` depends on `umitest`.
4. Timing tests focus on semantics, not fixed absolute value across architectures.
5. CI baseline includes host and wasm; embedded targets run in dedicated jobs (for example Renode).
6. Compile-fail checks (for API contract guards) are automated under `tests/compile_fail/`.

Target-specific tests are only for backend-specific behavior that common tests cannot verify.

### 8.1 Test Layout

- `tests/test_main.cc`: test entrypoint
- `tests/test_timer_measure.cc`: timer/measurement semantics
- `tests/test_stats_runner.cc`: statistics and runner behavior
- `tests/test_platform_output_report.cc`: platform/output/report checks
- `tests/test_integration.cc`: end-to-end benchmark flow
- `tests/compile_fail/calibrate_zero.cc`: compile-fail guard test

### 8.2 Running Tests

```bash
xmake test                              # all targets
xmake test 'test_umibench/*'            # host only
xmake test 'test_umibench_compile_fail/*'  # compile-fail only
xmake test 'umibench_wasm/*'            # wasm only
```

### 8.3 Quality Gates

- Functional tests pass on host
- WASM tests pass (when `emcc` exists)
- Compile-fail contract test passes
- Embedded cross-build passes in CI (`gcc-arm`)
- Embedded `clang-arm` profile validated locally before release

---

## 9. Example Strategy

Examples should represent learning stages while keeping source target-agnostic.

1. `minimal`: shortest complete benchmark.
2. `function_style`: existing function symbol benchmarking.
3. `lambda_style`: inline callable benchmarking.
4. `advanced_output`: report plus structured export.

If a target needs setup-specific demonstration, keep it under `platforms/<name>/examples/` and clearly mark it as target-specific.

---

## 10. Near-Term Improvement Plan

1. Finalize and freeze `<umibench/platform.hh>` contract.
2. Move target-only output backends (for example UART variants) under `platforms/*`.
3. Reduce redundant test entry points after common-path validation.
4. Add structured sink API (`CSV`, `JSONL`, optional raw-sample export).
5. Add onboarding templates for Cortex-M/A, Xtensa, and RISC-V backends.
6. Document target metadata requirements for reproducible benchmarking.

---

## 11. Design Principles

1. User `main` stays ordinary C++ and target-agnostic.
2. Build system selects backend; benchmark source remains unchanged.
3. Public/private boundary is strict (`include` API vs `target` implementation).
4. Benchmark semantics and stats must be predictable and explicit.
5. Human output and machine output are both first-class.
