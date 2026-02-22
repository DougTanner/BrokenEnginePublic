# /Projects/BrokenEngineSandbox/Source/Frame/Collections/

Game-specific object collections for space combat. Manages projectiles and enemies using phase-separated dynamic memory with Structure-of-Arrays layout.

## Architecture Overview

**Phase Separation**: Collections split into Interpolate (rendering state) and PostRender (logic state) structures for deterministic replay.

**Memory Layout**: SOA with dynamic allocation - contiguous buffers with 64-byte alignment for cache-friendly iteration and SIMD optimization.

**Lifecycle**: Objects spawn via requests, update through frame phases, and destroy when flagged. Dynamic capacity growth handles variable counts.

**Initialization Phases**: All Interpolate structs have two static initialization methods called during game startup:
- **`Register()`** - Called via `ForEachRegister(GameInterpolateTypes{})` from `game::FrameInterpolate::Register()` for type registration and configuration (e.g., MissilesInterpolate registers area light types).
- **`GraphicsResources()`** - Called via `ForEachGraphicsResources(GameInterpolateTypes{})` from `game::FrameInterpolate::GraphicsResources()`. Collections that render create their GPU buffers and pipelines directly; non-rendering collections have empty implementations.

## Core Collections

### Blasters.h/cpp

Fast-moving energy projectiles. Uses shared BlasterType configuration for memory efficiency - player and spaceship types registered at initialization, spawn uses type index to look up configuration. Has static `kName`/`kCrc` identifiers. Terrain impacts spawn visual and audio effects: crater light (4-keyframe flash -> glow -> fade), smoke puff, and one-shot impact sound.

**Wind Trail**: Each blaster owns a `wind_trail_t` ID and per-instance SOA fields for trail intensity, width, and length multiplier (`pfWindTrailIntensities`, `pfWindTrailWidths`, `pfWindTrailLengthMultipliers`), set at spawn time from `SpawnInfo`. Calls `WindTrailsInterpolate::Sync()` using these per-blaster values to update position, intensity, width, and length multiplier. Wind trails are gated per-instance at spawn time: only blasters spawned with `SpawnInfo::fWindTrailIntensity > 0.0f` create a wind trail, allowing callers to control which blasters produce wind (e.g., player blasters deposit wind while enemy blasters do not). Player blasters and spaceship blasters use separate wrapper globals for independent tuning of trail width, intensity, and length multiplier.

**Collision Architecture**: Single collision layer with unified `kBlaster` category. Each blaster stores an alignment (player alignment for player blasters, enemy alignment for spaceship blasters). The alignment system handles friend/foe filtering: blasters pass through objects with the same alignment but collide with enemies. PostCollision flags out-of-bounds blasters with `kTransfer`; the Transfer phase generates a `TransferRequest` with position, velocity, type index, alignment, and wind trail properties, then removes the entity. Static vectors (`sCollisionFlags`, `sCollisionRadii`, `sCollisionDamages`) are populated in PreCollision from per-object data and passed to the collision layer.

**Spawn Overloads**: Has two `Spawn()` overloads -- a parameterless version for weapon-fired spawning and a `SpawnInfo` version accepting position, velocity, type index, alignment, and wind trail properties for transfer-based spawning with preserved state.

**Owned Objects**: Each blaster owns an area light (visible glow) and sound (projectile audio). Uses Sync pattern: `AreaLightsInterpolate::Sync()` updates velocity-aligned quad positions, `SoundsInterpolate::Sync()` updates 3D audio position and velocity.

### Missiles.h/cpp

Guided missiles with homing AI and visual effects. Has static `kName`/`kCrc` identifiers and self-contained GPU pipeline and buffer management in its `.cpp` file (model pipeline with model CRC specified at runtime to avoid Data/Scene.h header dependency). Implements full homing behavior with jitter, rotation delays, target tracking, and turn rate limits. Rotation delay is applied in PostRender::Update by scaling the wanted delta rotation by the delay percentage before smoothing, causing missiles to gradually ramp up their rotation toward the target during the delay period rather than jumping abruptly when the delay expires.

**Phase Separation**: `MissilesInterpolate` holds rendering state (positions, directions, owned object IDs, destroyed times). `MissilesPostRender` holds logic state (velocities, targets, AI parameters, acceleration, stored directions, explosion directions).

**Owned Objects**: Each missile owns an area light (exhaust glow), pusher (air displacement), smoke trail, and sound. Uses Sync pattern via helper function `SyncMissile()`: `AreaLightsInterpolate::Sync()` for exhaust visuals with alternating width, randomized length for flicker effect, and intensity multiplier varying from 50% at minimum length to 100% at maximum length; `SmokeTrailsInterpolate::Sync()` for smoke trail with offset based on delta rotation; `SoundsInterpolate::Sync()` for engine audio; `PushersInterpolate::Sync()` for air displacement. Area lights and sounds are removed immediately in `Explode()`, while pushers and smoke trails are cleaned up in `Destroy()`.

**Registration Pattern**: `MissilesInterpolate::Register()` registers two area light types (player and enemy exhaust), one smoke trail type, and one explosion type. Called from game startup before GraphicsResources phase.

**Collision**: Collides with terrain and spaceships via collision layers. Uses `gPlayerAlignment` for alignment filtering. Exploding missiles marked with `kAlreadyCollided` in PreCollision to prevent hit absorption. Does not deal direct collision damage. PostCollision flags out-of-bounds missiles with `kTransfer`; the Transfer phase generates a `TransferRequest` with position, direction, velocity, alignment, acceleration, and gameplay timers (rotation delay, time, exhaust delay, jitter timer), then removes the entity. Static vectors (`sCollisionFlags`, `sCollisionRadii`, `sCollisionDamages`) are populated in PreCollision and passed to the collision layer.

**Spawn Overloads**: Has two `Spawn()` overloads -- a parameterless version for weapon-fired spawning and a `SpawnInfo` version accepting position, direction, velocity, stored direction, target, acceleration, flags, alignment, and gameplay timers (rotation delay, time, exhaust delay, jitter timer) for transfer-based spawning with preserved state. Transferred missiles lose their target and use stored direction for orientation until acquiring a new one.

**Target Tracking**: During Update, missiles first check if their target still exists in `idToIndexMap` (handles immediate target removal when spaceship dies). If the target exists, they also check if the `kDestination` flag is cleared (edge case for subscriber-only targets). When either condition triggers, missiles clear their target reference, capture the current direction as the stored direction, and continue orienting toward that heading. Untargeted missiles use stored direction for orientation instead of target position. In Destroy(), missiles check target existence before calling Remove() to handle force-removed targets gracefully.

**Area Damage**: When exploding via `Explode()`, spawns three simultaneous explosions at full, half, and quarter size (primary at exact position, secondary with small jitter) and registers area damage via `Collision::AddAreaDamage()` with position, radius, damage, and kMissile category. Supports directional explosions for terrain impacts (narrower trail/particle angles).

**Sentinel Value Pattern**: Uses `pfDestroyedTimes` as sentinel (-1.0f = not exploding, > 0.0f = exploding countdown, 0.0f = ready for removal).

### Targets.h/cpp

Trackable world positions for missile guidance and AI awareness. Uses indexable collection pattern with `CollectionFlags::kIdToIndex` for stable IDs. Inherits from `engine::TypeRegistry<TargetsType>` for type storage with custom `RegisterType()` in TargetsPostRender.

**Per-Instance Alignment**: Each target stores an `alignment_t` in PostRender, set at creation via `TargetsPostRender::Add()`. `Frame::GetMissileTarget()` uses `Alignments::CanCollide()` to filter targets by alignment, ensuring missiles only lock onto enemy targets.

**Sync Pattern**: Implements `TargetsInterpolate::Sync()` with SyncData (vecPosition, uiTypeIndex). Parent collections (Spaceships) call `TargetsInterpolate::Sync()` to update target positions.

**Subscriber Pattern**: Targets support multiple subscribers (e.g., missiles tracking the same target). `AddSubscriber()` increments the subscriber count when a missile locks on. Remove() has two paths based on flags: when called with `kDestination` (spaceship dying), the Target is immediately destroyed regardless of subscriber count; when called without flags (subscriber release), the subscriber count is decremented and the target is destroyed only when both no destination flag AND zero subscribers remain.

### Spaceships.h/cpp

AI-controlled enemies with health, weapons, and behavior flags. Uses the Spaceship model with per-instance skeletal animation. Has static `kName`/`kCrc` identifiers and self-contained GPU pipeline and buffer management in its `.cpp` file (model pipeline with model CRC specified at runtime to avoid Data/Scene.h header dependency). Pre-tags exploding spaceships with kAlreadyCollided so they don't absorb blaster hits. Renders with frustum culling, death shrink effects, roll animation during turns, and freeze color tint when hit. Fires single blasters at the nearest alive player when facing them (visibility-gated: only fires when within player's visible area), with a cooldown between shots. Blaster spawn passes spaceships-specific wind trail wrapper values (width, intensity, length multiplier) to each blaster instance.

**Data-Driven Player Targeting**: Spaceships use `NearestAlivePlayerPosition()` and `AnyAlivePlayerPosition()` helper functions that iterate the player collection to find non-exploding players, rather than hardcoding a player index or querying Game. This maintains Frame purity -- all player targeting is data-driven through the collections passed as function parameters.

**Phase Separation**: `SpaceshipsInterpolate` holds rendering state (positions, previous positions, directions, destroyed times, owned IDs, delta rotations, freeze times, animation times). `SpaceshipsPostRender` holds logic state (flags, velocities, damage directions, health, blaster spawn timing).

**Per-Instance Skeletal Animation**: Each spaceship tracks its own animation time, advanced in Interpolate::Update with looping via fmod. Render uses a parallelized two-pass approach: (1) main-thread visibility cull builds a compacted index list, (2) bulk pre-allocation of MeshData/JointMatrices via BufferManager for all visible spaceships in one call (using `AnimationData::SkinnedMaterialCount()` to compute joint allocation size), (3) parallel dispatch via `common::gpMultithreading->Dispatch()` which splits the visible range across the worker pool and main thread. Each worker calls `AnimationData::EvaluateAnimation()` to evaluate world matrices and all materials in a single call, writing to deterministic non-overlapping output slots in the shared GPU buffers.

**Owned Objects**: Each spaceship owns a pusher (air displacement), target (for missile tracking), and wind trail. Target type registered at static initialization via `TargetsPostRender::RegisterType()`. Targets created via `TargetsPostRender::Add()` in Spawn with the spaceship's per-instance alignment, synced via `TargetsInterpolate::Sync()` in Interpolate::Update, and removed via `TargetsPostRender::Remove()` when exploding starts. Pushers and wind trails synced via helper function `SyncSpaceship()` with spaceships-specific trail wrappers (width, intensity, length multiplier).

**Pusher Interaction**: Spaceships receive push forces from nearby pushers (e.g., missile exhaust) via `PushersInterpolate::ApplyPush()` during PostRender::Update. Each spaceship's own pusher is excluded to prevent self-push.

**Terrain Systems**: `AvoidTerrain()` samples terrain elevation ahead and to sides, adjusting rotation to steer away from obstacles. Terrain collision bounce reflects velocity off terrain normal and applies position correction.

**Explosion Effects**: Registers a custom `ExplosionType` via `ExplosionsInterpolate::RegisterType()` with spaceship-specific particle parameters (count, color, velocity, lighting). Death animation spawns staggered explosions at intervals, with per-instance scaling percentages (light, size, smoke, time, trail/particle counts) decreasing over the destroy timer for a cascading effect.

**Collision and Damage**: Each spaceship stores a per-instance `alignment_t` for alignment-based collision filtering. Takes damage from player blasters (via PostCollision) and missile explosions (via AreaDamage phase). When destroyed by blasters, uses the blaster's velocity from collision results for knockback direction. When health reaches zero, sets kExploding flag, removes target via `Remove()` with `kDestination` flag, and invalidates the target ID to stop syncing. PostCollision flags out-of-bounds spaceships with `kTransfer`; the Transfer phase generates a `TransferRequest` with position, direction, velocity, alignment, health, and blaster spawn timer, then removes the entity. AreaDamage skips entities flagged with `kTransfer`. Static vectors (`sCollisionFlags`, `sCollisionRadii`, `sCollisionDamages`) are populated in PreCollision and passed to the collision layer.

**Spawn Overloads**: Has two `Spawn()` overloads -- a parameterless version for wave spawning (random position/direction near alive players, default health from `HealthDamage.h`) and a `SpawnInfo` version accepting position, direction, velocity, alignment, health, and blaster spawn timer for transfer-based spawning with preserved state.

**Sentinel Value Pattern**: Uses `pfDestroyedTimes` as a sentinel in Interpolate phase to avoid PostRender access during rendering: -1.0f = not exploding, > 0.0f = exploding countdown, 0.0f = ready for removal.

## Common Patterns

### Memory Management

- `engine::Allocate()` - Buffer reallocation in Allocate phase (called from `Frame::Allocate()` before Update)
- `engine::GrowPairedCollections()` - Capacity growth for paired Interpolate/PostRender collections
- `engine::DestroyElement()` - O(1) unordered removal via swap-with-last
- Collections check `iCount == 0` early in Update() to skip processing when empty

### Serialization

Collections provide `Members()` returning `std::tie()` of SOA member pointers. Engine template functions use this for CRC generation, stream I/O, and buffer size calculation via fold expressions.

### Adding New Members

Follow the 5-step pattern in the **add-collection-member** skill: add to struct and Members(), equality comparison, load in Update(), save in Update(), initialize in Spawn().

### Static vs Dynamic Fields

- **Static fields**: Set once in Spawn(), never modified in Update(). Use `std::memcpy()` in AllocateAndCopy() after `engine::Allocate()`. Examples: alignment, acceleration, pitch.
- **Sync-written fields**: Written by owners via `Sync()` every frame. Also copied via `std::memcpy()` in AllocateAndCopy() so `MergeFramesForRender()` has valid data before Sync runs.
- **Dynamic fields**: Modified during Update() loop. Use load/save pattern. Examples: velocity, health, timers.

### Cross-Cell Transfer

All four collections (Blasters, Missiles, Spaceships, Players) use a two-phase approach for cross-cell entity migration. In PostCollision, each collection calls `ComputeFrameBounds()` once per pass, then `IsOutOfBounds()` per entity -- out-of-bounds entities are flagged with `kTransfer` rather than destroyed. In the dedicated Transfer phase, each collection's `Transfer()` method iterates entities looking for `kTransfer` flags, generates `TransferRequest`s (using `ComputeTransferDelta()` for the delta grid offset) that include gameplay timers for seamless state continuity, removes owned objects, and destroys the entity. This separation ensures AreaDamage can skip transferring entities and prevents premature removal before all collision phases complete.

## See Also
- Engine collections: [../../../../../Engine/Source/Frame/Collections/CLAUDE.md](../../../../../Engine/Source/Frame/Collections/CLAUDE.md)
- Parent frame: [../CLAUDE.md](../CLAUDE.md)
