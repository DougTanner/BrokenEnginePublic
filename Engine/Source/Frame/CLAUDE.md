# /Engine/Source/Frame/

The `/Engine/Source/Frame/` directory contains core game state management using triple-buffering for deterministic simulation at 250Hz (4ms per frame) with interpolation.

## File Overview

### /Engine/Source/Frame/FrameBase.h/.cpp
Base class for frame structures containing all game state.
- Frame timing: `iFrame`, `fCurrentTime`, sun angle
- Object pools for all entities (`alignas(64)` for cache efficiency)  
- Random engine for deterministic randomness
- Version constant (`kiVersion`) aggregating pool versions
- Global singletons: `giBackgroundThreadCount` (worker thread count)
- Global visibility functions: `InVisibleArea()`, `VisibleDistances()`

### /Engine/Source/Frame/Render.h/.cpp
Frame rendering orchestration and matrix calculations.
- Calculates view/perspective matrices, visible area bounds
- Object rendering with interpolated positions
- Screen-to-world coordinate conversion
- Day/night cycle calculations
- Global singletons:
  - `gMatView`, `gMatPerspective` - rendering matrices
  - `gf4RenderVisibleArea` - current visible bounds
  - `gf4VisibleTopLeft/Right/BottomLeft/Right` - screen corners

### /Engine/Source/Frame/Navmesh.h/.cpp  
Grid-based pathfinding system (16x16).
- Player distance calculation stored in `smppuiPlayerDistances`
- Height-based navigation (`kfHeight = 2.0f`)
- Optional debug visualization with billboards
- Trivially copyable for frame state

### /Engine/Source/Frame/UpdateList.h (header only)
Base class providing static methods for frame updates.
- Update methods: `Global()`, `Interpolate()`, `PostRender()`, `Collide()`, `Spawn()`, `Destroy()`
- Render methods: `RenderGlobal()`, `RenderMain()`
- Inherited by pools needing multithreaded updates

## Subdirectories

### /Engine/Source/Frame/Collections/
Template classes for spawn request management.
- `Spawnable<T, SIZE>` - Fixed-size spawn buffer base template
- Project-specific collections inherit and add object arrays

### /Engine/Source/Frame/Pools/
Fixed-size object pools with O(1) allocation/deallocation.
- Base templates: `ObjectPool<T,U,V,SIZE>`, `ObjectControllerPool`
- Pool implementations: Areas, Billboards, Explosions, HexShields, Lighting, Pullers, Pushers, Smoke, Sounds, Splashes, Targets
- Global singleton: `giMultithreading` (atomic thread safety counter)
- Pool-specific globals: `gbSmokeClear`, `gbSmokeSpread`

## Key Constants
- `kUpdateStepNs = 1'000'000'000ns / 250` - Fixed timestep (4ms)
- `kfDeltaTime = 0.004f` - Delta time in seconds
- `kfVisibleXAdjust/YAdjustTop/Bottom` - Visibility border adjustments