# `/Engine/Source/Profile/`

Performance profiling system providing CPU timing, GPU timing via Vulkan timestamp queries, boot-time measurements, and an in-game overlay. Uses Base/Derived inheritance for engine-game extension.

**Global**: `game::gpProfileManager` (always instantiated, points to game-derived ProfileManager)

## Architecture

**Base/Derived Pattern**: `engine::ProfileManagerBase` owns engine-level counters and timers as member arrays. `game::ProfileManager` inherits from it, adding game-specific arrays. Virtual dispatch on `GetCpuCounter()`/`GetCpuTimer()` routes to the correct array based on index. Game enums encode the engine offset directly so profiling calls need no additional calculation.

**Compile-Time Elimination**: All profiling methods guard with `if constexpr (kbEnableProfiling)` for zero overhead when disabled.

**GPU Queries** (client-only): Uses a single `VkQueryPool` with per-command-buffer query sets. Results are read without blocking to prevent hangs at low framerates; unavailable results retain previous smoothed values. Integrates with Vulkan debug utils for external profiler labeling (RenderDoc, etc.).

**Client/Server Split**: GPU methods, `VkQueryPool`, and the profile text overlay are gated behind `BT_CLIENT`. CPU timers, counters, and boot timers compile in both builds.

## Thread Safety

CPU profiling is thread-safe via per-thread timer state stored in a mutex-protected map keyed by `std::thread::id`. Timestamps are captured before acquiring the lock. This allows worker threads and the reconcile thread to contribute profiling data.

## Usage

- **`ScopedCpuProfile`**: RAII scope-based CPU timing; also tracks per-timer heap allocation counts
- **`ScopedBootTimer`**: RAII one-time initialization measurement
- **Direct API**: `CpuStart()`/`CpuStop()`, `GpuStart()`/`GpuStop()`, `SetCount()`
- **Overlay**: Cycles through `ProfileScreen` modes (Off, Cpu, Gpu, Frames, Network) via `ToggleProfileText()`. CPU screen shows timers, counters, memory stats, and allocations. GPU screen shows graphics info and GPU timers. Frames screen shows active grid visualization. Network screen shows transport stats, reconciliation state, clock correction, and tick classification counters. All overlay text is built using the workbuffer to avoid per-frame heap allocations.

## Extension

Game projects inherit from `ProfileManagerBase`, adding game-specific counter and timer arrays. See [game ProfileManager](../../../Projects/BrokenEngineSandbox/Source/Profile/CLAUDE.md).
