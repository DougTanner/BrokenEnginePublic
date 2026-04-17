# Render - GPU Uniform Buffer Population

## Overview

Per-subsystem files populate host-visible uniform buffers each frame before command buffer submission. Each file owns a region of `GlobalLayout` or `MainLayout`. Client-only.

## Ordering Contract

- Global pass before main pass: main reads `fElapsedTime` from the populated GlobalLayout.
- Within global: Smoke before Wind (wind reads shared spread-quad coordinates).
- Main pass: camera coord rendered first so it lands at index 0; debug render gated by `if constexpr (kbDebugRender)`.

## Camera-Relative Double Precision

CPU computes phase / UV origins in `double`, `std::fmod` reduces to a small modulus, then `static_cast<float>` — prevents precision loss kilometers from origin. Moduli chosen so shader-side size multipliers remain integer after reduction.

## Buffer & Dispatch Patterns

- Uniform layouts accessed via `reinterpret_cast` over persistent-mapped, per-command-buffer indexed buffers. Scalar block layout — no padding.
- Compute dispatches gated by writing `1` or `0` into indirect-dispatch buffers via `PipelineManager::WriteIndirectBuffer`.
- Ping-pong / clear-latch state lives in `Render.h` (shared) or function-local statics (per-file edge detectors) — do not promote the latter to globals.
