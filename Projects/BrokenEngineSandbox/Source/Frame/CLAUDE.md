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
- Static Update() method integrates all interpolation-phase updates from previous frame and delta time
- Instance Render() method submits rendering commands to command buffer
- Full serialization support (equality, checksum, stream operators)

**FramePostRender Structure**:
- Extends `engine::FramePostRenderBase` (inherits random engine and logic-phase state)
- Aggregates PlayerPostRender, BlastersPostRender, SpaceshipsPostRender
- Static Update() method processes all logic-phase updates using input and delta time
- Static Collide() method handles collision detection between all game objects
- Static Spawn() method orchestrates wave-based enemy spawning
- Static Destroy() method removes destroyed objects and cleans up resources
- Full serialization support (equality, checksum, stream operators)

**Frame Structure**:
- Extends `engine::FrameBase` (inherits frame counter and engine-level state)
- Aggregates FrameInterpolate and FramePostRender instances
- Game state flags tracking menu/gameplay/death states
- Static UpdateInterpolate() orchestrates interpolation phase for current and previous frames
- Static UpdatePostRender() orchestrates logic phase with input processing
- Instance Render() method drives hierarchical rendering
- Island configuration constants for terrain setup
- Enemy spawn position helper method

**Serialization Support**: Each level provides equality comparison, static Checksum() generation, and stream operators. Checksums aggregate via XOR from all contained structures for deterministic replay validation.

**Version Tracking**: Version numbers aggregate from all contained structures (kiVersion calculations) ensuring save file compatibility across engine and game changes.

### Player.h/cpp

Player spaceship controller with phase-separated state for deterministic replay.

**Purpose**: Manages player state across rendering and logic phases.

**PlayerInterpolate Structure**:
- Position and facing direction for rendering
- Static Update() integrates velocity into position using previous frame state and delta time
- Instance Render() submits player rendering commands to command buffer
- Full serialization support (equality, checksum, stream operators)

**PlayerFlags**: Enum class defining player state flags (exploding, fire blaster) with typesafe flags wrapper.

**PlayerPostRender Structure**:
- Velocity, wanted direction, and weapon state
- Player status flags tracking explosion and blaster firing
- Weapon cooldown tracking for fire rate limiting
- Static Update() processes input and physics using previous frame state and delta time
- Static Collide() handles player collision detection
- Static Spawn() creates player-spawned objects
- Static Destroy() processes player destruction
- Full serialization support (equality, checksum, stream operators)

**Design Pattern**: Phase separation ensures rendering state (position, direction) is independent from logic state (velocity, flags, cooldowns). Both structures provide equality comparison, static Checksum(), and stream operators for deterministic replay.

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

The game follows the engine's two-phase update pattern:

1. **Interpolate Phase** (Frame::UpdateInterpolate -> FrameInterpolate::Update):
   - Updates engine base state (sun angle, etc.)
   - Calls static Update() on PlayerInterpolate, BlastersInterpolate, SpaceshipsInterpolate
   - Integrates velocities into positions for smooth rendering
   - Updates wave display timer and spawning state

2. **PostRender Phase** (Frame::UpdatePostRender -> FramePostRender methods):
   - **Update**: Calls static Update() on PlayerPostRender, BlastersPostRender, SpaceshipsPostRender for logic processing and input handling
   - **Collide**: Handles collision detection between all game objects
   - **Spawn**: Orchestrates wave-based enemy spawning and object creation
   - **Destroy**: Removes destroyed objects and cleans up resources

## See Also
- Base engine frame: [../../../../Engine/Source/Frame/CLAUDE.md](../../../../Engine/Source/Frame/CLAUDE.md)
- Game object collections: [Collections/CLAUDE.md](Collections/CLAUDE.md)
