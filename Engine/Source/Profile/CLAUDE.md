# `/Engine/Source/Profile/`

Performance profiling system that tracks CPU/GPU timing and boot metrics.

**Global**: `gpProfileManager`
**Conditional**: Only available with `ENABLE_PROFILING` define

## ProfileManager

Singleton manager providing CPU, GPU, and boot timing measurements with smoothed display values.

### CPU Profiling

- **Counters**: Integer metrics tracking active object counts per frame (billboards, lights, smoke, etc.)
- **Timers**: High-resolution timing for engine subsystems, accumulated within frames and smoothed for display

### GPU Profiling

Vulkan timestamp queries measuring GPU execution time for render passes. Organized hierarchically:
- **Global**: shadows, terrain generation, particle updates
- **Main**: lighting, smoke, object shadows
- **Image**: final rendering passes

Uses VkQueryPool with per-command-buffer query sets. Validates timestamp support and gracefully degrades if unavailable. Query pools reset during command recording, results read with GPU synchronization.

### Boot Profiling

One-time initialization measurements (Vulkan managers, texture loading, command buffer recording). Automatically logs timers exceeding 10ms at startup.

### Usage

- CPU: `SCOPED_CPU_PROFILE` macro for automatic scope-based timing
- GPU: Manual timestamp insertion during command buffer recording
- Profile text defaults to visible in profile builds

### Extension

Game projects define additional counters and timers in `GameProfile.h` via `CPU_COUNTERS_GAME_ENUM` and `CPU_TIMERS_GAME_ENUM` macros.
