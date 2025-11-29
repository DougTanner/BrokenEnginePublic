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
- Extends `engine::FramePostRenderBase` (inherits random engine, logic-phase state, and engine collections)
- Aggregates PlayerPostRender, BlastersPostRender, SpaceshipsPostRender
- Static Update(rCurrent, rCurrentInterpolate, rPreviousFrame, rFrameInput, fDeltaTime) calls parent Update() which runs AllocateAndCopy phase for engine collections, then processes logic-phase updates using input and delta time
- Static PreCollision(rFrame) dispatches to game collection PreCollision() methods to add layers to CollisionSystem via AddLayer()
- Static PostCollision(rFrame) dispatches to game collection PostCollision() methods to query collision results via HasCollision()/GetCollisions() and apply damage/destruction, then calls CollisionSystem::Clear()
- Static Spawn(rFrame, fDeltaTime) orchestrates wave-based enemy spawning and blaster creation
- Static Destroy(rFrame) removes destroyed objects and cleans up resources
- Full serialization support (equality, CRC, Write/Read member functions)

**Frame Structure**:
- Extends `engine::FrameBase` (inherits frame counter and engine-level state)
- Aggregates FrameInterpolate and FramePostRender instances
- Game state flags tracking menu/gameplay/death states
- Static Register() called during game initialization to register shared type configurations
- Calls parent FrameBase::Register() then invokes Register() on PlayerInterpolate
- Static AllocateGraphicsResources() called during pipeline initialization to create game-specific rendering pipelines for AreaLights, Player, and Spaceships
- Static InterpolateUpdate() and InterpolateSync() orchestrate interpolation phase for current and previous frames
- Static PostRenderUpdate() orchestrates logic phase input processing and physics
- Static PostRenderPreCollision() orchestrates collision layer registration where collections call AddLayer() on CollisionSystem
- Static PostRenderCollide() calls engine::CollisionSystem::Collide() for centralized sphere-sphere collision detection
- Static PostRenderPostCollision() orchestrates collision response phase where collections query results and apply damage
- Static PostRenderSpawn() and PostRenderDestroy() orchestrate spawning and destruction phases
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
- Static member `suiBlasterTypeIndex` stores the type index returned by BlastersInterpolate::sTypes registration
- Static member `suiCollisionLayerIndex` stores the layer index returned by CollisionSystem::AddLayer() each frame
- Static Update() processes input, updates fire timer, and applies physics using previous frame state and delta time
- Static PreCollision() adds player layer to CollisionSystem via AddLayer() with uniform radius and no damage/flags
- Static PostCollision() queries collision results via HasCollision()/GetCollisions(), marks player as exploding when colliding with spaceships
- Static Spawn() creates player-spawned blasters alternating left/right barrels using registered type index
- Static Destroy() processes player destruction
- Full serialization support (equality, CRC, Write/Read member functions)

**Collision Integration**:
- PreCollision adds single player position to collision system each frame via AddLayer() with uniform radius (1.5f)
- PostCollision queries collision results and marks player as exploding on spaceship collision
- Uses no damage/flags since player doesn't damage others on collision

**Blaster Type Registration**:
- PlayerInterpolate::Register() adds BlasterType configuration to BlastersInterpolate::sTypes static vector
- Registration stores shared configuration (visual size, area light type index) for all player blasters
- Type index cached in static member suiBlasterTypeIndex for use during blaster spawning
- Ensures consistent blaster configuration across all player-fired blasters
- Type registration occurs once during game initialization, reducing memory overhead for instanced blasters

**Design Pattern**: Phase separation ensures rendering state (position, direction) is independent from logic state (velocity, flags, cooldowns). Type registration in constructor leverages deterministic Frame initialization order. Both structures provide equality comparison, static Crc(), and Write/Read member functions for deterministic replay.

### HealthDamage.h

Combat balance constants and collision system configuration.

**Purpose**: Defines damage values, health pools, and collision filtering configuration for game objects.

**Damage Configuration**: Defines damage values for spaceship weapons and collisions, player health pools (armor, shield, energy), shield regeneration and penetration rates, enemy health values, and blaster/missile damage.

**Collision System Configuration**: Provides namespaces and enum class defining collision behavior used by engine::CollisionSystem:
- **CollisionCategory**: Bit flags identifying object types (kBlaster, kSpaceship, kPlayer)
- **CollisionMask**: Bit flag combinations defining what each object type can collide with (kPlayerBlaster hits spaceships, kSpaceship hits player and blasters, kPlayer hits spaceships)
- **ColliderFlags**: Enum class with common::Flags wrapper (ColliderFlags_t) for type-safe behavior modifiers (kDestroyOnCollide marks objects that should only collide once, kAlreadyCollided tracks hits within a frame)

**Design Pattern**: Categories answer "what am I?", masks answer "what can I hit?", and flags modify collision behavior. CollisionSystem performs bitwise AND tests during Collide() to determine compatible layer pairs, then applies kDestroyOnCollide/kAlreadyCollided flags during collision detection to prevent duplicate hits.

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
   - **InterpolateUpdate**: Calls FrameInterpolate::Update() which calls parent FrameInterpolateBase::Update() to run engine-level AllocateAndCopy phase, then updates sun angle, wave state, and calls static Update() on game collections to integrate velocities into positions
   - **InterpolateSync**: Calls FrameInterpolate::Sync() which synchronizes positions from game collections to engine collections (area lights via idToIndexMap)

2. **PostRender Phase** (Frame::PostRenderUpdate/PreCollision/Collide/PostCollision/Spawn/Destroy):
   - **PostRenderUpdate**: Calls FramePostRender::Update() which calls parent FramePostRenderBase::Update() to run engine-level AllocateAndCopy phase, then calls static Update() on game collections for logic processing and input handling
   - **PostRenderPreCollision**: Calls FramePostRender::PreCollision() which dispatches to game collection PreCollision() methods (PlayerPostRender, BlastersPostRender, SpaceshipsPostRender). Collections add layers to CollisionSystem via AddLayer() with categories, masks, radii, damage, and flags
   - **PostRenderCollide**: Calls FrameBase::PostRenderCollide() which calls engine::CollisionSystem::Collide() to perform centralized sphere-sphere collision detection across all added layers
   - **PostRenderPostCollision**: Calls FramePostRender::PostCollision() which dispatches to game collection PostCollision() methods. Collections query results via HasCollision()/GetCollisions() and apply damage/destruction. Calls CollisionSystem::Clear() at end to reset for next frame
   - **PostRenderSpawn**: Calls FramePostRender::Spawn() which orchestrates wave-based enemy spawning and blaster creation
   - **PostRenderDestroy**: Calls FramePostRender::Destroy() which removes destroyed objects and cleans up resources

## See Also
- Base engine frame: [../../../../Engine/Source/Frame/CLAUDE.md](../../../../Engine/Source/Frame/CLAUDE.md)
- Game object collections: [Collections/CLAUDE.md](Collections/CLAUDE.md)
