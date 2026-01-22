# /Projects/BrokenEngineSandbox/Source/Profile/

Game-specific profiling configuration that extends the engine's ProfileManager.

## Overview

Defines game-specific CPU counters and timers that integrate with the engine's profiling system using explicit enums and lookup functions (not X-macros, for IntelliSense compatibility).

## Architecture

**GameProfile.h**: Declares enums starting from 0 (`GameCpuCounters`, `GameCpuTimers`) with sentinel count values, plus accessor function prototypes. Lives in `game::profile` namespace.

**GameProfile.cpp**: Defines the actual arrays of `engine::CpuCounter` and `engine::CpuTimer` structs with display names, and implements the accessor functions that return references to these arrays.

**Offset Integration**: The engine's ProfileManager.h includes GameProfile.h and provides convenience constants in the `game::` namespace (e.g., `game::kCpuTimerFrameUpdate`) that add the engine offset to game-local indices.

## Profiling Categories

**Counters**: Track per-frame object counts for game collections (blasters, missiles, spaceships, targets) with rendered subset counts for missiles and spaceships.

**Timers**: Measure CPU time for game-specific phases in a hierarchical structure:
- Frame update (top-level)
  - Interpolate phase: AllocateAndCopy, Update (with Spaceships sub-timers)
  - PostRender phase: AllocateAndCopy, Update, PreCollision, Collide, PostCollision, AreaDamage, Destroy, Spawn

Timer display names use indentation to show hierarchy in profiler output.

## See Also
- Engine profiling: [../../../../Engine/Source/Profile/CLAUDE.md](../../../../Engine/Source/Profile/CLAUDE.md)
