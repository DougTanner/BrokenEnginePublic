# `/Engine/Source/Profile/`

Performance profiling system providing CPU timing, GPU timing via Vulkan timestamp queries, boot-time measurements, and an in-game overlay. Uses Base/Derived inheritance for engine-game extension.

**Global**: `game::gpProfileManager` (always instantiated, points to game-derived ProfileManager)

## Architecture

**Base/Derived Pattern**: `engine::ProfileManagerBase` owns engine-level counters and timers as member arrays. `game::ProfileManager` inherits from it, adding game-specific arrays. Virtual dispatch on `GetCpuCounter()`/`GetCpuTimer()` routes to the correct array based on index. Game enums encode the engine offset directly so profiling calls need no additional calculation.

**Compile-Time Elimination**: All profiling methods guard with `if constexpr (kbProfiling)` for zero overhead when disabled.

**GPU Queries** (client-only): Uses a single `VkQueryPool` with per-command-buffer query sets. Results are read without blocking to prevent hangs at low framerates; unavailable results retain previous smoothed values. Integrates with Vulkan debug utils for external profiler labeling (RenderDoc, etc.).

**Client/Server Split**: GPU methods, `VkQueryPool`, and the client profile overlay are gated behind `BT_CLIENT`. CPU timers, counters, boot timers, and CPU text formatting functions compile in both builds.

## Thread Safety

CPU profiling is thread-safe via per-thread timer state stored in a mutex-protected map keyed by `std::thread::id`. Timestamps are captured before acquiring the lock. This allows worker threads and the reconcile thread to contribute profiling data.

## Usage

- **`ScopedCpuProfile`**: RAII scope-based CPU timing; also tracks per-timer heap allocation counts
- **`ScopedBootTimer`**: RAII one-time initialization measurement
- **Direct API**: `CpuStart()`/`CpuStop()`, `GpuStart()`/`GpuStop()`, `SetCount()`, `SmoothCpuTimers()` (updates smoothed CPU timer values; available in both builds so the server can call it independently of the client overlay)
- **Overlay**: Cycles through `ProfileScreen` modes (Off, Cpu, Gpu, Frames, Network) via `ToggleProfileText()`. CPU screen shows timers, counters, memory stats, and allocations. GPU screen shows graphics info, GPU timers, and VMA GPU memory statistics (allocated/used/unused bytes, allocation and block counts, and per-heap budget and usage via `vmaGetHeapBudgets` when `mbMemoryBudgetAvailable`). Frames screen shows active grid visualization. Network screen is organized into sections (Transport, Sync, Prediction, Clock, Reconciliation) and displays packet loss percent, interarrival jitter, and the active simulation level. When simulation is enabled, stats exceeding their `NetworkSimulationBounds` thresholds are flagged with `!`. All overlay text is built using the workbuffer to avoid per-frame heap allocations. The virtual `RenderImPlotGraphs()` hook (client-only) is called by `ImGuiManager::Submit()` after screen text; game-derived classes override it to render ImPlot time-series graphs alongside the text overlay.

## File Organization

Screen formatting logic is split into `ProfileScreens.cpp` to keep `ProfileManagerBase.cpp` under 500 lines. CPU timer and counter text formatting functions in `ProfileScreens.cpp` are shared (not client-gated) so the server can reuse them; the client-only `FormatCpuScreen()` calls these shared helpers. Game-specific screens (Frames, Network) are implemented by overriding `FormatGameScreens()` in the derived class.

## Extension

Game projects inherit from `ProfileManagerBase`, adding game-specific counter and timer arrays and overriding `FormatGameScreens()` to add game-specific overlay screens. See [game ProfileManager](../../../Projects/BrokenEngineSandbox/Source/Profile/CLAUDE.md).
