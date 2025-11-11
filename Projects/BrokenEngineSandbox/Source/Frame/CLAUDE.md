# /Projects/BrokenEngineSandbox/Source/Frame/

Game-specific frame state and core game systems. Extends the engine's FrameBase with game logic for space combat.

## Architecture Overview

**Frame Structure**: Extends `engine::FrameBase` with three sub-structures (Global, Interpolate, Full) containing game-specific state.

**Update Pattern**: Follows engine's three-phase update system (Camera → Interpolate → PostRender) with player, camera, and game object collections participating in each phase.

**Simulation Rate**: 30Hz timestep (defined in Frame.h as kUpdateStepNs and kfDeltaTime) provides responsive gameplay while allowing complex AI and physics calculations.

## Core Files

### Frame.h/cpp

Main game frame structure containing all game state.

**Purpose**: Aggregates all game-specific state into a single serializable structure that extends the engine's FrameBase.

**Frame Sub-Structures**:
- `FrameCamera` - Wave system state, spawn timing, game mode flags, time tracking (extends FrameBaseCamera)
- `FrameInterpolate` - Camera, player, and dynamic object collections (blasters, missiles, spaceships) (extends FrameBaseInterpolate)
- `FramePostRender` - Currently inherits navmesh from engine, future expansion point (extends FrameBasePostRender)

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
- **Camera**: Position updates based on time
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

### Camera.h/cpp

Third-person camera system that follows the player with smooth interpolation.

**Purpose**: Provides dynamic viewpoint that tracks player while allowing user control of perspective.

**Update Responsibilities**:
- **Camera**: Follows player position
- **Interpolate**: Smooths camera movement with spring damping for lag effect
- **PostRender**: Processes user input for height and rotation adjustments
- Empty phases (Collide, Spawn, Destroy) required by UpdateList interface

**Camera Behavior**: Uses spring-damper system for smooth following with configurable lag. Screen shake applied during combat for feedback. Height and rotation user-controllable for personalized viewing angle.

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

The game follows the engine's three-phase update pattern with game-specific implementations:

1. **Camera Phase** (WriteFrameCamera):
   - Time-based updates for game flags, wave timing, sun angle
   - Calls Global() on player, camera, and collections
   - Updates ability cooldowns, weapon timers, wave spawning logic

2. **Interpolate Phase** (WriteFrameInterpolate):
   - Checks for death condition (armor <= 0)
   - Calls Interpolate() on player, camera, and collections
   - Visual smoothing for movement, camera following, object interpolation

3. **PostRender Phase**:
   - **PostRender** (WriteFramePostRender): Currently empty, future expansion point
   - **Collide**: Player damage detection, projectile collisions
   - **Spawn** (WriteFramePostRenderSpawn): Wave spawning logic, pickup collection, new object creation
   - **Destroy** (WriteFramePostRenderDestroy): Wave cleanup, death handling, object removal

**Update Order**: Player → Camera → Collections. Camera depends on player position, other systems depend on camera's visible area calculation.

## See Also
- Base engine frame: [../../../../Engine/Source/Frame/CLAUDE.md](../../../../Engine/Source/Frame/CLAUDE.md)
- Game object collections: [Collections/CLAUDE.md](Collections/CLAUDE.md)
