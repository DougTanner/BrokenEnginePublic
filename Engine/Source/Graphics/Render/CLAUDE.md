# Render - GPU Uniform Buffer Population

## Overview

Per-subsystem files populate the host-visible `GlobalLayout` / `MainLayout` uniform buffers each frame before command-buffer submission (the per-frame state path the parent's CB re-record ban mandates). Each file owns a region of one layout. `RenderFrameGlobal` / `RenderFrameMain` are the two entry points the render loop calls. Client-only.

Author-facing tunables read through `g*` wrapper `Get()` accessors; almost all population is a flat copy of slider values, with the non-trivial work being day-cycle derivation, camera-relative precision reduction, and per-LOD draw setup described below.

Ownership exception: a downstream pass may zero a count field already written by the upstream pass when its own amplitude-style scale clamps to zero and it has skipped writing the matching array region — this short-circuits the shader's per-element loop without uploading garbage. Both writes (count and per-element array) must be co-gated in the same file.

## Ordering Contract

- Global pass before main pass: main reads `fElapsedTime` from the populated GlobalLayout (wave phase reduction).
- Within global: Smoke before Wind. Wind reads (does not write) the smoke world-area / previous-area uniforms, so smoke must populate them first.
- Main pass: camera coord rendered first so it lands at index 0; debug render gated by `if constexpr (kbDebugRender)`.

## Main Pass Phases

`RenderFrameMain` drives the collection draw pipeline in fixed order: `BeginRender` (compute capacities, resize GPU buffers, reset counters) → per-coord `Render` (camera first for index-0 stability) → `EndRender` (write indirect-draw counts) → debug overlays. It also selects the camera's visible-area LOD and writes the water / water-skybox indirect-draw ranges plus the paired active-quad dims consumed in lockstep by the displacement compute pre-pass and the water vertex shader's `texelFetch`. Camera matrices (with Perlin camera shake folded into the view-projection), Gerstner wave params, and hex-shield uniforms are written last.

## Day Cycle

The global pass resolves a single sun angle into the full lighting/shadow/water look CPU-side: a piecewise sun/moon color and ambient ramp across morning/noon/evening/night, independent night-gate envelopes for shadows vs. moon color, and shadow feather / stretch / direction terms. Only resolved floats reach the shader — no day-fraction logic lives shader-side.

## Debug Overlays

`kbDebugRender`-gated line/circle overlays for coord-frame edges, island placement boundaries, island valid-area hulls (drawn at the underwater mask depth so terrain never occludes them), and navigation polygon/vertex data; positions come from the fully-interpolated frame.

## Camera-Relative Double Precision

CPU computes phase / UV origins in `double`, `std::fmod` reduces to a small modulus, then `static_cast<float>` — prevents precision loss kilometers from origin. Moduli chosen so shader-side size multipliers remain integer after reduction.

## World-Area Uniforms

The global pass writes the three world-area extents the shaders sample against; the snap-grid / LOD-stability rationale behind them lives in the parent's CameraBase section, not here:
- `f4VisibleArea` — the camera's snapped render-visible rectangle. Anchors the water vertex grid and the water displacement / Jacobian-normal textures, which sample at the same per-meter-snapped resolution.
- `f4LightingArea` — the camera-centered, world-sized-texel rectangle for the lighting deposit / spread / combine RTs (same shadow-style mechanism as `f4ShadowArea`): the textures are allocated 1.5x the wanted pixel size via `game::Camera::kfLightingHeadroomMultiplier` (`TextureManager::LightingDetailTextureSize`) and the texel world size scales by the camera's rate-limited lighting-texel height, so the constant-on-screen-density grid is fixed at a settled eye height (snap-stable under pan) and rescales only while tracking a zoom. The headroom multiplier cancels out of the on-screen window count (it only sizes the texture for transient overflow). Spread/combine early-out outside the on-screen window (derived in-shader from `f4VisibleArea` vs `f4LightingArea`); the prior frame's footprint (`f4LightingAreaPrevious`) plus a temporal-blend weight feed the lighting temporal-accumulation pass (world-position reprojection + EMA, same previous-area pattern as shadow/smoke/wind).
- `f4ShadowArea` / `f4ShadowAreaExtra` — the camera-centered texel footprint, snapped to the shadow texel grid (integer-texel XY pan). Texel world size derives from the analytic straight-down frustum width (`gFov`/aspect) sized so a constant on-screen pixel count (`textureWidth / kfShadowHeadroomMultiplier`) spans it at the camera's rate-limited shadow-texel height, then read against the actual texture extent: at a **settled** eye height it is fixed, so the grid snaps cleanly under pan with no pop or shimmer; while a zoom is tracked the height ramps and the texels rescale on an imperceptibly slow crawl. Each frame also writes a centered visible sub-window in texels (`iShadowVisibleMinX/MinY/MaxX/MaxY`) — only that window (plus a `kiShadowWindowMargin` blur border) is ray-marched/blurred. The window is constant on-screen (`textureWidth / kfShadowHeadroomMultiplier`) at every settled height — coverage is preserved at all heights, the texels just coarsen with zoom rather than cropping; only a fast zoom-out that outruns the ramp transiently grows the window toward the full texture (`std::min`-clamped to the extent, with the CLAMP_TO_BORDER edge covering any overflow). The `Extra` variant extends a half-width on the sun side to cover the 1.5x-wider elevation texture. The prior frame's footprint is also written (`f4ShadowAreaPrevious`) to feed the temporal-accumulation pass, which reprojects history into the current grid by world position to de-flicker the texel ramp (same previous-area pattern as smoke/wind).

## Camera-Height-Conditional Uniforms

When a uniform field varies with camera eye height, lerp CPU-side and upload the single resolved float — do not pass start/end heights plus low/high targets to the shader. Author-facing controls are exposed as a `HeightLerpWrapperQuartet` (`StartHeight`, `EndHeight`, `Low`, `High` members; `Engine/Source/Ui/HeightLerpWrapperQuartet.h`) in the matching `<Tab>WrappersBase` pair, and the consumer calls `quartet.Resolve(fEyeHeight)` — that is the canonical implementation. Hard-coded-endpoint variants that don't expose author controls (e.g., the `kfWaveFadeEnd` block in `RenderLightingMain` and the speed-zoom factor in `GlobalUniforms.cpp`) call the free `engine::LerpAtHeight(fEyeHeight, fStart, fEnd, fLow, fHigh)` directly with `kfCameraEyeHeightDefault`-derived endpoints. Canonical wrapper-quartet instance: `gSpreadDistanceEnd` resolved in `RenderLightingGlobal`.

## Buffer & Dispatch Patterns

- Uniform layouts accessed via `reinterpret_cast` over persistent-mapped, per-command-buffer indexed buffers. Scalar block layout — no padding.
- Compute dispatches gated by writing `1` or `0` into indirect-dispatch buffers via `PipelineManager::WriteIndirectBuffer`.
- Ping-pong / clear-latch state lives in `Render.h` (shared) or function-local statics (per-file edge detectors) — do not promote the latter to globals.
- Smoke/wind spread is scale-aware: shaders reconstruct world position from the current world-area uniform and remap to the previous-frame texcoord, so no per-command-buffer spread-quad storage buffer is needed.
