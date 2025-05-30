# /Engine/Source/Frame/Collections/

The `/Engine/Source/Frame/Collections/` directory contains template classes for managing collections of spawnable objects within the frame-based state management system.

## Files

### /Engine/Source/Frame/Collections/Collections.h

Template class for spawn request management:
- `Spawnable<T, SIZE>` - Fixed-size stack-allocated spawn buffer
  - `T` - Spawn information type (e.g., SpawnBlaster, SpawnMissile)
  - `SIZE` - Maximum spawns per frame (compile-time constant)
  - Methods:
    - `AddSpawn(const T& rSpawn)` - Add spawn request with bounds checking
    - `static Interpolate()` - Copy spawns from previous to current frame
    - `operator==()` - Frame verification for save states
  - Members:
    - `T pSpawns[SIZE]` - Fixed array of spawn requests
    - `int64_t iSpawnCount` - Current number of spawns

## Purpose

Provides a standardized way to handle object creation requests during frame updates. Game-specific collections inherit from `Spawnable` and add:
- Arrays for active object state (positions, velocities, etc.)
- Update methods (Global, Spawn, Collide, Destroy, etc.)
- Additional spawn processing logic

## Usage Pattern

1. Game logic calls `AddSpawn()` during frame updates
2. Collection's `Spawn()` method processes requests and creates objects
3. Spawn count typically reset each frame
4. Interpolation maintains spawns when no updates occur

## Related Systems

- `/Engine/Source/Frame/Pools/` - ObjectPool manages persistent objects created from spawns
- `/Projects/*/Source/Frame/Collections/` - Game-specific implementations