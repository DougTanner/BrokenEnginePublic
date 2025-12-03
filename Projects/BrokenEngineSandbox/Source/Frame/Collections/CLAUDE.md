# /Projects/BrokenEngineSandbox/Source/Frame/Collections/

Game-specific object collections for space combat. Manages projectiles and enemies using phase-separated dynamic memory with Structure-of-Arrays layout.

## Architecture Overview

**Phase Separation**: Collections split into Interpolate (rendering state) and PostRender (logic state) structures for deterministic replay.

**Memory Layout**: SOA with dynamic allocation - contiguous buffers with 64-byte alignment for cache-friendly iteration and SIMD optimization.

**Lifecycle**: Objects spawn via requests, update through frame phases, and destroy when flagged. Dynamic capacity growth handles variable counts.

## Core Collections

### Blasters.h/cpp

Fast-moving energy projectiles. Uses shared BlasterType configuration for memory efficiency - player and spaceship types registered at initialization, spawn uses type index to look up configuration. Collision system uses two layers: player blasters (hit spaceships) and enemy blasters (hit player, marked with kCollidePlayer flag). Syncs positions to velocity-aligned area light quads for rendering. Spawns ControlledPointLight effects on terrain impact with 3-keyframe animation (flash → glow → fade out).

### Missiles.h/cpp

Guided missiles with homing AI and visual effects. Inherits from both `engine::Collection` and `engine::Renderable` mixin for GPU pipeline support with automatic buffer resizing. Implements full homing behavior with jitter, rotation delays, target tracking, and turn rate limits.

**Phase Separation**: `MissilesInterpolate` holds rendering state (positions, directions, owned object IDs, destroyed times). `MissilesPostRender` holds logic state (velocities, targets, AI parameters, acceleration).

**Owned Objects**: Each missile owns an area light (exhaust glow), pusher (air displacement), trail (smoke), and sound. These are synced via `IdToIndex` pattern in `Update()` (which takes `FrameInterpolate&` to access owned collections) and cleaned up in `Destroy()`.

**Registration Pattern**: `MissilesInterpolate::Register()` called from `Frame::Register()` pushes area light types directly to `engine::AreaLightsInterpolate::sTypes` for player and enemy exhaust visuals.

**Collision**: Uses `CollisionCategory::kBlaster` to collide with terrain and spaceships. Exploding missiles marked with `kAlreadyCollided` to prevent hit absorption.

**Sentinel Value Pattern**: Uses `pfDestroyedTimes` as sentinel (-1.0f = not exploding, > 0.0f = exploding countdown, 0.0f = ready for removal).

### Targets.h/cpp

Trackable world positions for missile guidance and AI awareness. Uses indexable collection pattern with `CollectionFlags::kIdToIndex` for stable IDs. Integrates with Billboards collection for visual indicators - billboard type registered once via `RegisterType()`, then referenced by index during Add(). Update() takes `FrameInterpolate&` to sync billboard positions from target positions.

**Subscriber Pattern**: Targets support multiple subscribers (e.g., missiles tracking the same target). Remove() decrements subscriber count or clears destination flag based on caller type. Target only destroyed when both conditions met: no destination flag AND zero subscribers. This prevents premature cleanup while missiles are still tracking.

### Spaceships.h/cpp

AI-controlled enemies with health, weapons, and behavior flags. Inherits from both `engine::Collection` and `engine::Renderable` mixin for GPU pipeline support with automatic buffer resizing. Pre-tags exploding spaceships with kAlreadyCollided so they don't absorb blaster hits. Renders with frustum culling and death shrink effects. Fires blasters at the player when facing them, using burst patterns with cooldowns.

**Sentinel Value Pattern**: Uses `pfDestroyedTimes` as a sentinel in Interpolate phase to avoid PostRender access during rendering: -1.0f = not exploding (spawn default), > 0.0f = exploding in progress (countdown), 0.0f = explosion finished (skip rendering, ready for removal). Render() accepts only `const FrameInterpolate&` to enforce phase separation.

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
