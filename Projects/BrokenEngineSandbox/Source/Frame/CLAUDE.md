# /Projects/BrokenEngineSandbox/Source/Frame/

Game-specific frame state and core game systems. Extends the engine's FrameBase with game logic for space combat.

## Architecture Overview

**Phase-Separated Structure**: Frame contains FrameInterpolate and FramePostRender sub-structures for strict separation between rendering state and logic state, enabling deterministic replay.

**Hierarchical Composition**: Each level (FrameInterpolate, FramePostRender, Frame) extends corresponding engine base structures (engine::FrameInterpolateBase, engine::FramePostRenderBase, engine::FrameBase) and aggregates game-specific collections (Player, Blasters, Spaceships).

**Simulation Rate**: 30Hz timestep (defined as kUpdateStepNs and kfDeltaTime) provides responsive gameplay with deterministic physics.

## Core Files

### Frame.h/cpp

Main game frame structure with hierarchical phase-based composition for deterministic gameplay.

**Purpose**: Aggregates game-specific state into a fully serializable structure with strict phase separation.

**FrameFlags**: Enum class defining game state flags (main menu, gameplay, first spawn, death screen) with typesafe flags wrapper.

**FrameInterpolate Structure**:
- Extends `engine::FrameInterpolateBase` (inherits sun angle, rendering-phase state, and engine collections including Colliders)
- Aggregates PlayerInterpolate, BlastersInterpolate, SpaceshipsInterpolate
- Wave spawning state for progressive difficulty scaling
- Static Update() calls parent Update() which runs AllocateAndCopy phase for engine collections, then integrates interpolation-phase updates from previous frame and delta time
- Static Sync() synchronizes positions from game collections to engine collections (area lights, colliders)
- Instance Render() method submits rendering commands to command buffer
- Full serialization support (equality, CRC, Write/Read member functions)

**FramePostRender Structure**:
- Extends `engine::FramePostRenderBase` (inherits random engine, logic-phase state, and engine collections including Colliders)
- Aggregates PlayerPostRender, BlastersPostRender, SpaceshipsPostRender
- Static Update(rCurrent, rCurrentInterpolate, rPreviousFrame, rFrameInput, fDeltaTime) calls parent Update() which runs AllocateAndCopy phase for engine collections, then processes logic-phase updates using input and delta time
- Static Collide(rFrame) calls engine::CollidersPostRender::Collide() to perform collision detection, then dispatches to game collection Collide() methods to query results and handle responses
- Static Spawn(rFrame) orchestrates wave-based enemy spawning, collections register colliders during object creation
- Static Destroy(rFrame) removes destroyed objects and cleans up resources, collections unregister colliders
- Full serialization support (equality, CRC, Write/Read member functions)

**Frame Structure**:
- Extends `engine::FrameBase` (inherits frame counter and engine-level state)
- Aggregates FrameInterpolate and FramePostRender instances
- Game state flags tracking menu/gameplay/death states
- Static RegisterTypes() called during game initialization to register shared type configurations
- Static AllocateGraphicsResources() called during pipeline initialization to create game-specific rendering pipelines for AreaLights, Player, and Spaceships
- Static InterpolateUpdate() and InterpolateSync() orchestrate interpolation phase for current and previous frames
- Static PostRenderUpdate(), PostRenderCollide(), PostRenderSpawn(), PostRenderDestroy() orchestrate logic phase with input processing, collision, spawning, and destruction
- Static Render() drives hierarchical rendering
- Island configuration constants for terrain setup
- Enemy spawn position helper method

**Serialization Support**: Each level provides equality comparison, static Crc() generation, and Write/Read member functions. CRCs aggregate via XOR from all contained structures for deterministic replay validation. Collection serialization delegates to engine template functions (`engine::CollectionCrc()`, `engine::WriteCollection()`, `engine::ReadCollection()`) with collection macros.

**Version Tracking**: Version numbers aggregate from all contained structures (kiVersion calculations) ensuring save file compatibility across engine and game changes.

### Player.h/cpp

Player spaceship controller with phase-separated state for deterministic replay and blaster type registration.

**Purpose**: Manages player state across rendering and logic phases, registers shared blaster type configuration during initialization.

**PlayerInterpolate Structure**:
- Position, facing direction, and collider ID for rendering
- Static CreatePipelines() registers storage buffer via BufferManager::CreateDynamicBuffer() with CRC key and creates both main and shadow rendering pipelines using PipelineManager::CreateGltfPipelinePair()
- CreateGltfPipelinePair() accepts name, glTF CRC, model vertex buffer CRC, and storage buffers, returning both pipelines in single call
- Static Update() integrates velocity into position using previous frame state and delta time
- Static Sync() synchronizes position to collider using CollidersPostRender::UpdatePosition()
- Instance Render() retrieves storage buffer via CRC-based lookup in mDynamicStorageBuffers and submits player rendering commands
- Full serialization support (equality, CRC, Write/Read member functions)

**PlayerFlags**: Enum class defining player state flags (exploding, fire blaster) with typesafe flags wrapper.

**PlayerPostRender Structure**:
- Velocity, wanted direction, and weapon state
- Player status flags tracking explosion and blaster firing
- Blaster fire timing with cooldown-based rate limiting
- Static member `suiBlasterTypeIndex` stores the type index returned by BlastersPostRender::RegisterType()
- Constructor registers blaster type configuration and stores returned type index for use by blaster spawning
- Static Update() processes input, updates fire timer, and applies physics using previous frame state and delta time
- Static Collide() queries collision system via engine::CollidersPostRender::GetCollisions(), applies damage from colliding blasters/spaceships
- Static Spawn() creates player-spawned blasters alternating left/right barrels using registered type index, registers player collider on first spawn
- Static Destroy() processes player destruction, unregisters collider
- Full serialization support (equality, CRC, Write/Read member functions)

**Collision Integration**:
- Player registers with category kPlayer and mask allowing collisions with spaceships
- Spawn creates collider with player category and collision mask on first player spawn
- Sync synchronizes collider position via CollidersPostRender::UpdatePosition()
- Collide queries collision system, applies damage from spaceship collisions
- Destroy unregisters collider via CollidersPostRender::Remove() before cleanup

**Blaster Type Registration**:
- PlayerPostRender constructor calls BlastersPostRender::RegisterType() with BlasterType configuration
- Registration stores shared configuration (texture CRC, sizes, light intensities) and automatically creates corresponding AreaLightType
- Returned type index cached in static member suiBlasterTypeIndex for use during blaster spawning
- Ensures consistent blaster configuration across all player-fired blasters
- Type registration occurs once during game initialization, reducing memory overhead for instanced blasters

**Design Pattern**: Phase separation ensures rendering state (position, direction) is independent from logic state (velocity, flags, cooldowns). Type registration in constructor leverages deterministic Frame initialization order. Both structures provide equality comparison, static Crc(), and Write/Read member functions for deterministic replay.

### HealthDamage.h

Combat balance constants and collision system configuration.

**Purpose**: Defines damage values, health pools, and collision filtering configuration for game objects.

**Damage Configuration**: Defines damage values for spaceship weapons and collisions, player health pools (armor, shield, energy), shield regeneration and penetration rates, and enemy health values.

**Collision System Configuration**: Provides three namespaces defining collision behavior used by engine Colliders collection:
- **CollisionCategory**: Bit flags identifying object types (kBlaster, kSpaceship, kPlayer)
- **CollisionMask**: Bit flag combinations defining what each object type can collide with
- **ColliderFlags**: Behavior modifiers (kDestroyOnCollide for single-hit objects, kAlreadyCollided for tracking hits within a frame)

**Design Pattern**: Categories answer "what am I?", masks answer "what can I hit?", and flags modify collision behavior. The engine performs bitwise tests during collision detection to filter incompatible pairs before distance calculations.

## Wave Spawning System

**Purpose**: Manages progressive difficulty scaling through wave-based enemy spawning.

**Architecture**:
- Wave state tracked in FrameInterpolate for deterministic replay
- FramePostRender::Spawn() orchestrates wave progression and enemy creation
- Clump mechanics divide spawns over time to prevent overwhelming player

**Wave State Variables**:
- Wave counter and display timer
- Clump tracking for staged spawning
- Spawn timing for controlled enemy introduction

## Update Flow

The game follows the engine's two-phase update pattern with AllocateAndCopy phase:

1. **Interpolate Phase** (Frame::InterpolateUpdate/Sync):
   - **InterpolateUpdate**: Calls FrameInterpolate::Update() which calls parent FrameInterpolateBase::Update() to run engine-level AllocateAndCopy phase (including Colliders), then updates sun angle, wave state, and calls static Update() on game collections to integrate velocities into positions
   - **InterpolateSync**: Calls FrameInterpolate::Sync() which synchronizes positions from game collections to engine collections (area lights via idToIndexMap, colliders via CollidersPostRender::UpdatePosition())

2. **PostRender Phase** (Frame::PostRenderUpdate/Collide/Spawn/Destroy):
   - **PostRenderUpdate**: Calls FramePostRender::Update() which calls parent FramePostRenderBase::Update() to run engine-level AllocateAndCopy phase (including Colliders), then calls static Update() on game collections for logic processing and input handling. Receives fDeltaTime and rFrameInput parameters
   - **PostRenderCollide**: Calls FramePostRender::Collide() which calls engine::CollidersPostRender::Collide() to perform centralized collision detection, then dispatches to game collection Collide() methods (PlayerPostRender, BlastersPostRender, SpaceshipsPostRender) to query results and apply damage/destruction. Receives fDeltaTime parameter
   - **PostRenderSpawn**: Calls FramePostRender::Spawn() which orchestrates wave-based enemy spawning and player blaster creation. Game collections register colliders via engine::CollidersPostRender::Add() during object creation. Receives fDeltaTime parameter
   - **PostRenderDestroy**: Calls FramePostRender::Destroy() which removes destroyed objects and cleans up resources. Game collections unregister colliders via engine::CollidersPostRender::Remove(). Receives fDeltaTime parameter

## See Also
- Base engine frame: [../../../../Engine/Source/Frame/CLAUDE.md](../../../../Engine/Source/Frame/CLAUDE.md)
- Game object collections: [Collections/CLAUDE.md](Collections/CLAUDE.md)
