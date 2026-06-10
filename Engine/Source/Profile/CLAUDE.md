# `/Engine/Source/Profile/`

CPU/GPU performance profiling, boot-time measurement, and in-game overlay. Engine Base / game Derived pattern; global is `game::gpProfileManager`.

## Architecture

Base holds engine-level counter/timer arrays; game derived adds project-specific arrays. Virtual dispatch routes by index, with game enums encoding the engine offset so call sites need no arithmetic.

All entry points are wrapped in `if constexpr (kbProfiling)` for zero overhead when disabled.

GPU timing, the `VkQueryPool`, and the overlay renderer are client-only; CPU timers, counters, boot timers, and the CPU text-formatting free functions compile in both builds — the server's GDI display reuses them, and must call `SmoothCpuTimers()` itself since the client-only overlay update (which calls it on the client) never runs there. The base also carries mimalloc memory stats written and read only by that server display.

## GPU Queries

Single `VkQueryPool` with start/stop pairs per timer per in-flight command buffer. Reads are non-blocking — `VK_NOT_READY` skips silently so low framerates never stall; stale frames keep the last smoothed value. If the graphics queue reports `timestampValidBits == 0`, the pool stays null and every GPU method early-outs. Timed regions are wrapped in `vkCmdBegin/EndDebugUtilsLabelEXT` for RenderDoc.

Both start and stop timestamps use `VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT` so each endpoint waits for all preceding pipeline work to complete — required for accurate per-region timing when multiple regions share a single render pass.

## Thread Safety

CPU timer state is a mutex-protected map keyed by `std::thread::id` so dispatch workers can contribute. Timestamps are captured before acquiring the lock. Map resize wraps in `ScopedSuppressAllocationTracking` so profiling never pollutes its own counts. Cross-thread Start/Stop is supported via an explicit flag that scans the per-thread map.

Each CPU timer reports the heap allocations that occurred inside its scope by diffing `giAllocationsThisFrame`.

CPU timers latch into their smoothing rings once per render frame via `SmoothCpuTimers()`; timers whose scope completes out of phase with the render frame (cross-thread acquire, network ops, server full ticks) instead pass `bSmoothNow` to latch at stop.

## Overlay

`ToggleProfileText()` cycles through fixed screens (off / CPU / GPU / Frames / Network); each transition clears all profile text slots to prevent stale content. The FPS header renders in CPU and GPU modes; the CPU screen adds asset-chunk memory stats, the GPU screen adds per-pass dynamic-resolution annotations (shadow window, lighting spread, terrain elevation, water LOD grid) and VMA memory stats. Frames/Network are game-owned via a `FormatGameScreens` override. Client-only ImPlot graphs render alongside the text overlay.

Display visibility is synchronized and sticky: `TickVisibilityCadence()` is the shared driver, re-evaluating every row's cached `bVisible` together only on a ~2s boundary, so all rows (CPU timers, CPU counters, GPU timers) flip at once and every show/hide lasts at least ~2s. Displayed numbers stay live each frame; `ToggleProfileText()` resets the clock so a switched-to screen re-evaluates immediately.

## Cross-Layer Dependency

The FPS header reads a game-specific CPU timer enum by name to report total frame time. The contiguous index space (game enums starting at `kEngineCpuCounterCount` / `kEngineCpuTimerCount`) and the required position of that timer are documented game-side.

The overlay also reads `game::gpCamera` directly (include of `Game.h`) — camera height in the FPS header, visible-area LOD for the GPU screen's water annotation. Sanctioned engine→game reads; noted here only so the game couplings are discoverable.

## Extension

Game projects inherit from `ProfileManagerBase` to add counters, timers, and overlay screens. See [game ProfileManager](../../../Projects/BrokenEngineSandbox/Source/Profile/CLAUDE.md).
