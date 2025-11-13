# /Projects/BrokenEngineSandbox/Source/Frame/

Game-specific frame state and core game systems. Extends the engine's FrameBase with game logic for space combat.

## Architecture Overview

**Frame Structure**: Contains two sub-structures - FrameInterpolate (extends FrameBaseInterpolate) and FramePostRender (extends FrameBasePostRender) - providing game-specific state for the update pipeline.

**Update Pattern**: Follows engine's two-phase update system (Interpolate → PostRender) with player and game object collections participating in each phase.

**Simulation Rate**: 30Hz timestep (defined in Frame.h as kUpdateStepNs and kfDeltaTime) provides responsive gameplay while allowing complex AI and physics calculations.

## Core Files

### Frame.h/cpp

Main game frame structure containing all game state and frame orchestration.

**Purpose**: Aggregates all game-specific state into a single serializable structure that extends the engine's FrameBase.

**Frame Sub-Structures**:
- `FrameInterpolate` - Wave system state, spawn timing, game mode flags, player, dynamic object collections (blasters, missiles, spaceships), and input-dependent camera state (extends FrameBaseInterpolate)
- `FramePostRender` - Inherits navmesh from engine, future expansion point (extends FrameBasePostRender)

**Camera State in FrameInterpolate**: Contains deterministic, input-dependent camera parameters for replay support: smoothed directional offset from player input, eye height and rotation, screen shake intensity, and velocity accumulators for smooth integration.

**Key Static Methods**:
- Enemy spawning and targeting systems for AI
- Missile target acquisition with lock-on logic
- Area damage application across multiple object types
- Pickup spawning with probability-based drops
- Game state transitions (death screen, wave completion)

**Helper Functions**: Template functions for applying damage and slow effects to collections, island flip transformations, health scaling based on wave progression.

**Design Pattern**: Struct-of-Arrays (SOA) layout for all collections enables efficient SIMD processing and cache-friendly iteration.

### Player.h/cpp

Player spaceship controller with combat abilities and health management.

**Purpose**: Manages player state, movement, weapons, and abilities through the three-phase update system.

**Update Responsibilities**:
- **Interpolate**: Smooth movement, ability visual effects, dash mechanics
- **PostRender**: Input-driven weapon firing, ability activation, stat management
- **Collide**: Damage detection from enemy projectiles and area attacks
- **Spawn**: Pickup collection for health/missiles
- **Destroy**: Death sequence and respawn logic

**Combat Systems**:
- Primary weapons (rapid-fire blasters)
- Secondary weapons (homing missiles with lock-on)
- Defensive abilities (hex shield with directional blocking, dash with invulnerability)
- Resource management (armor, shield, energy, missiles)

**Design Pattern**: Follows UpdateList interface to participate in frame update phases. Ability states managed via timers and cooldowns for deterministic replay support.

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
