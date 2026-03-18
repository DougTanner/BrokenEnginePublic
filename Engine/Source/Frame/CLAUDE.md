# Frame - Core Game State and Fixed-Timestep Simulation

## Overview

Manages per-frame game state through a dual-phase update system (Interpolate for rendering, PostRender for logic) over a sparse grid of simulation cells. Frame state uses composition: `game::Frame` aggregates engine base classes (`FrameInterpolateBase`, `FramePostRenderBase`) with game-specific extensions. All collections support automatic CRC, serialization, and comparison via compile-time type lists.

See also: [Frame Update Pipeline](../../../Documents/Architecture/FrameUpdatePipeline.md)

## Key Classes

- **FrameInterpolateBase** - Rendering-phase state: frame timing, visual collections (`#ifdef BT_CLIENT`), and render pipeline orchestration. Provides `Crcs()`/`ServerRead()`/`LogDifferences()` for cross-build determinism validation
- **FramePostRenderBase** - Logic-phase state: deterministic random engine, UUID generation, area bounds, CRC tracking, and all PostRender collections. Orchestrates seven sub-phases (Update, PreCollision, PostCollision, AreaDamage, Transfer, Destroy, Spawn)
- **FrameUtils** - Template utilities using `std::apply` and fold expressions to iterate all collections automatically for CRC, serialization, and copy
- **GridCoord** - 2D coordinate keying frames in the sparse grid, with key packing and neighbor offset helpers
- **TimeStep** - Fixed timestep accumulator converting variable render time into discrete physics ticks at `kiTickRate`. Includes time scaling and death spiral prevention. Signals time scale changes via `mbTimeScaleChanged` flag; Game polls and updates the text overlay
- **Collision** - Layer-based spatial partitioning with discrete and swept sphere tests. Uses `thread_local` statics for parallel per-frame execution. Results stored in flat contiguous workbuffer-backed storage with O(1) generation-counter deduplication
- **AreaDamage** - Thread-local accumulator for explosion/AoE damage sources. Collections call `Add()` in PostCollision and query `Get()` during the AreaDamage phase; `Clear()` resets the list each frame
- **Alignments** - Sparse collision filtering via sorted flat vector with binary search
- **IslandTerrain** - Stateless CPU terrain collision queries (heightmap elevation, normals) shared by client and server

## Architecture Notes

- **Dual CRC system**: `Crcs()` returns a `std::pair<crc_t, crc_t>` — first is the full CRC (all collections), second is the server CRC (server-only collections) — both computed together and cached after update phases complete
- **FrameFlags** bitmask tracks update phase and prevents duplicate side effects (e.g., `kRecalculated` guards audio/particle replay during reconciliation)
- **Client/server split**: Visual-only collections (lights, billboards, sounds, trails) are `#ifdef BT_CLIENT`; collection counts adjust automatically
- **No transient metadata in Frame structs** -- only logical state belongs here; derived data goes in lookup infrastructure

## See Also

- [Collections/CLAUDE.md](Collections/CLAUDE.md) - SOA collection structures and spawn management
