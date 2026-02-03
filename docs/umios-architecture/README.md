# UMI-OS Architecture Specification

This directory contains the target design specifications for UMI-OS.

## Quick Start

- **New to UMI?** → Start with [Fundamentals](00-fundamentals/)
- **Building an App?** → See [Application Guide](01-application/)
- **Working on Kernel?** → See [Kernel Documentation](02-kernel/)
- **Full Documentation** → See [Complete Index](index.md)

## Structure

Documentation is organized to match `lib/umi/` code structure:

| Section | Code Location | Description |
|---------|---------------|-------------|
| `00-fundamentals/` | `lib/umi/core/` | Core concepts (AudioContext, Processor) |
| `01-application/` | `lib/umi/app/` | App development (Events, Parameters, MIDI) |
| `02-kernel/` | `lib/umi/kernel/` | RTOS implementation (Scheduler, MPU, Boot) |
| `03-port/` | `lib/umi/port/` | Platform abstraction (Syscalls, Memory) |
| `04-services/` | `lib/umi/service/` | System services (Shell, Updater, Storage) |
| `05-binary/` | `lib/umi/boot/` | Binary format & security (.umia, Ed25519) |

## Status Legend

- **実装済み** — Fully implemented
- **新設計** — Spec finalized, implementation in progress  
- **将来** — Future direction

## Legacy Note

Previous versions used a flat numbered structure (00-21). The current hierarchical structure aligns with the codebase organization in `lib/umi/`.

本仕様は以下の既存ドキュメントの内容を統合・整理したものである。既存ドキュメントは参考資料として維持するが、矛盾がある場合は**本仕様を正とする**。
