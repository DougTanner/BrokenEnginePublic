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

Base classes for frame structures with hierarchical phase-based separation and serialization support.

**Architecture**: Frame state is organized into three independent base classes for the two-phase update system:

**FrameBase** - Core frame metadata:
- Frame counter and type tracking (Interpolate vs PostRender phase)
- Global area bounds for the game world
- Static Register() called during game initialization to register engine-level types and configuration
- Provides UpdateInterpolate() and Render() static methods for frame-level operations

**FrameInterpolateBase** - Time-based state for Interpolate phase:
- Sun angle for day/night cycle progression
- Provides Update() and Render() static methods for interpolate-phase operations
- Game-specific interpolate classes extend this base

**FramePostRenderBase** - Logic-phase state for PostRender phase:
- Deterministic random engine state for procedural generation
- UUID counter for globally unique ID generation across all indexable collections
- Provides static methods for post-render operations:
  - Update(rCurrent, rPreviousFrame, rFrameInput, fDeltaTime) - Processes input-driven logic
  - Collide(rFrame) - Handles collision detection
  - Spawn(rFrame) - Manages object creation
  - Destroy(rFrame) - Handles object removal
- Game-specific post-render classes extend this base

**Why This Three-Level Design**:
- Separates concerns: frame metadata, time-based rendering state, and logic-phase state
- Each level has explicit version tracking for save file compatibility
- Enables game-specific extensions (game::FrameInterpolate extends FrameInterpolateBase, game::FramePostRender extends FramePostRenderBase)
- Each base class provides equality comparison, CRC generation, and Write/Read member functions for hierarchical serialization and deterministic replay verification
- Clarifies data dependencies and update causality between phases
- Supports composition pattern where game Frame aggregates FrameInterpolate and FramePostRender structures

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
- `PreCollision()` - Bind collision layer data to CollisionSystem (called before Collide)
- `PostCollision()` - Handle collision results from CollisionSystem (called after Collide)
- `Spawn()` - Process deferred object creation requests (called after PostCollision)
- `Destroy()` - Clean up dead objects (called after Spawn)

**Render Methods**:
- `RenderMoveCamera()` - Camera positioning for rendering
- `RenderMain()` - Primary rendering pass

**Design Pattern**: Virtual interface allows heterogeneous pools to be updated uniformly via variadic template functions.

### CollisionSystem.h/cpp

Centralized collision detection system using layer-based filtering and sphere-sphere tests.

**Purpose**: Provides efficient collision detection across multiple object types with per-frame layer registration.

**Architecture**: Layer-based system where collections add layers each frame in PreCollision, then layers are cleared after PostCollision.

**CollisionLayer Structure**:
- Stores per-frame data: position arrays, radius/damage/flags (per-object or uniform)
- Category and collision mask for filtering
- Flags pointer for per-object collision behavior (using ColliderFlags_t)

**Key Operations**:
- `AddLayer()` - Per-frame registration returning layer index (called during PreCollision phase)
- `Collide()` - Computes compatible layer pairs and performs sphere-sphere tests, stores results by layer+index key
- `HasCollision()` / `GetCollisions()` - Query interface for collections to check results during PostCollision phase
- `Clear()` - Clears all layers (called at end of PostCollision phase)

**Collision Filtering**: Uses category bits (what am I?) and mask bits (what can I hit?) for early rejection before distance tests.

**Collision Flags**: Behavior modifiers using ColliderFlags enum class with common::Flags wrapper. kDestroyOnCollide prevents multiple hits per frame via kAlreadyCollided tracking.

**Design Pattern**: Decouples collision detection from game logic - collections add layers in PreCollision, query results in PostCollision, layers cleared after PostCollision. Compatible layer pairs computed during Collide() phase.

## Frame Update Flow

Frame updates are split into two distinct phases, implemented in FrameBase.cpp and called by GameBase:

**Interpolate Phase** (FrameBase::UpdateInterpolate → FrameInterpolateBase::Update):
- Advances frame counter and simulation time
- **AllocateAndCopy**: Calls AllocateAndCopy() on all collections to copy metadata and allocate memory
- Propagates sun angle and day/night cycle state
- Invokes Update() on collections to smooth positions and rotations
- Game-specific interpolation via game::FrameInterpolate::Update()
- Prepares smooth visual state for main rendering

**PostRender Phase** (FrameBase::PostRenderUpdate/PreCollision/Collide/PostCollision/Spawn/Destroy → FramePostRenderBase methods):
- **PostRenderUpdate**: Updates navigation mesh, processes input-driven logic via collection Update() methods, propagates deterministic random state. Parameters: rCurrent, rPreviousFrame, fDeltaTime, rFrameInput
- **PostRenderPreCollision**: Collections add layers to CollisionSystem via AddLayer(). Each collection adds its current positions, radii, damages, and flags for the frame. Parameters: rCurrent
- **PostRenderCollide**: Centralized collision detection via CollisionSystem::Collide(). Performs sphere-sphere tests on all compatible layer pairs and stores results.
- **PostRenderPostCollision**: Collections query collision results via HasCollision()/GetCollisions() and apply damage/destruction logic. Calls CollisionSystem::Clear() at end to reset layers for next frame. Parameters: rCurrent
- **PostRenderSpawn**: Object creation via collection Spawn() methods. Runs second-to-last, as spawned objects have no previous frame data. Parameters: rCurrent, rPreviousFrame, fDeltaTime
- **PostRenderDestroy**: Object removal via collection Destroy() methods. Runs last, as it desynchronizes indices from previous frame. Parameters: rCurrent, rPreviousFrame, fDeltaTime

**Why AllocateAndCopy Phase**:
- Runs before Update() to ensure all collection metadata is available before any Update() logic executes
- Solves dependency issues where one collection's Update() needs another collection's idToIndexMap
- Separates memory allocation concerns from game logic processing
- Enables safe cross-collection references during Update() phase

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
