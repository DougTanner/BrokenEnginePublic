# `/Engine/Source/Profile/`

Performance profiling system providing CPU timing, GPU timing via Vulkan timestamp queries, and boot-time measurements.

**Global**: `gpProfileManager`
**Conditional**: Only compiled with `ENABLE_PROFILING` define

## ProfileManager

Singleton manager that collects and displays performance metrics with smoothed values for stable readouts.

### Profiling Categories

**CPU Counters**: Track per-frame object counts (billboards, lights, particles, etc.) for monitoring active game entities.

**CPU Timers**: High-resolution timing for engine subsystems using `std::chrono::high_resolution_clock`. Values accumulate within frames and are smoothed for display. Supports multi-threaded timing with thread count tracking.

**GPU Timers**: Vulkan timestamp queries measuring render pass execution time. Organized into three hierarchical groups:
- **Global**: Shadow rendering, terrain generation, particle system updates
- **Main**: Lighting passes, smoke emission, object shadow rendering
- **Image**: Final object rendering, terrain, water, UI, and particle rendering

**Boot Timers**: One-time initialization measurements for Vulkan manager creation, texture loading, and command buffer recording. Automatically logs timers exceeding 10ms at startup.

**Memory Profiling**: Displays data memory usage via FileManager APIs showing eager (startup-loaded pack files), lazy (on-demand loaded chunks), and total memory in megabytes with allocation counts in parentheses. Includes per-data-type breakdowns showing individual categories (Font, Gltf, Model, Shader under Eager; Audio, Islands, Texture under Lazy) with their respective memory and allocation counts.

### Engine-Game Profile Extension Architecture

Uses explicit enums with lookup functions (not X-macros) for IntelliSense compatibility.

**Counter Layout**: Engine counters (Part 1) followed by game counters. Single `GetCpuCounter()` dispatches to the appropriate array based on index.

**Timer Layout**: Engine timers Part 1 (acquire, render, audio, input) → Game timers → Engine timers Part 2 (wait fence, render main, present). The `GetCpuTimer()` function dispatches across three arrays based on index ranges.

**Offset Constants**: The `game::` namespace provides convenience constants that add the engine offset to game-local indices, allowing game code to use constants like `game::kCpuTimerFrameUpdate` directly.

**Include Order**: ProfileManager.h defines struct types first, then includes GameProfile.h, then defines combined counts and lookup functions. This allows game code to use engine struct types without circular dependencies.

### Vulkan Architecture

Uses `VkQueryPool` with per-command-buffer query sets. Validates timestamp support at creation and gracefully degrades if unavailable. Query pools are reset during command recording, and results are read with GPU synchronization via `VK_QUERY_RESULT_WAIT_BIT`.

GPU profiling integrates with Vulkan debug utils for render pass labeling in external profilers (RenderDoc, etc.).

### Usage

- CPU: `SCOPED_CPU_PROFILE` macro for automatic scope-based timing
- GPU: Manual timestamp insertion via `GpuStart`/`GpuStop` during command buffer recording
- Profile text overlay toggleable at runtime, defaults to visible in profile builds

### Extension

Game projects define counters and timers in `GameProfile.h` and `GameProfile.cpp` within the `game::profile` namespace. The header declares enums (starting from 0) and accessor function prototypes; the cpp file defines the arrays and implements the accessor functions.
