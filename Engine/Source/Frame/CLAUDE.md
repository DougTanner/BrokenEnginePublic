# Frame - Core Game State and Fixed-Timestep Simulation

## Overview

Manages per-frame game state through a dual-phase update system (Interpolate for rendering, PostRender for logic) over a sparse grid of simulation cells. `game::Frame` aggregates engine base classes (`FrameInterpolateBase`, `FramePostRenderBase`) with game-specific extensions. Collections get automatic CRC, serialization, and comparison via compile-time type lists.

See also: [Frame Update Pipeline](../../../Documents/Architecture/FrameUpdatePipeline.md) — update this diagram if phase ordering changes.

## Key Classes

- **FrameInterpolateBase / FramePostRenderBase** - Split base classes for render-phase vs. logic-phase state. PostRender orchestrates sub-phases (Update, PreCollision, PostCollision, AreaDamage, Transfer, Destroy, Spawn) and owns determinism infrastructure (CRC chain, deterministic random, UUID streams).
- **FrameStaticData** - Immutable per-coord data (area bounds, island config, per-cell NavData) stored in `CoordFrames`. Server builds NavData from the island's canonical NavContour; clients receive it via network serialization.
- **FrameUtils** - Template helpers using `std::apply` + fold expressions to iterate all collections for CRC, serialization, and copy. Also exposes the shared movement model (drag/acceleration/max-speed with optional velocity-to-direction blending).
- **TimeStep** - Fixed-timestep accumulator with time scaling, clamp, and death-spiral auto-reduction. Also drives the client sim-ceiling clamp, not just stalls.
- **Collision** - Layer-based spatial partitioning (fixed zone grid) with discrete and swept sphere tests; swept falls through to discrete on already-overlapping. Per-pair masks must be bi-directional (asserted); same-layer collision unsupported. Results land in flat `thread_local` storage via prefix-sum spans.
- **AreaDamage** - Thread-local accumulator for explosion/AoE sources with linear falloff and category-bitmask filter; populated in PostCollision, queried in AreaDamage phase.
- **Alignments** - Sparse collision filtering via sorted flat vector with binary search; key is lower-id-first so `(A,B) == (B,A)`.
- **IslandTerrain** - CPU terrain queries (elevation, normals) shared by client and server. Builds the canonical `NavContour` once on the server; clients never walk the heightmap for nav.
- **NavBuild / NavQuery** - Visibility-graph pathfinding over per-cell NavData. Direct LOS fast path with A* fallback. **Bump `kiNavDataVersion` when changing NavData content** (feeds into `Frame::kiVersion` for save compatibility).

## Architecture Notes

- **CRC system**: Single shared CRC excludes both client-only and server-only fields, cached after update. Drives cross-build determinism validation and reconciliation fast-path. `ServerCollections()` is the cross-build-shared subset walked by CRC, `ServerRead`, and `LogDifferences`; client-only collections stay out of determinism.
- **Collection registration**: `Collections()` tuple size is `static_assert`-locked to each base's `kCollectionCount`, and the two bases must agree. When adding a Collection pair, update both `kCollectionCount` constants and (if shared) `ServerCollections()` alongside the `Collections()` tuple.
- **Client/server split**: Visual-only collections gated by `#ifdef BT_CLIENT`.
- **thread_local lazy-init**: `Collision` and `AreaDamage` thread_local vectors MUST start empty — constructors run during `mi_process_init` before the allocator is ready. First-use resize and overflow growth wrap realloc in `ScopedSuppressAllocationTracking` and (on overflow) `DEBUG_BREAK` naming the preallocate constant to raise.
- **No transient metadata in Frame structs** — only logical state belongs here; derived data goes in lookup infrastructure.

## See Also

- [Collections/CLAUDE.md](Collections/CLAUDE.md) - SOA collection framework and engine-level collections
