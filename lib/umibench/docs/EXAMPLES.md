# Examples

[Docs Home](INDEX.md) | [日本語](ja/EXAMPLES.md)

## Example Files

- `examples/minimal.cc`: shortest complete benchmark
- `examples/function_style.cc`: benchmark existing function symbols
- `examples/lambda_style.cc`: benchmark inline lambdas
- `examples/instruction_bench.cc`: grouped instruction-style micro-benchmarks
- `platforms/arm/cortex-m/stm32f4/examples/instruction_bench_cortexm.cc`: Cortex-M specific variant

## Recommended Learning Order

1. `minimal.cc`
2. `function_style.cc`
3. `lambda_style.cc`
4. `instruction_bench.cc`

## Guidance

- Keep examples target-agnostic unless target behavior must be demonstrated.
- Put target-specific examples under `platforms/<arch>/<board>/examples/`.
- Use examples to show API usage patterns, not test assertions.
