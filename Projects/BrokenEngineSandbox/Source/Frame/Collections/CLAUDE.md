# /Projects/BrokenEngineSandbox/Source/Frame/Collections/

Game-specific object collections for space combat. Manages projectiles and enemies using phase-separated dynamic memory with Structure-of-Arrays layout.

## Architecture Overview

**Initialization Phases**: All Interpolate structs have two static initialization methods called during game startup:
- **`Register()`** - Type registration and configuration (e.g., area light types, explosion types, smoke trail types).
- **`GraphicsResources()`** - Collections that render create their GPU buffers and pipelines; non-rendering collections have empty implementations.

## Core Collections

### Blasters.h/cpp

Fast-moving energy projectiles with shared BlasterType configuration for memory efficiency. Terrain impacts spawn visual and audio effects (crater light with 4-keyframe flash/glow/fade, smoke puff, impact sound).

Each blaster owns an area light and sound via the Sync pattern. Wind trails are gated per-instance at spawn time, allowing callers to control which blasters produce wind (e.g., player blasters deposit wind, enemy blasters do not). Uses swept sphere collision testing to prevent tunneling through targets at high velocities, with alignment-based friend/foe filtering and a single `kBlaster` collision category.

### Missiles.h/cpp

Guided missiles with homing AI and visual effects. Self-contained GPU model pipeline in the `.cpp` file. Implements homing behavior with jitter, rotation delays, target tracking, and turn rate limits. Rotation delay ramps up gradually via a delay percentage rather than jumping abruptly when the delay expires. Uses swept sphere collision testing to prevent tunneling at high velocities.

**Owned Objects**: Each missile owns an area light (exhaust glow with alternating width and randomized length for flicker), pusher (air displacement), smoke trail, and sound via `SyncMissile()` helper. Area lights and sounds are removed immediately on explosion; pushers and smoke trails are cleaned up in Destroy. Smoke trail ID is carried in `SpawnInfo` and `TransferData` to enable ID reuse across grid cell transfers for seamless trail rendering.

**Target Tracking**: Missiles check target existence each frame (handles spaceship death) and also check for cleared `kDestination` flag (subscriber-only edge case). When either triggers, the missile captures its current direction as a stored heading and orients toward it. Untargeted missiles orient toward their stored direction rather than a target position.

**Area Damage**: Explosions spawn three simultaneous blasts at full, half, and quarter size and register area damage via the collision system. Supports directional explosions for terrain impacts (narrower trail/particle angles).

**Sentinel Value Pattern**: `pfDestroyedTimes` encodes state: -1.0f = active, > 0.0f = exploding countdown, 0.0f = ready for removal.

### Targets.h/cpp

Trackable world positions for missile guidance. Uses `CollectionFlags::kIdToIndex` for stable ID-based references. Alignment-filtered so missiles only lock onto enemy targets.

**Subscriber Pattern**: Multiple missiles can track the same target. Remove with `kDestination` flag (spaceship dying) immediately destroys the target regardless of subscriber count. Remove without flags (subscriber release) decrements the count and only destroys when zero subscribers remain.

### Spaceships.h/cpp

AI-controlled enemies with health, weapons, and behavior flags. Self-contained GPU model pipeline with per-instance skeletal animation. Fires blasters at the nearest alive player when facing them (visibility-gated with cooldown).

**Data-Driven Player Targeting**: Helper functions iterate the player collection to find alive players, maintaining Frame purity without querying Game.

**Per-Instance Skeletal Animation**: Parallelized render with main-thread visibility cull, bulk GPU buffer pre-allocation, and `Dispatch()` across the worker pool for animation evaluation into non-overlapping output slots.

**Owned Objects**: Each spaceship owns a pusher, target (for missile tracking with per-instance alignment), and wind trail. Targets are removed with `kDestination` flag when exploding begins.

**Terrain and Physics**: Terrain avoidance samples elevation ahead and to sides for steering. Terrain collision reflects velocity off the terrain normal. Receives push forces from nearby pushers (excluding self).

**Explosion Effects**: Death animation spawns staggered explosions at intervals with decreasing scale percentages for a cascading effect.

**Collision and Damage**: Per-instance alignment for collision filtering. Takes damage from blasters (PostCollision) and missiles (AreaDamage). Blaster knockback uses the blaster's velocity direction from collision results.

## Common Patterns

### Memory Management

- `engine::Allocate()` for buffer reallocation, `engine::GrowPairedCollections()` for paired collection growth, `engine::DestroyElement()` for O(1) swap-with-last removal
- Collections early-out when `iCount == 0`

### Static vs Dynamic Fields

- **Static fields**: Set once in Spawn(), copied via `std::memcpy()` in AllocateAndCopy (e.g., alignment, acceleration)
- **Sync-written fields**: Written by owners via `Sync()` every frame, also memcpy'd so interpolates always have valid data
- **Dynamic fields**: Modified during Update() via load/save pattern (e.g., velocity, health, timers)

### Cross-Cell Transfer

All four collections (Blasters, Missiles, Spaceships, Players) use a two-phase approach for cross-cell entity migration. PostCollision flags out-of-bounds entities with `kTransfer`. The dedicated Transfer phase generates `TransferRequest`s with full gameplay state for seamless continuity, removes owned objects, and destroys the entity. This separation ensures AreaDamage can skip transferring entities and prevents premature removal before all collision phases complete. Smoke trail IDs are preserved across transfers for continuous rendering.

### Adding New Members

Follow the 5-step pattern in the **add-collection-member** skill.

## See Also
- Engine collections: [../../../../../Engine/Source/Frame/Collections/CLAUDE.md](../../../../../Engine/Source/Frame/Collections/CLAUDE.md)
- Parent frame: [../CLAUDE.md](../CLAUDE.md)
