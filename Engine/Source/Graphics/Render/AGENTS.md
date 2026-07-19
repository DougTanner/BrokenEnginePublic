# Render - GPU Uniform Buffer Population

## Overview

Client-only per-subsystem files populate the host-visible `GlobalLayout` / `MainLayout` buffers before submission, providing the per-frame state path required by record-once command buffers. Each file owns its layout region and reads author tunables through `g*` wrapper accessors.

A downstream pass may zero an upstream count only when its amplitude clamps to zero and the matching array write is skipped; gate both writes together so shaders never consume an unwritten region.

## Ordering Contract

- Global population precedes Main: Main consumes elapsed time, and Wind's flipped ping-pong index routes later deposits to the texture that spread will not overwrite.
- Smoke population precedes Wind within Global because wind shaders consume smoke's current and previous world areas. Both writes reach the same mapped layout before one submission, so this CPU order documents the shader dependency rather than creating separate GPU synchronization.

## Main Pass Phases

`RenderFrameMain` runs collection `BeginRender` → per-coord `Render` → `EndRender` → debug overlays, with the camera rendered first for stable index zero. It also writes the paired water LOD draw range and active-quad dimensions used by compute and vertex stages. If no coord is renderable, it still runs the begin/end pairs to flush entity, effect, and debug indirect counts; record-once command buffers would otherwise ghost-draw stale instances. Water and lighting keep their last self-consistent parameters because they always draw independently of coord contents.

## Day Cycle

Global population resolves the sun angle into sun/moon colors, ambient light, night gates, and shadow terms. Only resolved floats reach shaders; day-fraction policy stays CPU-side.

## Debug Overlays

`kbDebugRender` gates no-depth-test line/circle overlays for coord-frame, island-placement, valid-area-hull, and navigation data, all positioned from static coord-frame data. Valid-area hulls use the underwater-mask threshold depth that defines their boundary. The game-specific collection debug pass reads the fully interpolated frame.

## Camera-Relative Double Precision

Compute phase and UV origins in `double`, reduce them with `std::fmod`, then cast to float so distant-world coordinates retain precision. Reduction moduli must keep shader-side size multipliers integral.

- Integrate reduced time from `size * speed * dt`; recomputing from total playtime jumps when size or speed changes.
- Rotate camera origins by the shader's inverse pattern rotation before reduction. Rotate scrolling deltas by the difference between scroll and pattern directions so the shader restores the intended world direction; shader-side rotation before wrapping creates non-integral wrap jumps. Rotation preserves the delta's √2 magnitude, keeping direction-independent speed and the per-component fmod-10 wrap valid for every direction.

## World-Area Uniforms

Global population writes the world-area extents sampled by shaders; snap-grid and temporal rationale lives in the parent CameraBase section. Shadow and lighting also publish their previous footprint and temporal blend:

- `f4VisibleArea` — the camera's snapped render-visible rectangle; anchors the water vertex grid and the water displacement / Jacobian-normal textures.
- `f4ShadowArea` — texel size derives from the analytic straight-down frustum width, snaps through integer texel indices, and clamps the live-height sub-window to the texture. The extra area extends sunward for the wider elevation target.
- `f4LightingArea` — snapped to the deposit texel grid specifically (deposit is where lights rasterize, so its grid must pan in integer texels); spread/combine/temporal resample the same world rectangle at their own resolutions.

## Camera-Height-Conditional Uniforms

Resolve camera-height-dependent values CPU-side and upload one float. Author controls use `HeightLerpWrapperQuartet`; hard-coded variants use `engine::LerpAtHeight`. Shared fade endpoints remain single-sourced on the game camera.

## Buffer & Dispatch Patterns

- Uniform layouts reinterpret persistent-mapped, per-command-buffer buffers under scalar layout. Never read back from these write-combined maps; compute dependent intermediates in a file-static staging copy and copy the populated region once. Split tunable-dependent cached work from per-frame phase work.
- Binary compute gates use indirect buffers; water displacement instead writes workgroup dimensions for its active LOD sub-region.
- Cross-file or externally-reset state lives as `inline` globals in `Render.h`; single-file edge detectors / latches stay function-local statics — do not promote. All latches assume the entry points run exactly once per frame.
- Shadow/lighting recreate resets force one pure-current frame with previous area equal to current, reseeding rebuilt history textures despite persistent function-local latches.
- Wind textures hard-clear once at creation/recreate; occupancy-active spreading and exact-zero decay then maintain valid contents without a reset flag.
- Smoke render population gates the indirect fullscreen clears used on enable/disable/recreate edges. `RenderTargetTextures` and `BufferManager` own creation-time texture and occupancy zeroing; command-buffer and shader paths own stale-tile drainage.
- The `giShadow*` / `giLighting*` pixel-count globals in `Render.h` exist solely as readouts for `Profile/ProfileScreens.cpp`.
- Smoke/wind spread is scale-aware: shaders reconstruct world position from the current world-area uniform and remap to the previous-frame texcoord, so no per-command-buffer spread-quad storage buffer is needed.
