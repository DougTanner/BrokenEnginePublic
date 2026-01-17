# /Projects/BrokenEngineSandbox/Source/Profile/

Game-specific profiling configuration that extends the engine's ProfileManager.

## Overview

Defines game-specific CPU counters and timers that integrate with the engine's profiling system. The engine includes these definitions via macros during compilation.

## GameProfile.h

Provides two macro pairs consumed by the engine's ProfileManager:

**Counters** (`CPU_COUNTERS_GAME_ENUM` / `CPU_COUNTERS_GAME`): Track per-frame object counts for game collections (blasters, missiles, spaceships, targets) with rendered subset counts for missiles and spaceships.

**Timers** (`CPU_TIMERS_GAME_ENUM` / `CPU_TIMERS_GAME`): Measure CPU time for game-specific phases in a hierarchical structure:
- Frame update (top-level)
  - Interpolate phase: AllocateAndCopy, Update
  - PostRender phase: AllocateAndCopy, Update, PreCollision, Collide, PostCollision, AreaDamage, Destroy, Spawn

Timer display names use indentation to show hierarchy in profiler output.

## See Also
- Engine profiling: [../../../../Engine/Source/Profile/CLAUDE.md](../../../../Engine/Source/Profile/CLAUDE.md)
