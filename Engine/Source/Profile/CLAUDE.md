# `/Engine/Source/Profile/`

Performance profiling system that tracks CPU/GPU timing and boot metrics.

**Global**: `gpProfileManager`
**Conditional**: Only available with `ENABLE_PROFILING` define

## ProfileManager

Singleton manager providing CPU, GPU, and boot timing measurements with smoothed display values.

### CPU Profiling

**Counters**: Integer metrics tracking active object counts per frame (billboards, lights, smoke puffs, controllers, etc.). Game-specific counters added via `CPU_COUNTERS_GAME_ENUM` in GameProfile.h.

**Timers**: High-resolution timing for engine subsystems (rendering, input, audio, Vulkan synchronization operations). Values are accumulated across multiple operations within a frame and smoothed for display.

### GPU Profiling

**Timers**: Vulkan timestamp queries measuring GPU execution time for render passes. Organized hierarchically as Global (shadows, terrain generation, particle updates) → Main (lighting, smoke, object shadows) → Image (final rendering passes).

**Architecture**: Uses VkQueryPool with per-command-buffer query sets. Validates timestamp support on initialization and gracefully degrades if unavailable. Query pools are reset via command buffers at recording time and read with `VK_QUERY_RESULT_WAIT_BIT` to synchronize with GPU completion.

### Boot Profiling

**Timers**: One-time measurements tracking initialization performance (Vulkan manager creation, texture loading, command buffer recording). Automatically logs any timer exceeding 10ms at startup.

### Key Methods

- `CpuStart/Stop()`: Bracket CPU timing sections
- `GpuStart/Stop()`: Insert GPU timestamp queries into command buffers
- `GpuRead()`: Retrieve and smooth GPU timing results
- `ResetGlobalQueryPools/ResetImageQueryPools()`: Reset query pools during command recording
- `UpdateProfileText()`: Format and display performance overlay
- `ToggleProfileText()`: Show/hide profiling display

### Usage Pattern

CPU timers use `SCOPED_CPU_PROFILE` macro for automatic scope-based timing. GPU timers are manually inserted into command buffer recording. Profile text visibility defaults to on in profile builds, off otherwise.

Game projects extend profiling via GameProfile.h, which defines additional CPU counters and timers specific to game systems.
