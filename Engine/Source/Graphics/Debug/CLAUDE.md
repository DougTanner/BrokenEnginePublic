# Debug - Debug Visualization Utilities

## Overview

Wireframe primitive rendering (boxes, spheres, circles, lines) for development visualization. Whole file is `BT_CLIENT`-only and every body is gated by the game-layer `kbDebugRender` constexpr (true only in the debug build config) so calls compile to no-ops elsewhere; additionally gated at runtime by a file-scope flag (off by default) toggled via `Toggle()` from the game-layer hotkey, so submission helpers early-out when the user has not enabled visualization.

## Architecture Notes

- Stateless facade queues per-instance layouts into a single file-scope array of per-type staging structs (one each for box/sphere/circle/line), each owning a `std::vector` of shader layouts plus a live count and CRC key. Each vector starts empty, is pre-allocated to a large fixed reserve on first use (so nothing allocates until visualization is enabled), then silently doubles on overflow — no hard cap, no assert. Both grow paths wrap `ScopedSuppressAllocationTracking`; the growth path also logs.
- Not thread-safe: statics mutated without synchronization. Submit only from the render/main thread.
- Primitives drawn via indirect pipeline calls with pre-built unit meshes (built in `BufferManager`, recorded in main command-buffer recording, pipelines created by `PipelineManager`); game layer submits game-specific primitives (targeting, nav indicators), engine submits engine-level overlays from `Render/` during main-pass uniform population.

## Frame-Phase Contract

Submission helpers may be called any time during the frame. Engine bracket calls wrap the main render pass draw:
- `BeginRender` grows/updates the dynamic storage buffer (keyed by CRC per primitive type) and uploads queued layouts.
- `EndRender` writes the indirect draw count AND resets per-type counters. Counters reset here, not in `BeginRender`.

## Transform Encoding

Row-major 3x4 packed into the per-instance layout. Box/Sphere/Circle use scale (or radius) on the diagonal with translation in column w. Line collapses the unit `(0,0,0)->(1,0,0)` mesh to the requested endpoints by carrying only the delta component per row with start position in column w.

## See Also

- [../Managers/CLAUDE.md](../Managers/CLAUDE.md) - BufferManager and PipelineManager
- [../../../Data/Shaders/Debug/CLAUDE.md](../../../Data/Shaders/Debug/CLAUDE.md) - DebugRender shaders
