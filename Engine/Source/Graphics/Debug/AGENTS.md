# Debug - Debug Visualization Utilities

## Overview

Wireframe primitive rendering (boxes, spheres, circles, lines) for development visualization. Whole file is `BT_CLIENT`-only and every body is gated by the game-layer `kbDebugRender` constexpr (true only in the debug build config) so calls compile to no-ops elsewhere; additionally gated at runtime by a file-scope flag (off by default) toggled via `Toggle()` from the game-layer hotkey, so submission helpers early-out when the user has not enabled visualization.

## Architecture Notes

- The full primitive set (box/sphere/circle/line) is kept regardless of in-tree callers: debug submissions are added ad-hoc while testing and removed afterward, so a primitive with zero current call sites (e.g. `Box`/`Sphere`) is intentional toolkit completeness, not dead code — do not file dead-code plans against it or its pipelines/meshes/buffers.
- Entirely static — no instance, no `gp*` global. Submissions queue per-instance layouts into file-scope per-type staging structs (one each for box/sphere/circle/line), each owning a `std::vector` of shader layouts plus a live count and CRC key. Each vector starts empty, is resized to a large initial element count on first use (so nothing allocates until visualization is enabled), then silently doubles on overflow — no hard cap, no assert. Both grow paths wrap `ScopedSuppressAllocationTracking`; the growth path also logs.
- Not thread-safe: statics mutated without synchronization. Submit only from the render/main thread.
- Primitives drawn via indirect pipeline calls with pre-built unit meshes (built in `BufferManager`, recorded in main command-buffer recording, pipelines created by `PipelineManager`); game layer submits game-specific primitives (targeting, nav indicators), engine submits engine-level overlays from `Render/` during main-pass uniform population.

## Per-Frame Flush

Submission helpers may be called any time during the frame; `BeginRender`/`EndRender` then run back-to-back in `RenderFrameMain` after all debug submissions (further MainLayout population follows them) — the host-visible flush that keeps debug draws legal under the parent's CB re-record ban, not a bracket around recording. (`RenderFrameMain`'s all-rings-empty skip path also runs the pair back-to-back — with no submissions, so it writes zero counts — then returns before any further population.)
- `BeginRender` skips types with zero submissions; otherwise grows the CRC-keyed dynamic storage buffer if needed and copies queued layouts in. The pipeline's storage-buffer descriptor is rebound only on the resize path (legal via update-after-bind), so steady-state frames touch only mapped memory.
- `EndRender` writes the indirect instance count for all four types unconditionally — the zero write is load-bearing, clearing the prior frame's count in the record-once command buffer — then resets the counters. Counters reset here only, never in `BeginRender`.

## Transform Encoding

Row-major 3x4 packed into the per-instance layout. Box/Sphere/Circle use scale (or radius) on the diagonal with translation in column w. Line collapses the unit `(0,0,0)->(1,0,0)` mesh to the requested endpoints by carrying only the delta component per row with start position in column w. Circle is billboarded GPU-side — its vertex shader discards the orientation and rebuilds a camera-facing basis, so the encoding does not pin it to the XY plane (see shader doc below).

## See Also

- [../Managers/AGENTS.md](../Managers/AGENTS.md) - BufferManager and PipelineManager
- [../../../Data/Shaders/Debug/AGENTS.md](../../../Data/Shaders/Debug/AGENTS.md) - DebugRender shaders
