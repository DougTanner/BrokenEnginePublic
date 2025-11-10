# /Projects/BrokenEngineSandbox/Source/Frame/Collections/

Game-specific object collections for space combat. Manages projectiles, missiles, and enemies using the engine's spawn request pattern and Structure-of-Arrays layout.

## Architecture Overview

**Collection Pattern**: Each collection combines thread-safe spawn requests with fixed-size SOA storage for active objects, following the engine's UpdateList interface.

**Memory Layout**: Structure of Arrays (SOA) for cache-friendly iteration - position arrays, velocity arrays, etc. rather than array of structs. Enables SIMD optimization and reduces cache misses during parallel updates.

**Lifecycle Management**: Objects created via spawn requests, updated through frame phases, destroyed when flagged. Slots reused to avoid dynamic allocation.

## Core Collections

### Blasters.h/cpp

Fast-moving energy projectile system for player and enemy weapons.

**Purpose**: Manages rapid-fire projectiles with visual effects, collision detection, and sound.

**Key Responsibilities**:
- Position and velocity updates with decay over time (size, brightness, damage fade)
- Terrain and object collision detection with different behavior for player vs enemy blasters
- Visual effects via area lights and particle textures
- Audio management with distance-based culling
- Freeze and slow effects from area damage

**Update Phases**:
- **Camera**: Position updates, aging, decay processing
- **Interpolate**: Smooth movement for rendering
- **PostRender**: Audio updates based on camera distance
- **Collide**: Terrain and object collision with appropriate hit reactions
- **Spawn**: Process spawn requests, initialize new projectiles
- **Destroy**: Clean up expired or collided projectiles

**Design Decisions**: Separate collision flags allow player and enemy blasters to hit different targets. Velocity-based sizing creates motion blur effect. Decay system allows graceful fade-out rather than instant disappearance.

**Capacity**: 2048 active projectiles, 512 spawn requests per frame.

### Missiles.h/cpp

Guided missile system with homing behavior and explosive damage.

**Purpose**: Manages homing projectiles that track targets and explode on impact or proximity.

**Key Responsibilities**:
- Target tracking with smooth steering rather than instant rotation
- Propulsion with delayed acceleration for launch effect
- Proximity detection for detonation
- Explosion damage in radius with directional option
- Trail particle effects and audio
- Self-targeting for counter-missile mechanics

**Update Phases**:
- **Camera**: Position updates, target tracking, steering calculations
- **Interpolate**: Smooth rotation and movement
- **PostRender**: Homing logic, acceleration, audio updates
- **Collide**: Direct hit and proximity detonation detection
- **Spawn**: Process spawn requests with target assignment
- **Destroy**: Clean up destroyed or exploded missiles

**AI Behavior**: Missiles use jittering steering for realistic flight rather than perfect tracking. Delayed rotation and acceleration create a launch sequence feel. Targets can be lost if destroyed or out of range.

**Capacity**: 256 active missiles, 64 spawn requests per frame.

### Spaceships.h/cpp

Enemy spacecraft with AI behavior, weapons, and health.

**Purpose**: Manages AI-controlled enemies that navigate terrain, attack the player, and respond to damage.

**Key Responsibilities**:
- Navigation using heightmap queries for terrain avoidance
- Formation spacing via pusher force fields
- Weapon firing with burst patterns
- Health management with visual damage feedback (trails)
- Death explosions and pickup spawning
- Wave-based difficulty scaling

**Update Phases**:
- **Camera**: AI decision-making, weapon firing, movement
- **Interpolate**: Smooth position and rotation
- **PostRender**: Pathfinding (multithreaded), terrain avoidance, pusher calculations
- **Collide**: Damage from player weapons, terrain collision
- **Spawn**: Wave-based spawning with positioning
- **Destroy**: Death sequence, pickup drops

**AI Systems**:
- Flee behavior when damaged or player too close
- Return to play area if wandering too far
- Terrain avoidance using island heightmap queries
- Formation maintenance via pusher repulsion forces
- Target leading for weapon accuracy

**Performance**: Terrain avoidance and pusher calculations run multithreaded across worker threads for efficiency with large enemy counts.

**Capacity**: 1024 active enemies with wave-based difficulty scaling.

## Common Patterns

### SOA Layout Organization

Collections separate data by access pattern:
- **Interpolate-phase data**: Read during rendering, updated during interpolation (positions, visual effects)
- **PostRender-phase data**: Modified during gameplay logic, not needed for rendering (velocities, AI state)
- Cache-aligned arrays prevent false sharing in parallel updates

### Collision System

Flags control collision behavior to prevent friendly fire and enable different damage models:
- Blasters distinguish player vs enemy projectiles
- Missiles distinguish player-seeking vs enemy-seeking
- Spaceships have flee/return-to-center behavioral states

### Spawn Request Pattern

Blasters and Missiles use `engine::Spawnable<>` for thread-safe deferred creation:
- Spawn requests accumulated during any update phase
- Processed in dedicated Spawn phase to avoid mid-update creation
- Buffer size prevents overflow from burst spawning

### Parallel Processing

Spaceships use `engine::Multithread<>()` for expensive operations:
- Terrain heightmap queries bucketed across threads
- Pusher force field calculations distributed
- Dynamic bucket sizing adapts to available cores

## Integration with Engine

**Engine Pool Dependencies**: Collections reference engine pool objects for visual and audio effects (area lights, trails, targets, pushers, sounds, billboards).

**Damage System**: Uses centralized damage constants from HealthDamage.h with wave-based scaling for enemy health progression.

**Targeting**: Missiles and spaceships use engine's target system for lock-on mechanics and UI indicators.

## See Also
- Engine collections base: [../../../../../Engine/Source/Frame/Collections/CLAUDE.md](../../../../../Engine/Source/Frame/Collections/CLAUDE.md)
- Engine object pools: [../../../../../Engine/Source/Frame/Pools/CLAUDE.md](../../../../../Engine/Source/Frame/Pools/CLAUDE.md)
- Parent frame: [../CLAUDE.md](../CLAUDE.md)
