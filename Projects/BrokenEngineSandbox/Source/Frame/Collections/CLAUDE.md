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

Fast-moving energy projectiles. Uses shared BlasterType configuration for memory efficiency - player and spaceship types registered at initialization, spawn uses type index to look up configuration. Collision system uses two layers: player blasters (hit spaceships) and enemy blasters (hit player, marked with kCollidePlayer flag). Passes velocity data to collision system for directional effects on hit targets. Terrain impacts spawn visual and audio effects: crater light (4-keyframe flash → glow → fade), smoke puff, and one-shot impact sound.

**Owned Objects**: Each blaster owns an area light (visible glow) and sound (projectile audio). Uses Sync pattern: `AreaLightsInterpolate::Sync()` updates velocity-aligned quad positions, `SoundsInterpolate::Sync()` updates 3D audio position and velocity.

### Missiles.h/cpp

Guided missiles with homing AI and visual effects. Inherits from both `engine::Collection` and `engine::Renderable` mixin for GPU pipeline support with automatic buffer resizing. Implements full homing behavior with jitter, rotation delays, target tracking, and turn rate limits.

**Phase Separation**: `MissilesInterpolate` holds rendering state (positions, directions, owned object IDs, destroyed times). `MissilesPostRender` holds logic state (velocities, targets, AI parameters, acceleration, stored directions, explosion directions).

**Owned Objects**: Each missile owns an area light (exhaust glow), pusher (air displacement), trail (smoke), and sound. Uses Sync pattern via helper function `SyncMissile()`: `AreaLightsInterpolate::Sync()` for exhaust visuals with alternating width for flicker effect, `TrailsInterpolate::Sync()` for smoke trail with offset based on delta rotation, `SoundsInterpolate::Sync()` for engine audio, and `PushersInterpolate::Sync()` for air displacement. All cleaned up in `Destroy()`.

**Registration Pattern**: `MissilesInterpolate::Register()` registers two area light types (player and enemy exhaust), one trail type, and one explosion type. Called from game startup before GraphicsResources phase.

**Collision**: Collides with terrain and spaceships via collision layers. Self-destructs outside f4GlobalArea boundary. Exploding missiles marked with `kAlreadyCollided` in PreCollision to prevent hit absorption. Does not deal direct collision damage.

**Target Tracking**: During Update, missiles first check if their target still exists in `idToIndexMap` (handles immediate target removal when spaceship dies). If the target exists, they also check if the `kDestination` flag is cleared (edge case for subscriber-only targets). When either condition triggers, missiles clear their target reference, capture the current direction as the stored direction, and continue orienting toward that heading. Untargeted missiles use stored direction for orientation instead of target position. In Destroy(), missiles check target existence before calling Remove() to handle force-removed targets gracefully.

**Area Damage**: When exploding via `Explode()`, spawns visual explosion effect and registers area damage via `Collision::AddAreaDamage()` with position, radius, damage, and kMissile category. Supports directional explosions for terrain impacts.

**Sentinel Value Pattern**: Uses `pfDestroyedTimes` as sentinel (-1.0f = not exploding, > 0.0f = exploding countdown, 0.0f = ready for removal).

### Targets.h/cpp

Trackable world positions for missile guidance and AI awareness. Uses indexable collection pattern with `CollectionFlags::kIdToIndex` for stable IDs. Inherits from `engine::TypeRegistry<TargetsType>` for type storage, but has custom `RegisterType()` in TargetsPostRender that also registers a corresponding billboard type for visual indicators.

**Sync Pattern**: Implements `TargetsInterpolate::Sync()` with SyncData (vecPosition, uiTypeIndex). Sync() writes own fields and conditionally calls `BillboardsInterpolate::Sync()` only when the billboard is valid (exists). Parent collections (Spaceships) call `TargetsInterpolate::Sync()` and don't need to know about the billboard grandchild.

**Subscriber Pattern**: Targets support multiple subscribers (e.g., missiles tracking the same target). Billboards are created lazily when the first subscriber is added via `AddSubscriber()`, making the target visible only when actively tracked. When the last subscriber leaves via `Remove()`, the billboard is destroyed, making the target invisible again. Remove() has two paths based on flags: when called with `kDestination` (spaceship dying), the Target and Billboard are immediately destroyed regardless of subscriber count; when called without flags (subscriber release), the subscriber count is decremented and the target is destroyed only when both no destination flag AND zero subscribers remain.

### Spaceships.h/cpp

AI-controlled enemies with health, weapons, and behavior flags. Inherits from both `engine::Collection` and `engine::Renderable` mixin for GPU pipeline support with automatic buffer resizing. Pre-tags exploding spaceships with kAlreadyCollided so they don't absorb blaster hits. Renders with frustum culling, death shrink effects, roll animation during turns, and freeze color tint when hit. Fires blasters at the player when facing them (visibility-gated: only fires when within player's visible area), using burst patterns with cooldowns.

**Phase Separation**: `SpaceshipsInterpolate` holds rendering state (positions, directions, destroyed times, owned IDs, delta rotations, freeze times). `SpaceshipsPostRender` holds logic state (flags, velocities, health, blaster spawn timing).

**Owned Objects**: Each spaceship owns a pusher (air displacement) and target (for missile tracking). Target type registered at static initialization via `TargetsPostRender::RegisterType()` which also registers the corresponding billboard type. Targets created via `TargetsPostRender::Add()` in Spawn, synced via `TargetsInterpolate::Sync()` in Interpolate::Update (which automatically syncs the owned billboard grandchild), and removed via `TargetsPostRender::Remove()` when exploding starts. Pushers synced via `UpdatePosition()` in PostRender::Update.

**Terrain Systems**: `AvoidTerrain()` samples terrain elevation ahead and to sides, adjusting rotation to steer away from obstacles. Terrain collision bounce reflects velocity off terrain normal and applies position correction.

**Bounds and Damage**: Auto-destroys when outside f4GlobalArea boundary (same pattern as Missiles/Blasters). Takes damage from player blasters (via PostCollision) and missile explosions (via AreaDamage phase). When destroyed by blasters, uses the blaster's velocity from collision results for knockback direction. When health reaches zero or leaving bounds, sets kExploding flag, removes target via `Remove()` with `kDestination` flag, and invalidates the target ID to stop syncing.

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
