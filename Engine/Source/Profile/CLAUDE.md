# `/Engine/Source/Profile/`

CPU/GPU performance profiling, boot-time measurement, and in-game overlay. Engine Base / game Derived pattern; global is `game::gpProfileManager`.

## Architecture

Base holds engine-level counter/timer arrays; game derived adds project-specific arrays. Virtual dispatch routes by index, with game enums encoding the engine offset so call sites need no arithmetic.

All entry points are wrapped in `if constexpr (kbProfiling)` for zero overhead when disabled.

GPU timing, the `VkQueryPool`, and the overlay renderer are client-only; CPU timers, counters, boot timers, and text formatting compile in both builds so the server can use them.

## GPU Queries

Single `VkQueryPool` with start/stop pairs per timer per in-flight command buffer. Reads are non-blocking — `VK_NOT_READY` skips silently so low framerates never stall; stale frames keep the last smoothed value. If the graphics queue reports `timestampValidBits == 0`, the pool stays null and every GPU method early-outs. Timed regions are wrapped in `vkCmdBegin/EndDebugUtilsLabelEXT` for RenderDoc.

Both start and stop timestamps use `VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT` so each endpoint waits for all preceding pipeline work to complete — required for accurate per-region timing when multiple regions share a single render pass.

## Thread Safety

CPU timer state is a mutex-protected map keyed by `std::thread::id` so dispatch workers can contribute. Timestamps are captured before acquiring the lock. Map resize wraps in `ScopedSuppressAllocationTracking` so profiling never pollutes its own counts. Cross-thread Start/Stop is supported via an explicit flag that scans the per-thread map.

Each CPU timer reports the heap allocations that occurred inside its scope by diffing `giAllocationsThisFrame`.

## Overlay

`ToggleProfileText()` cycles through fixed screens (off / CPU / GPU / Frames / Network); each transition clears all profile text slots to prevent stale content. FPS header and memory screens render in CPU/GPU modes only; Frames/Network are game-owned via a `FormatGameScreens` override. Client-only ImPlot graphs render alongside the text overlay.

Display hysteresis: CPU timers, CPU counters, and GPU timers hide themselves ~1s after dropping below threshold.

## Cross-Layer Dependency

`FormatFpsHeader` reads a game-specific CPU timer enum directly — the engine base depends on the game enum defining the first game-specific CPU timer slot by convention.

## Extension

Game projects inherit from `ProfileManagerBase` to add counters, timers, and overlay screens. See [game ProfileManager](../../../Projects/BrokenEngineSandbox/Source/Profile/CLAUDE.md).
