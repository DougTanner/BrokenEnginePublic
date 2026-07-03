# Render - GPU Uniform Buffer Population

## Overview

Per-subsystem files populate the host-visible `GlobalLayout` / `MainLayout` uniform buffers each frame before command-buffer submission (the per-frame state path the parent's CB re-record ban mandates). Each file owns its region of each layout it writes. `RenderFrameGlobal` / `RenderFrameMain` are the two entry points the render loop calls. Client-only.

Author-facing tunables read through `g*` wrapper `Get()` accessors; almost all population is a flat copy of slider values, with the non-trivial work being day-cycle derivation, camera-relative precision reduction, and per-LOD draw setup described below.

Ownership exception: a downstream pass may zero a count field already written by the upstream pass when its own amplitude-style scale clamps to zero and it has skipped writing the matching array region — this short-circuits the shader's per-element loop without uploading garbage. Both writes (count and per-element array) must be co-gated in the same file.

## Ordering Contract

- Global pass before main pass: main reads `fElapsedTime` from the populated GlobalLayout (wave phase reduction).
- Global pass before main pass (ping-pong): `RenderWindGlobal` flips `giWindTextureIndex` each frame (`WindUniforms.cpp`, `giWindTextureIndex = 1 - giWindTextureIndex`); the main pass reads that index to route wind deposits to the matching A/B pipeline (`WindTrails/WindTrailsRender.cpp` `EndRender`, `WindRadials/WindRadialsRender.cpp` `EndRender` — indirect counts gated on index parity). Unlike `fElapsedTime`, running main before global deposits into the texture the wind sim is about to overwrite — the most consequential global-before-main dependency.
- Within global: Smoke before Wind — GPU-side dependency. The wind *shaders* read (do not write) the smoke world-area / previous-area uniforms (`Wind/WindSpreadOne.comp`, `WindSpreadTwo.comp`, `WindOccupancyDilate.comp` sample `f4SmokeArea` / `f4PreviousSmokeArea`); `RenderWindGlobal` performs no CPU-side read. Both populates land in the same mapped GlobalLayout buffer before the single Global submit, so the CPU-side smoke-before-wind population order is convention/documentation-only.

## Main Pass Phases

`RenderFrameMain` drives the collection draw pipeline in fixed order: `BeginRender` (compute capacities, resize GPU buffers, reset counters) → per-coord `Render` (camera first for index-0 stability) → `EndRender` (write indirect-draw counts) → debug overlays. It also selects the camera's visible-area LOD and writes the water indirect-draw range plus the paired active-quad dims consumed in lockstep by the displacement compute pre-pass and the water vertex shader's `texelFetch`. Camera matrices (with Perlin camera shake folded into the view-projection), Gerstner wave params, and hex-shield uniforms are written last. The sequence relies on the client's never-empty active-coords invariant: skipping it leaves each command buffer's last-written indirect counts in place while the record-once Main CB resubmits, ghost-drawing stale instances — any future skip path must still flush the counts.

## Day Cycle

The global pass resolves a single sun angle into the full lighting/shadow/water look CPU-side: a piecewise sun/moon color and ambient ramp across morning/noon/evening/night, independent night-gate envelopes for shadows vs. moon color, and shadow feather / stretch / direction terms. Only resolved floats reach the shader — no day-fraction logic lives shader-side.

## Debug Overlays

`kbDebugRender`-gated line/circle overlays for coord-frame edges, island placement boundaries, island valid-area hulls (drawn at the underwater mask depth that defines the hull; debug lines are a no-depth-test overlay, so nothing occludes them), and navigation polygon/vertex data — all positions from static coord-frame data. The separate game-specific per-collection debug pass reads the fully-interpolated frame.

## Camera-Relative Double Precision

CPU computes phase / UV origins in `double`, `std::fmod` reduces to a small modulus, then `static_cast<float>` — prevents precision loss kilometers from origin. Moduli chosen so shader-side size multipliers remain integer after reduction. Two invariants on top:

- Reduced-time accumulators integrate `size * speed * dt` per frame (then fmod) instead of recomputing `fmod(size * speed * t)` — keeps UV phase continuous when size or speed slide smoothly (zoom-driven speed lerp); the recompute form jumps proportionally to playtime.
- Rotated octaves rotate cameraXY (and, for scrolling samples, the per-frame reduced-time delta — a vec2, so scroll direction follows the pattern rotation) on the CPU by the same R(−θ) the shader uses, then fmod. Rotating the reduced origin or time delta shader-side instead makes the wrap shift non-integer for any θ not a multiple of π/2, producing a visible pattern jump at every wrap; the shader consumes both outside its UV-rotation multiply.

## World-Area Uniforms

The global pass writes the world-area extents the shaders sample against; the snap-grid / settled-height / temporal-accumulation rationale lives in the parent's CameraBase section, not here. Shadow and lighting also write the previous frame's footprint plus a temporal-blend weight for the reprojection passes. Facts specific to this directory:

- `f4VisibleArea` — the camera's snapped render-visible rectangle; anchors the water vertex grid and the water displacement / Jacobian-normal textures.
- `f4ShadowArea` — texel world size derives from the analytic straight-down frustum width (`gFov`/aspect), never the snapped render-area width; integer-texel snap via `int64_t` texel indices. The visible sub-window uses live eye height, `std::min`-clamped to the texture extent. `f4ShadowAreaExtra` extends a half-width on the sun side to cover the 1.5x-wider elevation texture.
- `f4LightingArea` — snapped to the deposit texel grid specifically (deposit is where lights rasterize, so its grid must pan in integer texels); spread/combine/temporal resample the same world rectangle at their own resolutions.

## Camera-Height-Conditional Uniforms

When a uniform varies with camera eye height, lerp CPU-side and upload the single resolved float — never pass endpoint heights and low/high targets to the shader. Author-facing controls use a `HeightLerpWrapperQuartet` (`Engine/Source/Ui/HeightLerpWrapperQuartet.h`) in the matching `<Tab>WrappersBase` pair; the consumer calls `Resolve(fEyeHeight)`. Hard-coded-endpoint variants without author controls call free `engine::LerpAtHeight` directly — the fade endpoint is single-sourced as `game::Camera::kfWaveFadeEndHeight`, referenced by both `RenderLightingMain` (lighting) and the water population (`GlobalUniforms.cpp`).

## Buffer & Dispatch Patterns

- Uniform layouts accessed via `reinterpret_cast` over persistent-mapped, per-command-buffer indexed buffers. Scalar block layout — no padding. Never *read back* from these write-combined maps — each readback stalls on memory latency. When population needs to read intermediate results (e.g. the Gerstner wave clamp/phase math in `MainUniforms.cpp`), compute into a file-static cached staging copy and `std::memcpy` the populated region (count entries) into the map once. The Gerstner path also splits frame-invariant terms (directions/omega/phi/clamped base amplitude, rebuilt only when a consumed-tunables snapshot changes) from per-frame terms (eye-height-scaled amplitude + `fmod` phase), so a steady camera pays only the phase pass.
- Compute dispatches gated by writing `1` or `0` into indirect-dispatch buffers via `Pipeline::WriteIndirectBuffer`. The water-displacement pass uses a separate host-visible compute dispatch-indirect path (`Pipeline::WriteIndirectComputeBuffer`) that writes real per-frame workgroup dims sized to the active LOD sub-region rather than a binary gate.
- Cross-file or externally-reset state lives as `inline` globals in `Render.h`; single-file edge detectors / latches stay function-local statics — do not promote. All latches assume the entry points run exactly once per frame.
- `gbShadowTemporalReset` / `gbLightingTemporalReset` (set by the shadow/lighting texture-recreate paths) re-arm the first-frame guards inside the shadow/lighting population: a Graphics recreate rebuilds the history textures with undefined contents while function-local statics survive, so the reset frame forces temporal blend to pure-current and previous-area = current-area, re-seeding valid history.
- Wind ping-pong textures take a complementary approach: `CreateWindTextures` hard-clears both to zero at creation/recreate time (`OneShotCommandBuffer + vkCmdClearColorImage`, kShaderReadOnly→kTransferDestination→clear→kShaderReadOnly) so inactive tiles never retain undefined contents across a device-lost recreate. No per-frame reset flag exists — wind spread only touches occupancy-active tiles and its exact-zero decay self-extinguishes them, so a one-time creation clear is sufficient.
- The `giShadow*` / `giLighting*` pixel-count globals in `Render.h` exist solely as readouts for `Profile/ProfileScreens.cpp`.
- Smoke/wind spread is scale-aware: shaders reconstruct world position from the current world-area uniform and remap to the previous-frame texcoord, so no per-command-buffer spread-quad storage buffer is needed.
