# /Engine/Source/Frame/

Core game state management with deterministic dual-buffered frame system and 250Hz fixed timestep updates.

## Overview

Manages frame state through a two-phase update system (Interpolate/PostRender) that separates rendering preparation from game logic. GameBase orchestrates dual-buffered frames (Current/Next) with swap-based updates for determinism.

## Key Systems

**TimeStep** - Fixed timestep accumulator converting variable render time into discrete 250Hz physics steps. Provides time scaling for slow-motion effects and interpolation alpha for smooth rendering between physics ticks.

**FrameInterpolateBase** - Time-based state for the Interpolate phase. Contains frame counter, simulation time, sun angle, and engine-level collections (AreaLights, Billboards, Explosions, HexShields, PointLights, Puffs, Pushers, Sounds, Trails). Static methods: Register(), GraphicsResources(), AllocateAndCopy(), Update(), Render(). Game-specific classes extend this base.

**FramePostRenderBase** - Logic-phase state for PostRender phase. Contains deterministic random engine and UUID counter. Static methods orchestrate the update sub-phases: Update(), PreCollision(), PostCollision(), AreaDamage(), Destroy(), Spawn(). Game-specific classes extend this base.

**Collision** - Layer-based collision detection with zone-based spatial partitioning. Collections register layers in PreCollision, query results in PostCollision. Also provides area damage system for explosions with linear falloff queries.

**Render** - Frame rendering orchestration with day/night cycle helpers (DayPercent/NightPercent) and shared rendering utilities for lighting and smoke effects.

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
- FrameType enum (kNone, kInterpolate, kPostRender) prevents duplicate side effects during interpolation
- IsVisible() helper provides axis-aligned visibility checks for AI targeting decisions

## See Also
- [Collections/CLAUDE.md](Collections/CLAUDE.md) - SOA collection structures and spawn management
