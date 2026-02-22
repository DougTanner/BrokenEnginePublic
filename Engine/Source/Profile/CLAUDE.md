# `/Engine/Source/Profile/`

Performance profiling system providing CPU timing, GPU timing via Vulkan timestamp queries, and boot-time measurements. Uses a Base/Derived inheritance pattern for engine-game extension.

**Global**: `game::gpProfileManager` (always instantiated, points to game-derived ProfileManager)

## ProfileManagerBase

Abstract base class that collects and displays performance metrics with smoothed values for stable readouts. Contains engine-specific counters, timers, and all profiling infrastructure.

### Profiling Categories

**CPU Counters**: Track per-frame object counts (billboards, lights, particles, etc.) for monitoring active game entities. Stored as member arrays in ProfileManagerBase (engine) and ProfileManager (game).

**CPU Timers**: High-resolution timing for engine subsystems using `std::chrono::high_resolution_clock`. Values accumulate within frames and are smoothed for display. Supports multi-threaded timing with thread count tracking. Each timer also tracks per-timer heap allocation counts (delta of global `giAllocationsThisFrame` between start and stop), displayed in brackets in the profile overlay and log output.

**GPU Timers**: Vulkan timestamp queries measuring render pass execution time. Organized into four hierarchical groups:
- **Global**: Shadow rendering, terrain generation, particle system updates
- **Main**: Lighting passes, smoke emission, object shadow rendering
- **Image**: Final object rendering, transparent objects, terrain, water, hex shields, and particle rendering
- **Ui Render**: ImGui UI rendering (recorded in ImGuiManager's dedicated command buffer)

**Boot Timers**: One-time initialization measurements for Vulkan manager creation, texture loading, and command buffer recording. Automatically logs timers exceeding 10ms at startup.

**Allocation Profiling**: When `kbEnableAllocationTracking` is true, `UpdateProfileText()` reads and resets a global atomic counter (`giAllocationsThisFrame`, declared in `Memory/MemoryManager.h`) to display per-frame heap allocation counts in the profile overlay.

**Memory Profiling**: Displays data memory usage via FileManager APIs showing eager (startup-loaded pack files), lazy (on-demand loaded chunks), and total memory in megabytes with allocation counts in parentheses.

**String Building**: Profile text construction uses `common::gpThreadLocal->mWorkbuffer` (allocation-free `Workbuffer`) instead of `std::string` concatenation, eliminating per-frame heap allocations in the profiling display code. Each text section calls `Push()` to start building, passes `View()` to `UpdateTextArea()`, then calls `Pop()` to mark the workbuffer as available.

### Engine-Game Inheritance Architecture

Uses Base/Derived pattern where `engine::ProfileManagerBase` contains engine counters/timers as member arrays, and `game::ProfileManager` inherits from it adding game-specific counters/timers.

**Virtual Dispatch**: `GetCpuCounter()` and `GetCpuTimer()` are virtual methods overridden by game::ProfileManager to dispatch to the appropriate array (engine or game) based on index.

**Offset-Based Constants**: Game code defines `inline constexpr` offset constants (e.g., `kCpuTimerFrameUpdate = engine::kEngineCpuTimerCount + kGameCpuTimerFrameUpdate`) for direct use in profiling calls.

**Global Pointer**: `game::gpProfileManager` is the single global pointer used everywhere. Engine code forward-declares this pointer to use it in RAII classes.

### Vulkan Architecture

Uses `VkQueryPool` with per-command-buffer query sets. Validates timestamp support at creation and gracefully degrades if unavailable. Query pools are reset during command recording. Results are read without blocking to prevent hangs at low framerates; unavailable results are skipped and smoothed values from previous frames are retained.

GPU profiling integrates with Vulkan debug utils for render pass labeling in external profilers (RenderDoc, etc.).

### Usage

ProfileManager is always instantiated (game::gpProfileManager is never nullptr). All profiling methods use `if constexpr (kbEnableProfiling)` internally for compile-time elimination when profiling is disabled.

- **CPU Scoped**: `ScopedCpuProfile` RAII class for automatic scope-based timing
- **CPU Direct**: `gpProfileManager->CpuStart()`/`CpuStop()` for manual timing control
- **GPU**: `gpProfileManager->GpuStart()`/`GpuStop()` during command buffer recording
- **Boot**: `ScopedBootTimer` RAII class for initialization measurements
- **Counters**: `gpProfileManager->SetCount()` for object counts
- **Suppression**: `ScopedSuppressCpuProfiling` RAII class temporarily suppresses CPU profiling via thread-local counter, used to exclude timing noise from nested profiling calls
- Profile text overlay cycles through `ProfileScreen` modes (Off, Cpu, Gpu, Frames, Network) via `ToggleProfileText()`. Cpu and Gpu screens share an FPS line showing render fps, estimated CPU fps, estimated GPU fps, and frame update rates. Cpu screen shows CPU timers, counters, and memory stats. Gpu screen shows graphics info and GPU timers. Frames screen shows multi-frame grid visualization with active coordinate map. Network screen is a placeholder. Defaults to visible in profile builds

### Extension

Game projects create `ProfileManager` inheriting from `ProfileManagerBase`, defining game-specific counters and timers as member arrays and overriding `GetCpuCounter()`, `GetCpuTimer()`, `GetCpuCounterCount()`, and `GetCpuTimerCount()` to dispatch between engine and game arrays.
