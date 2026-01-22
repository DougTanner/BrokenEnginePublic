# /Engine/Source/Frame/

Core game state management with deterministic dual-buffered frame system and 250Hz fixed timestep updates.

## Overview

Manages frame state through a two-phase update system (Interpolate/PostRender) that separates rendering preparation from game logic. GameBase orchestrates dual-buffered frames (Current/Next) with swap-based updates for determinism.

## Key Systems

**TimeStep** - Fixed timestep accumulator converting variable render time into discrete 250Hz physics steps. Provides time scaling for slow-motion/fast-forward effects and interpolation alpha for smooth rendering between physics ticks. Time scale adjustment methods (`ReduceTimeScale`, `DecreaseTimeScale`, `IncreaseTimeScale`) handle both the scaling logic and debug text display updates. Includes death spiral prevention that automatically reduces time multiplier and clamps the accumulator when excessive updates are detected (debug builds only).

**FrameInterpolateBase** - Time-based state for the Interpolate phase. Contains frame counter, simulation time (`fCurrentTime`, `fDeltaTime`), and engine-level collections (AreaLights, Billboards, Explosions, HexShields, PointLights, Puffs, Pushers, Sounds, Trails). Provides `Collections()` method using C++23 deduced `this` to return a tuple of all collections, enabling automatic iteration via `std::apply` with fold expressions for CRC, serialization, and other bulk operations. Static methods: Register(), GraphicsResources(), AllocateAndCopy(), Update(), Render(). Game-specific classes extend this base.

**FramePostRenderBase** - Logic-phase state for PostRender phase. Contains deterministic random engine, per-Frame UUID generator, and vecArea (bounds for object destruction). UUID generation uses Frame ID (high 16 bits) combined with a counter (low 48 bits) to ensure uniqueness across multiple Frames without atomics. Provides `Collections()` method (same pattern as FrameInterpolateBase) for automatic collection iteration. Static methods orchestrate the update sub-phases: Update(), PreCollision(), PostCollision(), AreaDamage(), Destroy(), Spawn(). Game-specific classes extend this base.

**Collision** - Layer-based collision detection with zone-based spatial partitioning. Collections register layers in PreCollision, query results in PostCollision. Supports collision group filtering via dynamic `CollisionGroups` matrix stored in FramePostRenderBase. Groups can be per-object (`pGroups` array) or uniform per-layer (`uniformGroup`). Also provides area damage system for explosions with linear falloff queries.

**CollisionGroups** - Dynamic collision group matrix for runtime-configurable collision filtering. Uses `collision_group_t` (stable IDs via `id_t` pattern) to identify groups. Groups are added/removed dynamically with automatic matrix compaction. The `SetCanCollide()` method configures which groups can collide with each other. Uses `uint8_t` matrix storage for batch I/O operations. Stored in FramePostRenderBase for serialization with save/load support. Games register groups at initialization and restore global references when loading saves.

**Render** - Frame rendering orchestration populating GPU uniform buffers for shaders. `RenderFrameGlobal()` sets global uniforms (sun direction/color, shadows, terrain, water waves). `RenderFrameMain()` sets per-frame uniforms (camera matrices, wave parameters, hex shields). Includes day/night cycle helpers (DayPercent/NightPercent) and shared utilities for lighting collections (`IsPointVisible`, `ProjectToBaseHeight`, `BuildAxisAlignedQuad`). `RenderObjects()` batch renders with visibility culling and transform setup. Sun angle override applies when in Graphics UI or ImGui overlay (`mbShowImGui`). Smoke simulation controlled via `gbSmokeClear`/`gbSmokeSpread` flags.

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

## See Also
- [Collections/CLAUDE.md](Collections/CLAUDE.md) - SOA collection structures and spawn management
