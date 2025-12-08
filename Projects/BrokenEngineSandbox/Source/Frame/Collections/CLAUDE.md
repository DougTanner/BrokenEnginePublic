# /Projects/BrokenEngineSandbox/Source/Frame/Collections/

Game-specific object collections for space combat. Manages projectiles and enemies using phase-separated dynamic memory with Structure-of-Arrays layout.

## Architecture Overview

**Phase Separation**: Collections split into Interpolate (rendering state) and PostRender (logic state) structures for deterministic replay.

**Memory Layout**: SOA with dynamic allocation - contiguous buffers with 64-byte alignment for cache-friendly iteration and SIMD optimization.

**Lifecycle**: Objects spawn via requests, update through frame phases, and destroy when flagged. Dynamic capacity growth handles variable counts.

**Initialization Phases**: All Interpolate structs have two static initialization methods called during game startup:
- **`Register()`** - Called from `game::FrameInterpolate::Register()` for type registration and configuration (e.g., MissilesInterpolate registers area light types). Each collection's Register() also calls `FrameInterpolate::RegisterGraphicsResources()` to self-register its GraphicsResources callback.
- **`GraphicsResources()`** - Called via callback registration. Collections register their GraphicsResources callback during Register() phase, then `FrameInterpolate::GraphicsResources()` iterates the `sGameGraphicsResourcesCallbacks` vector to invoke them all. Renderable collections call AllocatePipelines() from the Renderable mixin; non-renderable collections have empty implementations.

## Core Collections

### Blasters.h/cpp

Fast-moving energy projectiles. Uses shared BlasterType configuration for memory efficiency - player and spaceship types registered at initialization, spawn uses type index to look up configuration. Collision system uses two layers: player blasters (hit spaceships) and enemy blasters (hit player, marked with kCollidePlayer flag). Terrain impacts spawn visual and audio effects: crater light (4-keyframe flash → glow → fade), smoke puff, and one-shot impact sound.

**Owned Objects**: Each blaster owns an area light (visible glow) and sound (projectile audio). Uses Sync pattern: `AreaLightsInterpolate::Sync()` updates velocity-aligned quad positions, `SoundsInterpolate::Sync()` updates 3D audio position and velocity.

### Missiles.h/cpp

Guided missiles with homing AI and visual effects. Inherits from both `engine::Collection` and `engine::Renderable` mixin for GPU pipeline support with automatic buffer resizing. Implements full homing behavior with jitter, rotation delays, target tracking, and turn rate limits.

**Phase Separation**: `MissilesInterpolate` holds rendering state (positions, directions, owned object IDs, destroyed times). `MissilesPostRender` holds logic state (velocities, targets, AI parameters, acceleration).

**Owned Objects**: Each missile owns an area light (exhaust glow), pusher (air displacement), trail (smoke), and sound. Uses Sync pattern: `AreaLightsInterpolate::Sync()` for exhaust visuals, `TrailsInterpolate::Sync()` for smoke trail, `SoundsInterpolate::Sync()` for engine audio. Pushers use `UpdatePosition()` since they only have position. All cleaned up in `Destroy()`.

**Registration Pattern**: `MissilesInterpolate::Register()` called from `Frame::Register()` pushes area light types directly to `engine::AreaLightsInterpolate::sTypes` for player and enemy exhaust visuals.

**Collision**: Collides with terrain and spaceships via collision layers. Exploding missiles marked with `kAlreadyCollided` to prevent hit absorption. Does not deal direct collision damage.

**Area Damage**: When exploding, registers an area damage source via `Collision::AddAreaDamage()` with position, radius, damage, and kMissile category. Damage is applied to spaceships during the AreaDamage phase with linear falloff.

**Sentinel Value Pattern**: Uses `pfDestroyedTimes` as sentinel (-1.0f = not exploding, > 0.0f = exploding countdown, 0.0f = ready for removal).

### Targets.h/cpp

Trackable world positions for missile guidance and AI awareness. Uses indexable collection pattern with `CollectionFlags::kIdToIndex` for stable IDs. Inherits from `engine::TypeRegistry<TargetsType>` for type storage, but has custom `RegisterType()` in TargetsPostRender that also registers a corresponding billboard type for visual indicators.

**Sync Pattern**: Implements `TargetsInterpolate::Sync()` with SyncData (vecPosition, uiTypeIndex). Sync() writes own fields and automatically calls `BillboardsInterpolate::Sync()` to update the owned billboard. Parent collections (Spaceships) call `TargetsInterpolate::Sync()` and don't need to know about the billboard grandchild.

**Subscriber Pattern**: Targets support multiple subscribers (e.g., missiles tracking the same target). Remove() decrements subscriber count or clears destination flag based on caller type. Target only destroyed when both conditions met: no destination flag AND zero subscribers. This prevents premature cleanup while missiles are still tracking.

### Spaceships.h/cpp

AI-controlled enemies with health, weapons, and behavior flags. Inherits from both `engine::Collection` and `engine::Renderable` mixin for GPU pipeline support with automatic buffer resizing. Pre-tags exploding spaceships with kAlreadyCollided so they don't absorb blaster hits. Renders with frustum culling, death shrink effects, roll animation during turns, and freeze color tint when hit. Fires blasters at the player when facing them, using burst patterns with cooldowns.

**Phase Separation**: `SpaceshipsInterpolate` holds rendering state (positions, directions, destroyed times, owned IDs, delta rotations, freeze times). `SpaceshipsPostRender` holds logic state (flags, velocities, health, blaster spawn timing).

**Owned Objects**: Each spaceship owns a pusher (air displacement) and target (for missile tracking). Target type registered at static initialization via `TargetsPostRender::RegisterType()` which also registers the corresponding billboard type. Targets created via `TargetsPostRender::Add()` in Spawn, synced via `TargetsInterpolate::Sync()` in Interpolate::Update (which automatically syncs the owned billboard grandchild), and removed via `TargetsPostRender::Remove()` when exploding starts. Pushers synced via `UpdatePosition()` in PostRender::Update.

**Terrain Systems**: `AvoidTerrain()` samples terrain elevation ahead and to sides, adjusting rotation to steer away from obstacles. Terrain collision bounce reflects velocity off terrain normal and applies position correction.

**Damage Sources**: Takes damage from player blasters (via PostCollision) and missile explosions (via AreaDamage phase). When health reaches zero, sets kExploding flag and removes target so missiles stop tracking.

**Sentinel Value Pattern**: Uses `pfDestroyedTimes` as a sentinel in Interpolate phase to avoid PostRender access during rendering: -1.0f = not exploding, > 0.0f = exploding countdown, 0.0f = ready for removal.

## Common Patterns

### Memory Management

- `engine::ReallocateAndCopyMetadata()` - Buffer reallocation in Allocate phase (called from `Frame::Allocate()` before Update)
- `engine::GrowPairedCollections()` - Capacity growth for paired Interpolate/PostRender collections
- `engine::SwapElement()` - O(1) unordered removal
- Collections check `pData == nullptr` early in Update() to skip processing when empty

### Serialization

Collections provide `Members()` returning `std::tie()` of SOA member pointers. Engine template functions use this for CRC generation, stream I/O, and buffer size calculation via fold expressions.

### Adding New Members

Follow the 5-step pattern in the **add-collection-member** skill: add to struct and Members(), equality comparison, load in Update(), save in Update(), initialize in Spawn().

## See Also
- Engine collections: [../../../../../Engine/Source/Frame/Collections/CLAUDE.md](../../../../../Engine/Source/Frame/Collections/CLAUDE.md)
- Parent frame: [../CLAUDE.md](../CLAUDE.md)
