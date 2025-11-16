# /Projects/BrokenEngineSandbox/Source/Frame/

Game-specific frame state and core game systems. Extends the engine's FrameBase with game logic for space combat.

## Architecture Overview

**Frame Structure**: Contains two sub-structures - FrameInterpolate (extends FrameBaseInterpolate) and FramePostRender (extends FrameBasePostRender) - providing game-specific state for the update pipeline.

**Update Pattern**: Follows engine's two-phase update system (Interpolate → PostRender) with player and game object collections participating in each phase.

**Simulation Rate**: 30Hz timestep (defined in Frame.h as kUpdateStepNs and kfDeltaTime) provides responsive gameplay while allowing complex AI and physics calculations.

## Core Files

### Frame.h/cpp

Main game frame structure containing all game-specific state extending the engine's FrameBase.

**Purpose**: Aggregates game-specific state into a hierarchical serializable structure with phase-based separation:
- `FrameInterpolate` extends `engine::FrameInterpolateBase` for time-based updates
- `FramePostRender` extends `engine::FrameBasePostRender` for logic-phase updates
- Provides version tracking for save file compatibility

**Frame Flags** (FrameFlags_t):
- Main menu, gameplay, first spawn, death screen states

**FrameInterpolate Structure**:
- Player interpolate state (position, direction)
- Inherits sun angle and base interpolate state from engine

**FramePostRender Structure**:
- Player post-render state (velocity, wanted direction, flags)
- Inherits random engine and navmesh from engine

**Frame-Level Methods**:
- `UpdateInterpolate()` - Advances time-based systems, checks death conditions, updates sun angle
- `UpdatePostRender()` - Main game logic including input processing, enemy spawning, collision detection
- `Render()` - Orchestrates rendering for all game objects

**Helper Functions**: Template functions for applying damage and slow effects to collections, island flip transformations, health scaling based on wave progression.

**Design Pattern**: Binary stream operators for each level enable hierarchical serialization while maintaining compact format. Version aggregation ensures compatibility across engine and game state changes.

### Player.h/cpp

Player spaceship controller split into interpolate and post-render phases.

**Purpose**: Manages player state through two update phases for deterministic replay support.

**PlayerInterpolate Structure** (Update phase):
- Position and facing direction
- Smooth interpolation for rendering
- Version tracked for save compatibility

**PlayerPostRender Structure** (Logic phase):
- Velocity and wanted direction (input-driven)
- Status flags (explosion state)
- Input processing, weapon firing, ability activation
- Static `Update()` method processes input and frame state

**Design Pattern**: Phase-based separation clarifies which systems depend on current frame state (PostRender) versus smooth animation state (Interpolate). Binary stream operators for both structures enable versioned serialization. Flags type provides bitwise state management for deterministic state.

### HealthDamage.h

Damage and health configuration constants defining combat balance.

**Purpose**: Centralizes all damage values and health capacities for easy game balance tuning.

**Configuration Categories**:
- Player defensive stats (armor, shield, energy capacities and regeneration)
- Enemy health values with wave-based scaling
- Weapon damage values (player vs enemy attacks)
- Pickup drop probabilities

**Design Pattern**: Header-only constant definitions allow compile-time optimization and easy balance iteration without recompilation of implementation files.

## Update Flow

The game follows the engine's update pattern with game-specific implementations:

1. **Interpolate Phase** (WriteFrameInterpolate):
   - Time-based updates for game flags, wave timing, sun angle
   - Camera state integration: smooths directional offset, integrates eye height/rotation from velocities, decays screen shake
   - Checks for death condition (armor <= 0)
   - Calls Interpolate() on player and collections
   - Visual smoothing for movement and object interpolation

2. **PostRender Phase** (WriteFramePostRender):
   - Camera input processing to set eye rotation and height velocities for next frame integration
   - Calls PostRender() on player and collections for input-driven logic
   - Collision detection via Collide() methods
   - Spawn phase via WriteFramePostRenderSpawn(): Wave spawning logic, pickup collection, new object creation
   - Destroy phase via WriteFramePostRenderDestroy(): Wave cleanup, death handling, object removal

## See Also
- Base engine frame: [../../../../Engine/Source/Frame/CLAUDE.md](../../../../Engine/Source/Frame/CLAUDE.md)
- Game object collections: [Collections/CLAUDE.md](Collections/CLAUDE.md)
