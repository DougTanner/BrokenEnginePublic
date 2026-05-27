# Debug - Debug Visualization Utilities

## Overview

Wireframe primitive rendering (boxes, spheres, circles, lines) for development visualization. Gated by a debug-render constexpr (`BT_DEBUG` only) so calls compile to no-ops in non-debug builds; additionally gated at runtime by a file-scope flag (off by default) toggled from the game-layer hotkey, so submission helpers early-out when the user has not enabled visualization.

## Architecture Notes

- Stateless facade queues per-instance data into per-primitive-type file-scope `std::vector` staging. Each vector starts empty and is lazily pre-allocated on first use (so nothing allocates until visualization is enabled), then silently doubles on overflow — no hard cap, no assert. Both grow paths wrap `ScopedSuppressAllocationTracking`.
- Not thread-safe: statics mutated without synchronization. Submit only from the render/main thread.
- Primitives drawn via indirect pipeline calls with pre-built unit meshes; game layer submits game-specific primitives (targeting, nav indicators), engine submits engine-level overlays from `Render/` during main-pass uniform population.

## Frame-Phase Contract

Submission helpers may be called any time during the frame. Engine bracket calls wrap the main render pass draw:
- `BeginRender` grows/updates the dynamic storage buffer (keyed by CRC per primitive type) and uploads queued layouts.
- `EndRender` writes the indirect draw count AND resets per-type counters. Counters reset here, not in `BeginRender`.

## Transform Encoding

Row-major 3x4 packed into the per-instance layout. Box/Sphere/Circle use scale (or radius) on the diagonal with translation in column w. Line collapses the unit `(0,0,0)->(1,0,0)` mesh to the requested endpoints by carrying only the delta component per row with start position in column w.

## See Also

- [../Managers/CLAUDE.md](../Managers/CLAUDE.md) - BufferManager and PipelineManager
- `Engine/Data/Shaders/Debug/` - DebugRender shaders
