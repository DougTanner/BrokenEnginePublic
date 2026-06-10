# Frame - Game-Specific Frame State and Simulation

## Overview

Game frame state extending engine `FrameBase` with collections for players, blasters, missiles, spaceships, and targets. Fixed timestep from `engine::kiTickRate`, phase-separated (FrameInterpolate, FramePostRender) for deterministic replay and shared-CRC validation. World-space cells are `Frame::kfCellWidth` × `kfCellHeight` engine units; each cell's deterministic island layout lives in `engine::FrameStaticData::islands` (built engine-side — see the engine frame docs), shared by client and server without broadcast. `Frame::kiVersion` aggregates `engine::kiNavDataVersion` plus every collection's `kiVersion`; bumping any sub-version invalidates persisted frames.

Frame purity constraint is documented in the parent ([../CLAUDE.md](../CLAUDE.md)) and must be respected here.

See also: [Frame Update Pipeline](../../../../Documents/Architecture/FrameUpdatePipeline.md) — update this diagram if phase ordering changes.

## Key Classes/Systems

- **Frame / FrameInterpolate / FramePostRender** - Hierarchical frame structures extending engine base classes. Collections held via `std::unique_ptr` with forward declarations in `Frame.h`; concrete types live in `FrameCollections.h` which also exposes tuple/type-list aliases used by engine `ForEach*` dispatch. `Frame::GetMissileTarget` is the shared enemy-target query used by missile/player homing — it prefers under-subscribed targets (load-balances volleys) and registers a subscriber on success, so it mutates the frame and call order within a tick is part of the deterministic stream
- **FrameTick** - `RunFrameTick(ActiveFrameRef, ...)` is the unified physics pipeline shared by GameBase parallel dispatch and client reconciliation replay. Asserts MXCSR `_DN_FLUSH` + `_RC_NEAR` at entry because external APIs (audio, Vulkan) can corrupt rounding mode and break `/fp:strict` determinism. On a coord's first tick it lazily builds the purely-derived per-cell data (server-only NavData from placements; the elevation grid on both sides) before any sim phase
- **TerrainUtils** - Stateless AI terrain-following helpers: gradient contour-following with elevation correction and mountain look-ahead (open-ocean and far-from-center cases steer back toward island center), consumed by Players; and a separate front/side elevation-sampling obstacle-avoidance that nudges delta-rotation away from rising terrain, consumed by Spaceships
- **StatusChange.h** - Network-serialized cross-cell migration and behavior-update payloads. Wire format is hand-written per type in `Network/NetworkSerialization.cpp` (see [Network/CLAUDE.md](../Network/CLAUDE.md)); payloads never enter the frame `sharedCrc` — payload `SharedMembers()` feeds build-shared equality only. `IsTransferType` relies on the transfer enum values staying contiguous. Distinct from the transient in-frame `TransferRequest` buffer which is not serialized and not in the CRC
- **HealthDamage.h** - Combat balance constants plus `CollisionCategory`/`CollidesWith` bitmask pairs defining the collision matrix; alignment filter layered on top for same-team rejection

## Architecture Notes

- **Phase separation**: FrameInterpolate (velocity integration, sync) and FramePostRender (collision, transfer, destroy, spawn) strictly separated for deterministic replay. `RunFrameTick` runs them in order: Interpolate → PostRender Update → Collision (PreCollision / engine collide / PostCollision / AreaDamage) → Transfer → Destroy → Spawn, then stamps `sharedCrc` — after all phases and before the server transfer harvest, so transfer-spawned arrivals enter the next tick's CRC on both sides. The game level owns clearing the engine per-tick queues: PostCollision ends with `engine::Collision::Clear()`, AreaDamage with `engine::AreaDamage::Clear()`
- **Per-cell derived data**: NavData and the elevation grid (`kiElevationGridDim` resolution) are deterministically derived from island placements + shared heightmaps, so both sides rebuild bit-identical copies on a coord's first tick — kept out of the CRC and never serialized (client receives prebuilt server-only NavData over the wire)
- **Two-tier collection dispatch**: Players are invoked explicitly because they carry hand-written player-only logic; Blasters/Missiles/Spaceships/Targets auto-dispatch via `engine::ForEach*` over the `GameInterpolateTypes`/`GamePostRenderTypes` aliases in `FrameCollections.h`. Add a new collection by registering it in those tuple helpers
- **Spaceship spawning**: `Spawn` ticks a fixed-interval timer that places an enemy chevron group per spawn pulse, rasterizing the cell into a validity grid (in-bounds, terrain-clear, away from alive players) and scoring candidate anchors for best chevron fit
- **Delayed StatusChange activation**: behavior-changing StatusChanges (weapon mode, fleet wanted-coord) carry `uint8_t` countdown fields rather than an absolute activation tick; the receiving collection stores them in pending SOA fields, decrements each PostRender Update, and applies the change at zero. Countdowns ride in `TransferData` so a mid-countdown cell transfer keeps draining. Entity transfers, spawns, and destroys apply immediately
- **`Frame.h` inline helpers**: the frame-bounds/transfer helpers use the `vecArea` lane convention x=minX, y=maxY, z=maxX, w=minY (matches `common::InsideArea`) — easy to get wrong. Seed per-coord RNG streams via `SeedFromGridCoord(coord, multiplier)` with a distinct multiplier per use case (decorrelates streams; rationale in the header comment)
- **Serialization/CRC protocol**: `Crcs()` hashes the shared subset only (`SharedCrcMembers()` for players, `SharedCollectionCrc` for auto-dispatched collections). `Write()`/`Read()` covers full `Members()` for save/load; `ServerRead()` consumes only the shared subset for replication. Arrival-grace timing for transfers is documented at the [Collections hub](Collections/CLAUDE.md)

## See Also

- Engine base frame: [../../../../Engine/Source/Frame/CLAUDE.md](../../../../Engine/Source/Frame/CLAUDE.md)
- Game object collections: [Collections/CLAUDE.md](Collections/CLAUDE.md)
