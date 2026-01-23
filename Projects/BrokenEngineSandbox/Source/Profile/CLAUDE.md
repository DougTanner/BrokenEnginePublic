# /Projects/BrokenEngineSandbox/Source/Profile/

Game-specific profiling that extends `engine::ProfileManagerBase` with game counters and timers.

**Global**: `game::gpProfileManager` (singleton pointer used by all engine and game code)

## Overview

Implements `game::ProfileManager` inheriting from `engine::ProfileManagerBase`, adding game-specific CPU counters and timers as member arrays. Provides the global `gpProfileManager` pointer that engine code uses via forward declaration.

## Architecture

**ProfileManager.h**: Defines `game::ProfileManager` class inheriting from `engine::ProfileManagerBase`. Contains:
- `GameCpuCounters` and `GameCpuTimers` enums with values starting from the engine's count (e.g., `kCpuTimerFrameUpdate = engine::kEngineCpuTimerCount`)
- Member arrays `mGameCpuCounters[]` and `mGameCpuTimers[]` with display names
- Virtual method overrides for `GetCpuCounter()`, `GetCpuTimer()`, `GetCpuCounterCount()`, `GetCpuTimerCount()`

**ProfileManager.cpp**: Implements virtual methods that dispatch to engine base class arrays for engine indices, or game member arrays for game indices.

**Enum Values**: Game enums directly include the engine offset in their values, so game code uses `game::kCpuTimerFrameUpdate` directly in profiling calls without additional calculation.

## Profiling Categories

**Counters**: Track per-frame object counts for game collections (blasters, missiles, spaceships, targets) with rendered subset counts.

**Timers**: Measure CPU time for game-specific phases in a hierarchical structure:
- Frame update (top-level)
  - Interpolate phase: AllocateAndCopy, Update (with Spaceships sub-timers)
  - PostRender phase: AllocateAndCopy, Update, PreCollision, Collide, PostCollision, AreaDamage, Destroy, Spawn

Timer display names use indentation to show hierarchy in profiler output.

## See Also
- Engine profiling base: [../../../../Engine/Source/Profile/CLAUDE.md](../../../../Engine/Source/Profile/CLAUDE.md)
