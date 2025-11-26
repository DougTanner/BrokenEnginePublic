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
- Extends `engine::FrameInterpolateBase` (inherits sun angle and rendering-phase state)
- Aggregates PlayerInterpolate, BlastersInterpolate, SpaceshipsInterpolate
- Wave spawning state for progressive difficulty scaling
- Static Update() calls parent Update(), runs AllocateAndCopy phase for all game collections, then integrates interpolation-phase updates from previous frame and delta time
- Instance Render() method submits rendering commands to command buffer
- Full serialization support (equality, CRC, Write/Read member functions)

**FramePostRender Structure**:
- Extends `engine::FramePostRenderBase` (inherits random engine and logic-phase state)
- Aggregates PlayerPostRender, BlastersPostRender, SpaceshipsPostRender
- Static Update() calls parent Update(), runs AllocateAndCopy phase for all game collections, then processes logic-phase updates using input and delta time
- Static Collide() method handles collision detection between all game objects
- Static Spawn() method orchestrates wave-based enemy spawning
- Static Destroy() method removes destroyed objects and cleans up resources
- Full serialization support (equality, CRC, Write/Read member functions)

**Frame Structure**:
- Extends `engine::FrameBase` (inherits frame counter and engine-level state)
- Aggregates FrameInterpolate and FramePostRender instances
- Game state flags tracking menu/gameplay/death states
- Static CreatePipelines() called during pipeline initialization to create game-specific rendering pipelines for AreaLights, Player, and Spaceships
- Static UpdateInterpolate() orchestrates interpolation phase for current and previous frames
- Static UpdatePostRender() orchestrates logic phase with input processing
- Instance Render() method drives hierarchical rendering
- Island configuration constants for terrain setup
- Enemy spawn position helper method

**Serialization Support**: Each level provides equality comparison, static Crc() generation, and Write/Read member functions. CRCs aggregate via XOR from all contained structures for deterministic replay validation. Collection serialization delegates to engine template functions (`engine::CollectionCrc()`, `engine::WriteCollection()`, `engine::ReadCollection()`) with collection macros.

**Version Tracking**: Version numbers aggregate from all contained structures (kiVersion calculations) ensuring save file compatibility across engine and game changes.

### Player.h/cpp

Player spaceship controller with phase-separated state for deterministic replay and blaster type registration.

**Purpose**: Manages player state across rendering and logic phases, registers shared blaster type configuration during initialization.

**PlayerInterpolate Structure**:
- Position and facing direction for rendering
- Static CreatePipelines() registers storage buffer via BufferManager::CreateDynamicBuffer() with CRC key and creates both main and shadow rendering pipelines using PipelineManager::CreateGltfPipelinePair()
- CreateGltfPipelinePair() accepts name, glTF CRC, model vertex buffer CRC, and storage buffers, returning both pipelines in single call
- Static Update() integrates velocity into position using previous frame state and delta time
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
- Static Collide() handles player collision detection
- Static Spawn() creates player-spawned blasters alternating left/right barrels using registered type index
- Static Destroy() processes player destruction
- Full serialization support (equality, CRC, Write/Read member functions)

**Blaster Type Registration**:
- PlayerPostRender constructor calls BlastersPostRender::RegisterType() with BlasterType configuration
- Registration stores shared configuration (texture CRC, sizes, light intensities) and automatically creates corresponding AreaLightType
- Returned type index cached in static member suiBlasterTypeIndex for use during blaster spawning
- Ensures consistent blaster configuration across all player-fired blasters
- Type registration occurs once during game initialization, reducing memory overhead for instanced blasters

**Design Pattern**: Phase separation ensures rendering state (position, direction) is independent from logic state (velocity, flags, cooldowns). Type registration in constructor leverages deterministic Frame initialization order. Both structures provide equality comparison, static Crc(), and Write/Read member functions for deterministic replay.

### HealthDamage.h

Placeholder for future damage and health configuration constants.

**Purpose**: Reserved for combat balance tuning values.

**Current State**: Empty header file.

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

1. **Interpolate Phase** (Frame::UpdateInterpolate -> FrameInterpolate::Update):
   - Calls parent FrameInterpolateBase::Update() which runs engine-level AllocateAndCopy phase
   - **AllocateAndCopy**: Calls AllocateAndCopy() on PlayerInterpolate, BlastersInterpolate, SpaceshipsInterpolate to copy metadata and allocate memory
   - Updates sun angle and day/night cycle state
   - Updates wave display timer and spawning state
   - Calls static Update() on PlayerInterpolate, BlastersInterpolate, SpaceshipsInterpolate
   - Integrates velocities into positions for smooth rendering

2. **PostRender Phase** (Frame::UpdatePostRender -> FramePostRender methods):
   - Calls parent FramePostRenderBase::Update() which runs engine-level AllocateAndCopy phase
   - **AllocateAndCopy**: Calls AllocateAndCopy() on PlayerPostRender, BlastersPostRender, SpaceshipsPostRender to copy metadata and allocate memory
   - **Update**: Calls static Update() on PlayerPostRender, BlastersPostRender, SpaceshipsPostRender for logic processing and input handling
   - **Collide**: Handles collision detection between all game objects
   - **Spawn**: Orchestrates wave-based enemy spawning and object creation
   - **Destroy**: Removes destroyed objects and cleans up resources

## See Also
- Base engine frame: [../../../../Engine/Source/Frame/CLAUDE.md](../../../../Engine/Source/Frame/CLAUDE.md)
- Game object collections: [Collections/CLAUDE.md](Collections/CLAUDE.md)
