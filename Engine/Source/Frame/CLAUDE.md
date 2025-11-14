# /Engine/Source/Frame/

The `/Engine/Source/Frame/` directory contains the core game state management with deterministic triple-buffered frame system.

## Core Files

### TimeStep.h/cpp

Manages fixed timestep accumulator and time scaling for physics updates.

**Purpose**: Encapsulates 250Hz (4ms) fixed timestep logic used by GameBase to ensure consistent physics simulation regardless of rendering framerate.

**Key Responsibilities**:
- Accumulates real-time delta and converts to discrete physics steps
- Provides time scaling for slow-motion and fast-forward effects
- Monitors performance with smoothed delta tracking and spike detection
- Handles focus loss to prevent time accumulation jumps
- Calculates interpolation alpha for smooth rendering between physics steps

**Design Pattern**: Acts as a time quantizer - converts variable rendering time into fixed physics steps while preserving remainder for interpolation.

### FrameBase.h/cpp

Base class for frame structures containing all game state with dual-buffering.

**Architecture**: Frame state is organized into two sub-structures corresponding to the two-phase update system:

**FrameBaseInterpolate** - Contains all dynamic game object pools and time-based state:
- Frame metadata: frame number, current time, frame type, random engine
- Environment: global area bounds, sun angle
- Visual effects: areas, billboards, explosions, hex shields, particle puffs, trails
- Lighting: area lights and point lights with their animation controllers
- Physics: pullers, pushers, splashes, targets
- Audio: 3D sound sources
- Cache-aligned for optimal parallel processing

**FrameBasePostRender** - Holds state for the PostRender phase:
- Navigation mesh for AI pathfinding
- Future expansion point for game-specific PostRender-phase data

**Why This Design**:
- Splitting by update phase clarifies data dependencies and enables efficient partial updates
- Dual-buffering (Current/Next) with swap-based updates for efficient state progression
- Trivially copyable for fast frame state replication and save/load
- Version number aggregation ensures save file compatibility

### Render.h/cpp

Frame rendering orchestration with interpolation between fixed timestep updates.

**Purpose**: Bridges the gap between fixed-rate physics (250Hz) and variable-rate rendering by computing interpolated visual state.

**Key Responsibilities**:
- Orchestrates frame update phases (Interpolate, PostRender, Collide, Spawn, Destroy)
- Manages day/night cycle and sun positioning
- Provides frame-level rendering coordination for all object pools

**Design Pattern**: Camera matrices and visible area calculation are handled by CameraBase. Rendering systems access camera state via the global `gpCamera` pointer.

### Navmesh.h/cpp

Grid-based pathfinding system using a coarse navigation mesh.

**Purpose**: Provides efficient AI pathfinding by maintaining a distance field from the player across a grid.

**Key Responsibilities**:
- Manages 16×16 cell navigation grid for pathfinding queries
- Calculates distance from player to all grid cells for enemy AI
- Constrains navigation to a fixed height plane
- Provides optional debug visualization of pathfinding data

**Why This Design**: Coarse grid trades precision for performance - AI can query paths quickly without expensive continuous pathfinding.

### UpdateList.h

Abstract base class defining the interface for frame update phases.

**Purpose**: Establishes the contract that all game object pools must implement for participating in the frame update system.

**Update Phases**:
- `Interpolate()` - Position/rotation smoothing for rendering
- `PostRender()` - Logic that depends on current frame rendering (input processing)
- `Collide()` - Collision detection and response
- `Spawn()` - Process deferred object creation requests
- `Destroy()` - Clean up dead objects

**Render Methods**:
- `RenderGlobal()` - Shadow passes and pre-main rendering
- `RenderMain()` - Primary rendering pass

**Design Pattern**: Virtual interface allows heterogeneous pools to be updated uniformly via variadic template functions.

## Frame Update Flow

Frame updates are split into two distinct phases, implemented in FrameBase.cpp and called by GameBase:

**WriteFrameInterpolateBase()** - Interpolate phase:
- Advances frame counter and simulation time
- Propagates deterministic random state
- Copies object pools from previous frame as baseline
- Updates animation controllers for lights and particles
- Invokes Interpolate() to smooth positions and rotations
- Game-specific interpolation via WriteFrameInterpolate()
- Prepares smooth visual state for main rendering

**WriteFramePostRenderBase()** - PostRender phase (main game logic):
- Updates navigation mesh and collision structures
- Invokes PostRender() for input-driven logic
- Invokes Collide() for damage and collision resolution
- Invokes Spawn() to create new objects via WriteFramePostRenderSpawn()
- Invokes Destroy() to remove dead objects via WriteFramePostRenderDestroy()
- Game-specific full updates via WriteFramePostRender()

**Why Two Phases**:
- Interpolate phase handles time-based and visual updates for rendering
- PostRender phase ordering (PostRender → Collide → Spawn → Destroy) ensures proper causality
- Shadow rendering happens before Interpolate for full physics steps, allowing smooth interpolation afterward

**Parallelization**: Engine provides `Multithread<>()` helper that distributes update work across worker threads using dynamic bucket sizing.

## System Dependencies

### Runtime Dependencies
- **Graphics** → Render.cpp provides matrices and visible area for rendering
- **Audio** → Sound pool creates 3D audio sources from object positions
- **FileManager** → Frame save/load, island heightmap data
- **Input** → Frame consumes input from previous frame for deterministic replay

### Data Flow Pipeline
1. Input captured and stored for next frame
2. Frame updates at 250Hz fixed timestep
3. Object pools update in parallel across worker threads
4. Audio source positions updated from object state
5. Rendering interpolates between physics steps for smooth visuals

## See Also
- Collections: [Collections/CLAUDE.md](Collections/CLAUDE.md) - Spawn request management
- Pools: [Pools/CLAUDE.md](Pools/CLAUDE.md) - Object pool implementations
