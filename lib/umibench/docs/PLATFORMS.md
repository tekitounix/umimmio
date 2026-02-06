# Platforms

[Docs Home](INDEX.md) | [日本語](ja/PLATFORMS.md)

## Header Resolution Model

User code always includes:

- `<umibench/bench.hh>`
- `<umibench/platform.hh>`

The build configuration selects which physical `platform.hh` is used.

## Built-in Targets

- Host: `platforms/host/umibench/platform.hh`
- WASM: `platforms/wasm/umibench/platform.hh`
- STM32F4 (Cortex-M): `platforms/arm/cortex-m/stm32f4/umibench/platform.hh`

## Shared Platform Base

`platforms/common/platform_base.hh` centralizes shared host/wasm behavior:

- `PlatformBase<Timer, Output>`
- `PlatformAutoInit<Platform>`

This keeps target files small and consistent.

## Adding a New Target

1. Implement timer/output backend.
2. Add `platforms/<your-target>/umibench/platform.hh`.
3. Wire include paths and target rules in `xmake.lua`.
4. Reuse common tests where possible.
