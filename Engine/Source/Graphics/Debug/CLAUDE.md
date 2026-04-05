# Debug - Debug Visualization Utilities

## Overview

Client-only debug rendering utilities for visualizing world-space primitives during development. Entirely gated by a `constexpr bool kbDebugRender` (`BT_DEBUG` only) so all calls compile to no-ops in non-debug builds.

## Key Classes

- **DebugRender** - Stateless facade for submitting wireframe primitives (boxes, spheres, circles, lines) each frame. Queues per-instance transform and color data into static per-type storage, then uploads to GPU via `BufferManager` dynamic storage buffers and draws via indirect pipeline calls. `BeginRender` uploads instance data; `EndRender` issues indirect draws and resets counts.

## Architecture Notes

- Four primitive types (box, sphere, circle, line) each map to a dedicated pipeline and pre-built unit mesh. Instance transforms are compact 3x4 row-major matrices with translation in the W column.
- Relies on `BufferManager::ResizeDynamicBufferIfNeeded` for auto-growing storage buffers and `PipelineManager` indirect draw pipelines (using `kLineList` topology).
- Call sites submit primitives any time during the frame; `BeginRender`/`EndRender` bracket the main render pass draw call.

## See Also

- [../Managers/CLAUDE.md](../Managers/CLAUDE.md) - BufferManager and PipelineManager used for GPU resources
- [../../../../Engine/Data/Shaders/Debug/](../../../../Engine/Data/Shaders/Debug/) - DebugRender vertex/fragment shaders
