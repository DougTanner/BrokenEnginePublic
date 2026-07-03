# `/Engine/Source/Profile/`

CPU/GPU performance profiling, boot-time measurement, and in-game overlay. Engine Base / game Derived pattern; global is `game::gpProfileManager`.

## Architecture

Base holds engine-level counter/timer arrays; game derived adds project-specific arrays. Virtual dispatch routes by index, with game enums encoding the engine offset so call sites need no arithmetic.

All recording and update entry points are wrapped in `if constexpr (kbProfiling)` for zero overhead when disabled. The visibility-cadence tick (`TickVisibilityCadence`), the CPU text formatters (`FormatCpuTimersText`/`FormatCpuCountersText`), and the virtual timer/counter accessors are deliberately left unwrapped so the server's GDI display still renders them (showing zeros when profiling is off).

GPU timing, the `VkQueryPool`, and the overlay renderer are client-only; CPU timers, counters, boot timers, and the CPU text-formatting free functions compile in both builds — the server's GDI display reuses them by calling `UpdateProfileText()`, which runs the shared prefix (`SmoothCpuTimers()` + the per-frame allocation latch/reset) and skips its `BT_CLIENT`-gated GPU-timer and overlay-formatting work. The base also carries mimalloc memory stats (gated `BT_SERVER`) written and read only by that server display.

## GPU Queries

Single `VkQueryPool` with start/stop pairs per timer per in-flight command buffer. Reads are non-blocking — `VK_NOT_READY` skips silently so low framerates never stall; stale frames keep the last smoothed value. If the graphics queue reports `timestampValidBits == 0`, the pool stays null and every GPU method early-outs. Timed regions are wrapped in `vkCmdBegin/EndDebugUtilsLabelEXT` for RenderDoc.

Both start and stop timestamps use `VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT` so each endpoint waits for all preceding pipeline work to complete — required for accurate per-region timing when multiple regions share a single render pass.

Each command buffer resets its own contiguous enum span via `ResetQueryPools` (Global CB / Main CB / ImGui CB split the `GpuTimers` enum into three ranges), so a new GPU timer must be inserted into the span of the command buffer that records it — placed elsewhere, its queries are reset by the wrong buffer. The name table and enum share ordering; keep the indentation-hierarchy convention when inserting.

## Thread Safety

CPU timer state is a mutex-protected map keyed by `std::thread::id` so dispatch workers can contribute. Timestamps and the allocation-counter snapshot are captured before acquiring the lock. The CPU-timer-reading text formatters take the same lock for their reads, since dispatch, submit, and network threads write timer fields concurrently. Map resize wraps in `ScopedSuppressAllocationTracking` so profiling never trips the allocation tripwire. Cross-thread Start/Stop is supported via an explicit flag that scans the per-thread map.

Each CPU timer reports the heap allocations that occurred during its scope's wall-clock window by diffing `giAllocationsThisFrame` — a single process-wide atomic, so the count includes every thread's allocations during the window, not just the timer's own thread.

CPU timers latch into their smoothing rings once per render frame via `SmoothCpuTimers()`; timers whose scope completes out of phase with the render frame (cross-thread acquire, network ops, server full ticks) instead request a latch-at-stop via a `CpuStop` flag.

## Overlay

`ToggleProfileText()` cycles through fixed screens (off / CPU / GPU / Frames / Network); each transition clears all profile text slots to prevent stale content. The FPS header renders in CPU and GPU modes; the CPU screen adds asset-chunk memory stats, the GPU screen adds per-pass dynamic-resolution annotations (shadow window, lighting spread, terrain elevation, water LOD grid) and VMA memory stats. Frames/Network are game-owned via a `FormatGameScreens` override. Client-only ImPlot graphs render alongside the text overlay.

Display visibility is synchronized and sticky: `TickVisibilityCadence()` is the shared driver, re-evaluating every row's cached visibility flag (`ProfileRowFlags::kVisible`) together only on a ~2s boundary, so all rows (CPU timers, CPU counters, GPU timers) flip at once and every show/hide lasts at least ~2s. Displayed numbers stay live each frame; `ToggleProfileText()` resets the clock so a switched-to screen re-evaluates immediately.

## CSV Dump

When the game defines `kbProfilingDump` true (client-only), every GPU timer, CPU timer, and counter — plus meta rows (Fps, FullUpdates, InterpolateUpdates) — is sampled once per second into a long-format CSV at `%TEMP%\<game::kGameName>\ProfileDump.csv` for offline analysis, using `common::DiagnosticLog` slot 3. The file is truncated fresh per process run and must span the whole session, so the log handle is deliberately not reset in `Destroy()` — which also runs on swapchain-tier recreates — and closes only when the manager itself is destroyed. It is created before the GPU-timestamp-support early-outs (CPU rows still dump on devices without timestamps), and sampling runs before the overlay-off early-out, so dumping never requires the overlay to be visible.

## Cross-Layer Dependency

The FPS header reads a game-specific CPU timer enum by name to report total frame time. The contiguous index space (game enums starting at `kEngineCpuCounterCount` / `kEngineCpuTimerCount`) is documented game-side; the position contract (first game enumerator == engine count) is compile-enforced engine-side by `static_assert` so an omitted game-enum initializer fails the build instead of misrouting game indices into the engine arrays.

Display names live in deduced-extent namespace name tables (one per counter/timer enum), each guarded by a `static_assert` that its extent equals the enum count — a dropped or extra name is a compile error instead of a silently misaligned overlay row. The structs carry only runtime state; names are read through virtual accessors that route engine/game indices the same way the counter/timer accessors do (GPU/boot names read their engine tables directly). The game project mirrors the convention with its own guarded tables.

The overlay also reads `game::gpCamera` directly (via the game `Graphics/Camera.h` include) — camera height in the FPS header, visible-area LOD for the GPU screen's water annotation. Sanctioned engine→game reads; noted here only so the game couplings are discoverable.

## Extension

Game projects inherit from `ProfileManagerBase` to add counters, timers, and overlay screens. See [game ProfileManager](../../../Projects/BrokenEngineSandbox/Source/Profile/CLAUDE.md).
