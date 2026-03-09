# `/Engine/Source/Profile/`

Performance profiling system providing CPU timing, GPU timing via Vulkan timestamp queries, boot-time measurements, and an in-game overlay. Uses Base/Derived inheritance for engine-game extension.

**Global**: `game::gpProfileManager` (always instantiated, points to game-derived ProfileManager)

## Overview

`ProfileManagerBase` collects and displays performance metrics across four categories: CPU counters (per-frame object counts), CPU timers (high-resolution subsystem timing with allocation tracking), GPU timers (Vulkan timestamp queries across render pass groups), and boot timers (one-time initialization measurements logged at startup). All values are smoothed for stable readouts. Profile text is built using the workbuffer to avoid per-frame heap allocations.

## Architecture

**Base/Derived Pattern**: `engine::ProfileManagerBase` owns engine-level counters and timers as member arrays. `game::ProfileManager` inherits from it, adding game-specific arrays. Virtual dispatch on `GetCpuCounter()`/`GetCpuTimer()` routes to the correct array based on index. Game enums encode the engine offset directly so profiling calls need no additional calculation.

**Compile-Time Elimination**: All profiling methods guard with `if constexpr (kbEnableProfiling)` for zero overhead when disabled.

**GPU Query Architecture**: Client-only (`#ifdef BT_CLIENT`). Uses a single `VkQueryPool` with per-command-buffer query sets. The GPU methods (`ResetGlobalQueryPools`, `ResetMainQueryPools`, `ResetUiQueryPool`, `GpuStart`, `GpuStop`, `GpuRead`), the `VkQueryPool` member, and the profile text overlay (`UpdateProfileText`) are all gated behind `BT_CLIENT`. CPU timers, counters, and boot timers compile in both builds. Validates timestamp support at creation and degrades gracefully. Results are read without blocking to prevent hangs at low framerates; unavailable results retain previous smoothed values. Integrates with Vulkan debug utils for external profiler labeling (RenderDoc, etc.).

## Thread Safety

CPU profiling is thread-safe via per-thread timer state. `CpuTimerThreadState` (start timestamp and allocation count) is stored in a mutex-protected `unordered_map` keyed by `std::thread::id`. `CpuStart`/`CpuStop` capture timestamps before acquiring the lock, then access per-thread state under the lock. `SetCount` asserts main-thread-only. `UpdateProfileText` and `LogTimers` lock the mutex during timer reads. This allows worker threads and the reconcile thread to properly contribute profiling data.

## Usage

- **`ScopedCpuProfile`**: RAII class for automatic scope-based CPU timing. Each timer also tracks per-timer heap allocation counts, displayed in brackets in the overlay
- **`ScopedBootTimer`**: RAII class for one-time initialization measurements
- **Direct API**: `CpuStart()`/`CpuStop()`, `GpuStart()`/`GpuStop()`, `SetCount()` for manual control
- **Overlay**: Cycles through `ProfileScreen` modes (Off, Cpu, Gpu, Frames, Network) via `ToggleProfileText()`. CPU screen shows timers, counters, memory stats, and allocation counts. GPU screen shows graphics info and GPU timers. Frames screen shows multi-frame grid visualization. Network screen shows ENet peer stats (transport RTT, packet loss), pipeline RTT (full client-server-client round-trip measured via timestamp echo, in milliseconds), per-second bandwidth (in/out), ACK tracking state (floor and received bitfield), game reconciliation state (confirmed tick, smoothed rollback depth, smoothed update buffer size, desync status), smoothed received-bitfield popcount, clock correction state (smoothed offset in ticks ahead/behind server, target ticks behind, and error deviation from target), and reconciliation tick classification counters (CRC-validated, assumed, CRC fast-path events, status-change replay, knock-on replay ticks per second). All numeric network stats (rollback, buffer, recv, clock offset/target/error) use `common::Smoothed<int64_t>` members owned by ProfileManagerBase for stable readouts. Reconciliation counters use `common::InTheLastSecond` members fed via `SetReconcileCounters()` from `ApplyReconcileResult()`. Clock correction values are fed in via `SetClockCorrection()` from Game's `ComputeClockCorrectionNs()`; rollback, buffer, and recv are computed and smoothed directly in `UpdateProfileText()`

## Extension

Game projects inherit from `ProfileManagerBase`, adding game-specific counter and timer arrays. See [game ProfileManager](../../../Projects/BrokenEngineSandbox/Source/Profile/CLAUDE.md) for details.
