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

## World-Area Uniforms

`GlobalLayout` exposes several world-area extents consumed by fullscreen / RT passes:
- `f4VisibleArea` — tracks the camera each frame; drives the water vertex grid and visible-area RT pixel-to-world mapping. Also anchors the water displacement and Jacobian-normal textures (baked once per frame by the displacement compute pre-pass) since they sample at the same per-meter-snapped resolution as the visible grid. The active LOD's quad count is uploaded as a paired field consumed by both the compute dispatch and the vertex shader's `texelFetch`.
- `f4LightingArea` — LOD-stable + texel-snap anchor for the lighting / ambient composite RTs; pixel-to-world stays bit-identical within an `iLod`. This pattern is appropriate for low-frequency content (lighting) but does NOT suit the water displacement / normal textures — those align to the visible grid, not a stable LOD anchor.

## Camera-Height-Conditional Uniforms

When a uniform field varies with camera eye height, lerp CPU-side and upload the single resolved float — do not pass start/end heights plus low/high targets to the shader. Author-facing controls are exposed as a `HeightLerpWrapperQuartet` (`StartHeight`, `EndHeight`, `Low`, `High` members; `Engine/Source/Ui/HeightLerpWrapperQuartet.h`) in the matching `<Tab>WrappersBase` pair, and the consumer calls `quartet.Resolve(fEyeHeight)` — that is the canonical implementation. Hard-coded-endpoint variants that don't expose author controls (e.g., the `kfWaveFadeEnd` block in `RenderLightingMain` and the speed-zoom factor in `GlobalUniforms.cpp`) call the free `engine::LerpAtHeight(fEyeHeight, fStart, fEnd, fLow, fHigh)` directly with `kfCameraEyeHeightDefault`-derived endpoints. Canonical wrapper-quartet instance: `gSpreadDistanceEnd` resolved in `RenderLightingGlobal`.

## Buffer & Dispatch Patterns

- Uniform layouts accessed via `reinterpret_cast` over persistent-mapped, per-command-buffer indexed buffers. Scalar block layout — no padding.
- Compute dispatches gated by writing `1` or `0` into indirect-dispatch buffers via `PipelineManager::WriteIndirectBuffer`.
- Ping-pong / clear-latch state lives in `Render.h` (shared) or function-local statics (per-file edge detectors) — do not promote the latter to globals.
- Smoke/wind spread is scale-aware: shaders reconstruct world position from the current world-area uniform and remap to the previous-frame texcoord, so no per-command-buffer spread-quad storage buffer is needed.
