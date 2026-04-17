# Frame - Game-Specific Frame State and Simulation

## Overview

Game frame state extending engine `FrameBase` with collections for players, blasters, missiles, spaceships, and targets. Fixed timestep from `engine::kiTickRate`, phase-separated (FrameInterpolate, FramePostRender) for deterministic replay and shared-CRC validation. World-space cells are 300x300 with 200x200 islands placed deterministically via `ComputeIslandOffset(GridCoord)` so client and server agree on island layout without broadcast. `Frame::kiVersion` aggregates `engine::kiNavDataVersion` plus every collection's `kiVersion`; bumping any sub-version invalidates persisted frames.

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
