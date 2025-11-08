# /Engine/Source/Frame/

The `/Engine/Source/Frame/` directory contains the core game state management with deterministic triple-buffered frame system.

## Core Files

### TimeStep.h/cpp

Manages fixed timestep accumulator and time scaling for physics updates.

**Purpose**: Encapsulates 250Hz (4ms) fixed timestep logic used by GameBase.

**Key Features**:
- **Time Accumulation**: Tracks remainder time between frames
- **Time Scaling**: Support for slow-motion and fast-forward (multiply/divide)
- **VSync Monitoring**: Automatically reduces time scale if falling behind
- **Performance Tracking**: Smoothed average delta for monitoring
- **Reset on Focus Loss**: Prevents time jumps when window loses focus

**Core Methods**:
- `AddDelta(realDelta, bSingleStep, bLostFocus) -> stepCount` - Calculate physics steps needed
- `Reset()` - Reset timers (called on focus loss)
- `AdjustTimeScale(monitorRefreshTime)` - Slow down if behind VSync
- `ClearAccumulator()` - Reset time remainder
- `GetInterpolationAlpha()` - Smooth rendering between physics steps
- `GetAverageDelta()` - Performance monitoring

**Public Members**:
- `mRealTime` - High-resolution timer
- `miTimeMultiply`, `miTimeDivide` - Time scale factors
- `mUpdateRemainderNs` - Accumulated time
- `mAverageDelta` - Smoothed delta tracking

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
- All methods receive delta time (float fDeltaTime) as final parameter

### Frame Update Flow
Frame updates are split into three distinct phases called by GameBase::Update():

**UpdateFrameGlobal()** - Global phase (before RenderGlobal)
- Time-based systems and camera updates
- Called with final delta time after time scaling
- Updates global state needed for visible area calculation
- Runs before RenderGlobal to ensure camera is positioned
- Calls UpdateList::Global() for object pools

**UpdateFrameInterpolate()** - Interpolation phase (after RenderGlobal)
- Position and rotation smoothing between frames
- Uses visible area from FrameInput (set by CopyVisibleAreaToFrameInput)
- Prepares smooth animations for rendering
- Calls UpdateList::Interpolate() for object pools

**UpdateFrameFull()** - Full update phase (main game logic)
- PostRender: Updates that need rendered frame data
- Collision detection between objects
- Spawning new objects via Collections
- Destroying dead objects
- Calls UpdateList::PostRender(), Collide(), Spawn(), Destroy()
- Most game logic happens in this phase

**FrameType enum (debug-only)**:
- kGlobal, kInterpolate, kFull - tracks which phase is currently executing
- Only available in BT_DEBUG builds for validation
- Stored in gCurrentFrameTypeProcessing global

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