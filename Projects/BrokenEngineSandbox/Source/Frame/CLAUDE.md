# /Projects/BrokenEngineSandbox/Source/Frame/

Game-specific frame state and core game systems. Extends the engine's FrameBase with game logic for space combat.

## Architecture Overview

**Phase-Separated Structure**: Frame contains FrameInterpolate and FramePostRender sub-structures for strict separation between rendering state and logic state, enabling deterministic replay.

**Hierarchical Composition**: Each level extends corresponding engine base structures and aggregates game-specific collections (Player, Blasters, Spaceships).

**Simulation Rate**: 30Hz timestep provides responsive gameplay with deterministic physics.

## Core Files

### Frame.h/cpp

Aggregates game-specific state into a fully serializable structure with strict phase separation. Orchestrates the two-phase update pattern: Interpolate phase for rendering state (positions, directions) and PostRender phase for logic state (velocities, health, AI). Manages collision flow by dispatching PreCollision/PostCollision to collections and calling Collision::Collide() between them.

### Player.h/cpp

Player spaceship controller with phase-separated state. Handles input processing, weapon firing with cooldowns, and collision response. Terrain collision pushes player away from elevated terrain and reflects velocity for bouncy response. Entity collision implements shield/armor damage system with shield regeneration after cooldown and penetration mechanics. Registers blaster type configuration during initialization for memory-efficient projectile spawning.

### HealthDamage.h

Combat balance constants and collision system configuration. Defines CollisionCategory (what am I?), CollisionMask (what can I hit?), and CollisionFlags (behavior modifiers like destroy-on-collide). Includes separate collision masks for player blasters (hit spaceships) and enemy blasters (hit player).

## Wave Spawning System

Manages progressive difficulty scaling through wave-based enemy spawning. Wave state tracked in FrameInterpolate for deterministic replay. Clump mechanics divide spawns over time to prevent overwhelming the player.

## Initialization Flow

During game startup, Frame implements two initialization phases:
1. **Register Phase**: `FrameInterpolate::Register()` calls static `Register()` on all collection Interpolate structs for type registration. Each collection's Register() also calls `RegisterGraphicsResources()` to self-register its GraphicsResources callback.
2. **Graphics Resources Phase**: `FrameInterpolate::GraphicsResources()` iterates the registered callback vectors (engine-level via parent call, game-level via `sGameGraphicsResourcesCallbacks`) to invoke all collection GraphicsResources() methods for GPU pipeline and buffer allocation.

## Update Flow

1. **Interpolate Phase**:
   - **Allocate**: `FrameInterpolate::Allocate()` calls `ReallocateAndCopyMetadata()` for all game collections
   - **Update**: Integrates velocities into positions, syncs owned objects to engine collections (area lights, billboards, sounds, trails)
2. **PostRender Phase**:
   - **Allocate**: `FramePostRender::Allocate()` calls `ReallocateAndCopyMetadata()` for all game PostRender collections
   - **Update**: Processes input and AI logic, runs collision detection, applies damage/destruction, spawns new objects

## See Also
- Base engine frame: [../../../../Engine/Source/Frame/CLAUDE.md](../../../../Engine/Source/Frame/CLAUDE.md)
- Game object collections: [Collections/CLAUDE.md](Collections/CLAUDE.md)
