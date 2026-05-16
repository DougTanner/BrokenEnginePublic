# Render - GPU Uniform Buffer Population

## Overview

Per-subsystem files populate host-visible uniform buffers each frame before command buffer submission. Each file owns a region of `GlobalLayout` or `MainLayout`. Client-only.

Ownership exception: a downstream pass may zero a count field already written by the upstream pass when its own amplitude-style scale clamps to zero and it has skipped writing the matching array region — this short-circuits the shader's per-element loop without uploading garbage. Both writes (count and per-element array) must be co-gated in the same file.

## Ordering Contract

- Global pass before main pass: main reads `fElapsedTime` from the populated GlobalLayout.
- Within global: Smoke before Wind (wind shares smoke's dynamic world-area / previous-area state).
- Main pass: camera coord rendered first so it lands at index 0; debug render gated by `if constexpr (kbDebugRender)`.

## Camera-Relative Double Precision

CPU computes phase / UV origins in `double`, `std::fmod` reduces to a small modulus, then `static_cast<float>` — prevents precision loss kilometers from origin. Moduli chosen so shader-side size multipliers remain integer after reduction.

## Camera-Height-Conditional Uniforms

When a uniform field varies with camera eye height, lerp CPU-side and upload the single resolved float — do not pass start/end heights plus low/high targets to the shader. Canonical instances: the `kfWaveFadeEnd` block in `RenderLightingMain` and the spread-distance-end blend in `RenderLightingGlobal`. Author-facing controls are exposed as four Wrappers (`*StartHeight`, `*EndHeight`, `*Low`, `*High`) in the matching `<Tab>WrappersBase` pair.

## Buffer & Dispatch Patterns

- Uniform layouts accessed via `reinterpret_cast` over persistent-mapped, per-command-buffer indexed buffers. Scalar block layout — no padding.
- Compute dispatches gated by writing `1` or `0` into indirect-dispatch buffers via `PipelineManager::WriteIndirectBuffer`.
- Ping-pong / clear-latch state lives in `Render.h` (shared) or function-local statics (per-file edge detectors) — do not promote the latter to globals.
- Smoke/wind spread is scale-aware: shaders reconstruct world position from the current world-area uniform and remap to the previous-frame texcoord, so no per-command-buffer spread-quad storage buffer is needed.
