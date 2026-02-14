# /Engine/Source/Frame/

Core game state management with deterministic dual-buffered frame system and 250Hz fixed timestep updates.

## Overview

Manages frame state through a two-phase update system (Interpolate/PostRender) that separates rendering preparation from game logic. GameBase orchestrates dual-buffered frames (Current/Next) with swap-based updates for determinism.

## Key Systems

**TimeStep** - Fixed timestep accumulator converting variable render time into discrete 250Hz physics steps. Provides time scaling for slow-motion/fast-forward effects and interpolation alpha for smooth rendering between physics ticks. Time scale adjustment methods (`DecreaseTimeScale`, `IncreaseTimeScale`) handle both the scaling logic and debug text display updates using `common::gpThreadLocal->mWorkbuffer` for allocation-free string building, calling `Pop()` after each usage. Includes death spiral prevention that automatically reduces time multiplier and clamps the accumulator when excessive updates are detected (debug builds only).

**FrameUtils** - Template utilities for data-driven collection iteration. Defines `TypeList<TS...>` for compile-time type lists and `InterpolateTypes`/`PostRenderTypes` type aliases listing all engine collections. Provides `ForEach*` helper functions that use fold expressions to invoke static methods (Register, GraphicsResources, Update, Render, PreCollision, PostCollision, AreaDamage, Destroy, Spawn) on all collection types. Also provides `AllocateAndCopyCollections` and `CompareCollections` helpers for tuple-based bulk operations.

**FrameInterpolateBase** - Time-based state for the Interpolate phase. Contains frame counter, simulation time, and engine-level collections (AreaLights, Billboards, Explosions, HexShields, PointLights, Puffs, Pushers, Sounds, Trails, WindDeposits). Provides `Collections()` method using C++23 deduced `this` to return a tuple of all collections, enabling automatic iteration via `std::apply` with fold expressions for CRC, serialization, and equality comparison. Static methods use ForEach helpers from FrameUtils for data-driven dispatch. Game-specific classes extend this base.

**FramePostRenderBase** - Logic-phase state for PostRender phase. Contains deterministic random engine, per-Frame UUID generator, and vecArea (bounds for object destruction). UUID generation uses Frame ID (high 16 bits) combined with a counter (low 48 bits) to ensure uniqueness across multiple Frames without atomics. Provides `Collections()` method (same pattern as FrameInterpolateBase) for automatic collection iteration. Static methods use ForEach helpers from FrameUtils to orchestrate the update sub-phases. Game-specific classes extend this base.

**Collision** - Layer-based collision detection with fixed-size grid spatial partitioning. Collections register layers in PreCollision, query results in PostCollision. `Collide()` takes the frame's `vecArea` bounds to compute zone dimensions dynamically. All major containers use pre-allocation + count patterns to avoid per-frame heap allocations: `sLayers` (pre-allocated with `kiCollisionLayerPreallocate`), `sLayerPairZones` (pre-allocated with `kiCollisionLayerPairPreallocate`), and `sAreaDamageSources` (pre-allocated with `kiAreaDamageSourcePreallocate`) are each tracked by count variables (`siLayerCount`, `siLayerPairCount`, `siAreaDamageSourceCount`), using count-based reset instead of clearing/reallocating each frame. Growth beyond pre-allocated capacity triggers `DebugBreak()` before resizing to flag unexpected capacity needs during development. Zone grids within each `LayerPairZones` use a fixed `kiCollisionZonesY` x `kiCollisionZonesX` 2D array of `ZonePair` structs (pre-allocated with `kiCollisionZonePreallocate` capacity per side), with count-based reuse instead of clearing/reallocating each frame. Zone structures are built per colliding layer pair, eliminating layer filtering during zone iteration. Collision masks must be bi-directional (enforced via assert): if layer A's mask includes layer B's category, layer B's mask must include layer A's category. Same-layer collision is asserted as not configured (implementation would require self-collision avoidance and different loop structure). Supports alignment-based filtering via sparse relationship map stored in `Alignments` struct. CollisionLayer requires per-object arrays for radii, damages, and flags - collections maintain static vectors populated in PreCollision (with `ScopedSuppressAllocationTracking` to exclude expected allocation noise). Invalid alignment (0) collides with everything. `CollideLayerPair` uses `common::gpThreadLocal->mWorkbuffer` for temporary per-object duplicate tracking via `PushBuffer<bool*>()`, avoiding per-frame heap allocations. Also provides area damage system for explosions with linear falloff queries.

**Alignment** - Sparse relationship map for collision filtering between aligned objects. Uses `alignment_t` (simple 32-bit ID wrapper) to identify groups. Relationships are stored as sorted `AlignmentPair` entries in a flat vector, where each key concatenates two alignment IDs (lower first) and flags indicate the relationship (kEnemies for hostile, kAllies for friendly). Binary search via `std::lower_bound` provides O(log n) lookup. `CopyFrom()` uses a memcpy fast path when source and destination sizes match, avoiding heap allocation during frame copies. Alignment state is owned by the game layer and passed to `Collision::Collide()`.

**Render** - Frame rendering orchestration populating GPU uniform buffers for shaders. `RenderFrameGlobal()` sets global uniforms (sun direction/color, shadows, terrain, water waves, wind simulation parameters) using individually named scalar fields on GlobalLayout. `RenderWindGlobal()` uses a fixed timestep accumulator (`kfWindFixedStep = 1/60`) with spiral-of-death prevention (clamped to one step max) to decouple wind simulation rate from render framerate. Accumulates wind time using a static Timer scaled by `gWindTimeScale`, and writes all wind uniforms to the global layout including swirl speed (for animated noise sampling) and vorticity confinement strength. Wind uses a ping-pong pattern with four pipelines (WindClear, WindClearTwo, WindSpread, WindSpreadTwo) targeting TextureOne and TextureTwo respectively. `giWindTextureIndex` toggles (0/1) only on simulation step frames; `fWindTextureIndex` is a continuous blend factor (0.0-1.0) that interpolates between the two wind textures for smooth results between simulation steps. Wind deposit pipelines follow the same pattern via `kDynamicPipelineWindDeposit`/`kDynamicPipelineWindDepositTwo`. Wind spread quad offsets are only recomputed on step frames. `RenderFrameMain()` sets per-frame uniforms (camera matrices, wave parameters, hex shields), resets skinning buffer allocations via `BufferManager::ResetSkinningAllocations()`, then delegates to `FrameInterpolate::Render()` for collection rendering. Includes day/night cycle helpers (DayPercent/NightPercent) and shared utilities for lighting and wind collections (`IsPointVisible`, `ProjectToBaseHeight`, `BuildAxisAlignedQuad`). `RenderObjects()` batch renders with visibility culling and transform setup. Sun angle is obtained from `Camera::SunAngle()` which handles UI override logic internally. Smoke simulation controlled via `gbSmokeClear`/`gbSmokeSpread` flags. Wind simulation controlled via `gbWindClear` flag.

## Frame Update Flow

**Interpolate Phase**: Advances frame counter and simulation time, calls AllocateAndCopy() on all collections to prepare memory, then Update() to smooth positions for rendering.

**PostRender Phase** (six sub-phases):
1. **Update** - Input-driven logic, random state propagation
2. **PreCollision** - Collections register collision layers
3. **PostCollision** - Collections query collision results, apply damage
4. **AreaDamage** - Collections query explosion damage with falloff
5. **Destroy** - Clean up flagged objects
6. **Spawn** - Create new objects from spawn requests

## Architecture Notes

- Frame state uses composition: game::Frame aggregates FrameInterpolate and FramePostRender
- Each base provides equality comparison, CRC generation, and serialization for deterministic replay
- Collection serialization uses `Collections()` with `std::apply` and fold expressions to automatically iterate all collections without manual per-collection calls
- FrameType enum (kNone, kInterpolate, kPostRender) prevents duplicate side effects during interpolation
- IsVisible() helper provides axis-aligned visibility checks for AI targeting decisions
- **Load/Save local variable paradigm**: Frame update methods load previous frame state into locals, modify, then save to current frame. This pattern is only appropriate for trivially copyable types (scalars, XMVECTOR, small POD structs). Heap containers (std::vector, std::string, etc.) should use direct assignment or dedicated `CopyFrom()` methods to allow buffer reuse and avoid unnecessary allocations

## See Also
- [Collections/CLAUDE.md](Collections/CLAUDE.md) - SOA collection structures and spawn management
