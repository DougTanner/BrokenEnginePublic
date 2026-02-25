# `/Engine/Source/Profile/`

Performance profiling system providing CPU timing, GPU timing via Vulkan timestamp queries, boot-time measurements, and an in-game overlay. Uses Base/Derived inheritance for engine-game extension.

**Global**: `game::gpProfileManager` (always instantiated, points to game-derived ProfileManager)

## Overview

`ProfileManagerBase` collects and displays performance metrics across four categories: CPU counters (per-frame object counts), CPU timers (high-resolution subsystem timing with allocation tracking), GPU timers (Vulkan timestamp queries across render pass groups), and boot timers (one-time initialization measurements logged at startup). All values are smoothed for stable readouts. Profile text is built using the workbuffer to avoid per-frame heap allocations.

## Architecture

**Base/Derived Pattern**: `engine::ProfileManagerBase` owns engine-level counters and timers as member arrays. `game::ProfileManager` inherits from it, adding game-specific arrays. Virtual dispatch on `GetCpuCounter()`/`GetCpuTimer()` routes to the correct array based on index. Game enums encode the engine offset directly so profiling calls need no additional calculation.

**Compile-Time Elimination**: All profiling methods guard with `if constexpr (kbEnableProfiling)` for zero overhead when disabled.

**GPU Query Architecture**: Client-only (`#ifdef BT_CLIENT`). Uses a single `VkQueryPool` with per-command-buffer query sets. The GPU methods (`ResetGlobalQueryPools`, `ResetMainQueryPools`, `ResetUiQueryPool`, `GpuStart`, `GpuStop`, `GpuRead`), the `VkQueryPool` member, and the profile text overlay (`UpdateProfileText`) are all gated behind `BT_CLIENT`. CPU timers, counters, and boot timers compile in both builds. Validates timestamp support at creation and degrades gracefully. Results are read without blocking to prevent hangs at low framerates; unavailable results retain previous smoothed values. Integrates with Vulkan debug utils for external profiler labeling (RenderDoc, etc.).

## Usage

- **`ScopedCpuProfile`**: RAII class for automatic scope-based CPU timing. Each timer also tracks per-timer heap allocation counts, displayed in brackets in the overlay
- **`ScopedBootTimer`**: RAII class for one-time initialization measurements
- **`ScopedSuppressCpuProfiling`**: RAII class that suppresses CPU profiling via thread-local counter, used to exclude timing noise from nested profiling calls
- **Direct API**: `CpuStart()`/`CpuStop()`, `GpuStart()`/`GpuStop()`, `SetCount()` for manual control
- **Overlay**: Cycles through `ProfileScreen` modes (Off, Cpu, Gpu, Frames, Network) via `ToggleProfileText()`. CPU screen shows timers, counters, memory stats, and allocation counts. GPU screen shows graphics info and GPU timers. Frames screen shows multi-frame grid visualization

## Extension

Game projects inherit from `ProfileManagerBase`, adding game-specific counter and timer arrays. See [game ProfileManager](../../../Projects/BrokenEngineSandbox/Source/Profile/CLAUDE.md) for details.
