# Frame - Game-Specific Frame State and Simulation

## Overview

Game frame state extending engine `FrameBase` with collections for players, blasters, missiles, spaceships, and targets. Fixed timestep from `engine::kiTickRate`, phase-separated (FrameInterpolate, FramePostRender) for deterministic replay and shared-CRC validation. World-space cells are 1000x1000 engine units; each cell (including origin) holds a deterministically-generated archipelago chain (1 Huge anchor + up to 5 big chain links + an emergent ring of smalls around each big island, trimmed by hull-overlap packing and the cell edge, each with world position, rotation, island CRC) built by `engine::IslandChainPlacement::Generate` via contact growth: a Huge anchor island at output index 0 in the SW third, then a chain of 2 Large + 3 Medium islands each placed just-touching the chain tip (the anchor or the previous link) with the heading turning hard per step so the chain reads as a clear curve (the chain stops as soon as the next link would leave the cell, so it extends as far as the cell allows), then small islets ringing each big island — each small touches a big island ONLY (never another small, so the ring can't grow into small-to-small chains), the per-island count scaling to that island's perimeter, plus a few extra trailing off the end of the curve. Every non-anchor island is positioned to sit a small fixed gap beyond an existing island's hull along a chosen direction (projection-based "touch"), so the cell reads as one tight archipelago cluster rather than scattered islands; attempts that leave the cell or overlap another hull are rejected. Islands pack by their rotated valid-area convex hull (SAT overlap test) against per-template buckets and footprints from `engine::IslandTerrain` — hulls never overlap, though bounding rectangles may overlap invisibly underwater (DataPacker auto-crops each island to its land bbox at bake time). The island CRC for each placement is drawn from `engine::gpIslandTerrain` bucket lists (falling back to `mIslandCrcsSorted`), so adding a new island folder (`Engine/Data/Islands/03/`, `04/`, …) automatically widens the variety with no code changes. Placements live in each `FrameStaticData::islands` list, so client and server agree on island layout without broadcast. (Exception: in `kbDebugInput` builds the main-menu origin cell is overridden by Game to a single centered island acting as a browser — gameplay cells and release builds always use the scatter.) `Frame::kiVersion` aggregates `engine::kiNavDataVersion` plus every collection's `kiVersion`; bumping any sub-version invalidates persisted frames.

Frame purity constraint is documented in the parent ([../CLAUDE.md](../CLAUDE.md)) and must be respected here.

See also: [Frame Update Pipeline](../../../../Documents/Architecture/FrameUpdatePipeline.md) — update this diagram if phase ordering changes.

## Key Classes/Systems

- **Frame / FrameInterpolate / FramePostRender** - Hierarchical frame structures extending engine base classes. Collections held via `std::unique_ptr` with forward declarations in `Frame.h`; concrete types live in `FrameCollections.h` which also exposes tuple/type-list aliases used by engine `ForEach*` dispatch
- **RunFrameTick** - Unified physics pipeline driven by `ActiveFrameRef`, shared by GameBase parallel dispatch and client reconciliation replay. Asserts MXCSR `_DN_FLUSH` + `_RC_NEAR` at entry because external APIs (audio, Vulkan) can corrupt rounding mode and break `/fp:strict` determinism
- **TerrainUtils** - AI terrain-following and obstacle avoidance combining gradient contour-following, elevation correction, mountain look-ahead, and return-to-island steering; Players layer `NavQueryDirection` on top for destination routing
- **StatusChange.h** - Network-serialized cross-cell migration and behavior-update payloads; `SharedMembers()` isolates the deterministic subset consumed by the shared CRC (excludes client-only fields). Distinct from the transient in-frame `TransferRequest` buffer which is not serialized and not in the CRC
- **HealthDamage.h** - Combat balance constants plus `CollisionCategory`/`CollidesWith` bitmask pairs defining the collision matrix; alignment filter layered on top for same-team rejection

## Architecture Notes

- **Delayed StatusChange activation**: Behavior changes (fleet nav coord, weapon mode) carry a `uint8_t` countdown initialized to `kiTickRate` and broadcast immediately; behavior applies when the countdown hits zero. Eliminates reconciliation from in-flight StatusChanges. Does NOT apply to entity transfers/spawns/destroys
- **Phase separation**: FrameInterpolate (velocity integration, sync) and FramePostRender (collision, transfer, destroy, spawn) strictly separated for deterministic replay
- **Two-tier collection dispatch**: Players are invoked explicitly because they carry hand-written player-only logic; Blasters/Missiles/Spaceships/Targets auto-dispatch via `engine::ForEach*` over the `GameInterpolateTypes`/`GamePostRenderTypes` aliases in `FrameCollections.h`. Add a new collection by registering it in those aliases
- **Serialization/CRC protocol**: `Crcs()` hashes the shared subset only (`SharedCrcMembers()` for players, `SharedCollectionCrc` for auto-dispatched collections). `Write()`/`Read()` covers full `Members()` for save/load; `ServerRead()` consumes only the shared subset for replication

## See Also

- Engine base frame: [../../../../Engine/Source/Frame/CLAUDE.md](../../../../Engine/Source/Frame/CLAUDE.md)
- Game object collections: [Collections/CLAUDE.md](Collections/CLAUDE.md)
