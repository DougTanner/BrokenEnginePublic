# /Engine/Source/Frame/

The `/Engine/Source/Frame/` directory contains the core game state management with deterministic triple-buffered frame system.

## Core Files

### FrameBase.h/cpp
Base class for frame structures containing all game state with triple-buffering (Previous/Current/Next).
- **Frame timing**: `iFrame` counter, `fCurrentTime`, sun angle for day/night cycle
- **Object pools**: All game entities (`alignas(64)` for cache line optimization)  
- **Deterministic randomness**: Random engine seeded per frame for reproducibility
- **Version tracking**: `kiVersion` aggregates all pool versions for save compatibility
- **Global state**: `giBackgroundThreadCount` (worker thread count)
- **Visibility culling**: `InVisibleArea()`, `VisibleDistances()` functions

### Render.h/cpp
Frame rendering orchestration with interpolation between fixed timestep updates.
- **Matrix calculations**: View/perspective matrices updated per frame
- **Interpolation**: Smooth rendering between 250Hz physics updates
- **Coordinate transforms**: Screen↔world space conversion functions
- **Environment**: Day/night cycle with sun position calculations
- **Global singletons**:
  - `gMatView`, `gMatPerspective` - current frame rendering matrices
  - `gf4RenderVisibleArea` - frustum bounds for culling
  - `gf4VisibleTopLeft/Right/BottomLeft/Right` - screen corner world positions

### Navmesh.h/cpp  
Grid-based pathfinding system using 16×16 cell navigation mesh.
- **Distance field**: Player distance calculation in `smppuiPlayerDistances[16][16]`
- **Height constraints**: Navigation at fixed height (`kfHeight = 2.0f`)
- **Debug visualization**: Optional billboard rendering for pathfinding debug
- **Frame compatibility**: Trivially copyable for state serialization

### UpdateList.h
Abstract base class providing static methods for frame updates.
- **Update phases**: `Global()`, `Interpolate()`, `PostRender()`, `Collide()`, `Spawn()`, `Destroy()`
- **Render methods**: `RenderGlobal()`, `RenderMain()`

## Key Constants
- `kUpdateStepNs = 1'000'000'000ns / 250` - Fixed timestep (4ms)
- `kfDeltaTime = 0.004f` - Delta time in seconds
- `kfVisibleXAdjust/YAdjustTop/Bottom` - Visibility border adjustments

## System Dependencies

### Runtime Dependencies
- **Graphics** → Render.cpp provides matrices, pools use for rendering
- **Audio** → Sounds pool creates 3D sources with positions/velocities
- **FileManager** → FrameBase saves/loads state, island data
- **Input** → Frame processes input from previous frame

### Data Flow Pipeline
1. Input processed → stored for next frame
2. Frame updates at 250Hz fixed timestep
3. Object pools update in parallel (worker threads)
4. Audio positions updated from objects
5. Render interpolates between frames

### Object Pool → System Dependencies
- **Areas, Billboards, HexShields, Lighting, Smoke, Splashes, Targets** → Graphics
- **Explosions** → Graphics + Audio
- **Sounds** → Audio
- **Pullers/Pushers** → Physics forces (no rendering)

## See Also
- Collections: [Collections/CLAUDE.md](Collections/CLAUDE.md)
- Pools: [Pools/CLAUDE.md](Pools/CLAUDE.md)