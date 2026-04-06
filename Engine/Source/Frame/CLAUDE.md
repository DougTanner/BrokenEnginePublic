# Frame - Core Game State and Fixed-Timestep Simulation

## Overview

Manages per-frame game state through a dual-phase update system (Interpolate for rendering, PostRender for logic) over a sparse grid of simulation cells. Frame state uses composition: `game::Frame` aggregates engine base classes (`FrameInterpolateBase`, `FramePostRenderBase`) with game-specific extensions. All collections support automatic CRC, serialization, and comparison via compile-time type lists.

See also: [Frame Update Pipeline](../../../Documents/Architecture/FrameUpdatePipeline.md)

## Key Classes

- **FrameInterpolateBase** - Rendering-phase state: frame timing, visibility bounds, visual collections (`#ifdef BT_CLIENT`), and render pipeline orchestration. Provides CRC/serialization/`LogDifferences()` for cross-build determinism validation
- **FramePostRenderBase** - Logic-phase state: deterministic random engine, UUID generation (separate counters for shared/sound/visual UUIDs), CRC chain, alignments, and all PostRender collections. Orchestrates seven sub-phases (Update, PreCollision, PostCollision, AreaDamage, Transfer, Destroy, Spawn), each receiving a `const FrameStaticData&`
- **FrameStaticData** - Immutable per-coord data (area bounds, island configuration, and per-cell NavData) stored in `CoordFrames` alongside frames. Set once at coord creation, serialized separately from frames, and passed to all phase functions. NavData is built by the server from the island's canonical NavContour and distributed to clients via FrameStaticData network serialization. Also carries a `GridCoord` identifying the cell this data belongs to — set externally by the coord management layer and not included in serialization
- **FrameUtils** - Template utilities using `std::apply` and fold expressions to iterate all collections automatically for CRC, serialization, and copy
- **GridCoord** - 2D coordinate keying frames in the sparse grid, with key packing and neighbor offset helpers
- **TimeStep** - Fixed timestep accumulator converting variable render time into discrete physics ticks at `kiTickRate`. Includes time scaling and death spiral prevention. Signals time scale changes via `mbTimeScaleChanged` flag; Game polls and updates the text overlay
- **Collision** - Layer-based spatial partitioning with discrete and swept sphere tests. Uses `thread_local` statics for parallel per-frame execution. Results stored in flat contiguous workbuffer-backed storage with O(1) generation-counter deduplication
- **AreaDamage** - Thread-local accumulator for explosion/AoE damage sources. Collections call `Add()` in PostCollision and query `Get()` during the AreaDamage phase; `Clear()` resets the list each frame
- **Alignments** - Sparse collision filtering via sorted flat vector with binary search
- **IslandTerrain** - CPU terrain queries (elevation, normals) shared by client and server. All islands use the same heightmap flipped by parity
- **NavBuild / NavQuery** - Visibility graph pathfinding using per-cell NavData stored in world space. `NavBuild` constructs NavData from a canonical NavContour on IslandTerrain (server builds once; clients receive it via FrameStaticData) through a six-step pipeline: contour extraction, simplification, inflation, polygon union merge (eliminates intersecting boundaries from independently inflated nearby polygons), flat-array packing, and visibility graph construction. `NavQueryDirection(position, destination, navData)` returns a steering direction between two world-space points: escapes toward the nearest polygon boundary if the start position is inside an obstacle, uses a direct line-of-sight fast path if unobstructed, or falls back to A* through the visibility graph; if A* fails, steers toward the nearest obstacle vertex with line-of-sight. If the destination is inside an obstacle it is snapped to navigable space before pathfinding. `NavQuerySnapToNavigable(position, navData)` moves a position that falls inside an obstacle to just outside the nearest polygon boundary, used to sanitize randomly generated destinations. **When changing anything that affects NavData content** (threshold, simplification, inflation, merge, contour extraction), bump `kiNavDataVersion` in `NavBuild.h` — it feeds into `Frame::kiVersion` for save compatibility

## Architecture Notes

- **CRC system**: `Crcs()` returns a single shared CRC (excluding both client-only and server-only fields), computed and cached after update phases complete. Used for cross-build determinism validation and reconciliation fast-path
- **FrameFlags** bitmask tracks update phase and prevents duplicate side effects (e.g., `kRecalculated` guards audio/particle replay during reconciliation)
- **Client/server split**: Visual-only collections (lights, billboards, sounds, trails) are `#ifdef BT_CLIENT`; collection counts adjust automatically
- **No transient metadata in Frame structs** -- only logical state belongs here; derived data goes in lookup infrastructure

## See Also

- [Collections/CLAUDE.md](Collections/CLAUDE.md) - SOA collection structures and spawn management
