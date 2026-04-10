# Frame - Game-Specific Frame State and Simulation

## Overview

Game-specific frame state for a space combat game, extending the engine's `FrameBase` with collections for players, blasters, missiles, spaceships, and targets. Runs at a 64 fps fixed timestep with phase-separated structures (FrameInterpolate and FramePostRender) enabling deterministic replay and shared CRC validation.

## IMPORTANT: Frame Purity Constraint

Frame code is purely functional. Frame updates must only rely on explicit function parameters -- Frame code must NEVER query Game (`gpGame`) for anything. The Frame does not know which Player is human vs AI. Human player identity, camera shake, death screen transitions, and respawn orchestration are Game-level responsibilities.

## Key Classes/Systems

- **Frame / FrameInterpolate / FramePostRender** (`Frame.h/cpp`) - Hierarchical frame structures extending engine base classes. Collections held via `std::unique_ptr` with forward declarations. Also defines `GameFlags`, `TransferRequest`, `FrameBounds`, cross-cell transfer utility functions, game-wide type aliases (`player_t`, `target_t`), and `Frame::GetMissileTarget()` (selects the best target for a new missile based on proximity, heading angle, and subscriber count). Provides shared CRC via `Crcs()`, `LogDifferences()`, and `ServerRead()` for determinism validation. Enemy spaceships are spawned every half second as a tight cluster via `SpawnSpaceshipGroup()`: one spaceship per non-exploding player, all at angular offsets from a single random base angle around the first non-exploding player, with radius expanded until all positions have ocean depth (elevation < `-kfSpaceshipRadius * 2.0f`)
- **FrameCollections.h** - Aggregation header including all collection types; provides tuple accessors and type list aliases used by engine `ForEach*` dispatch helpers
- **RunFrameTick()** (`FrameTick.h/cpp`) - Unified physics pipeline executing all five phases via `ActiveFrameRef`. Shared by GameBase parallel dispatch and client reconciliation replay. Asserts MXCSR flags at entry to catch corruption by external APIs
- **TerrainUtils** (`TerrainUtils.h/cpp`) - AI terrain-following and obstacle avoidance used by Players. Combines gradient-based contour following, elevation correction, mountain look-ahead, and return-to-island steering. Players use this for roaming and switch to `NavQueryDirection` (taking position, destination, and per-cell NavData from `FrameStaticData`) for destination-based frame changes
- **StatusChange.h** - Status change types and `TransferData` struct for cross-cell entity migration. `TransferData` uses deducing-this `SharedMembers()` for shared CRC subset and carries a `global_id_t`, client GUID (two 64-bit halves, not serialized over the network), navigation delay, fleet wanted coord, and both pending countdown tick fields (`uiPendingFleetWantedCoordTicks`, `uiPendingWeaponModeTicks`) to preserve full player state across cell boundaries. `UpdatePlayerData` carries a player UUID, weapon mode flag, and `uiPendingWeaponModeTicks` — used by `kUpdatePlayer` to apply per-player behavior settings injected by the server session. `UpdateFleetData` carries a fleet wanted coord, flagship designation, and `uiPendingFleetWantedCoordTicks` — used by `kUpdateFleet` to broadcast the fleet's current navigation target to all fleet members. `SpawnPlayerData` carries a flagship designation flag, fleet wanted coord, and `uiPendingFleetWantedCoordTicks` for the initial spawn of fleet members
- **HealthDamage.h** - Combat balance constants, collision category/mask configuration, and difficulty-scaled damage

## Architecture Notes

- **Delayed StatusChange Activation**: When a StatusChange carries a behavior change (fleet nav coord, weapon mode), the server initializes a `uint8_t` countdown field to `kiTickRate` and broadcasts the StatusChange immediately. Frame code decrements the countdown each tick and applies the behavior when it reaches 0. This eliminates reconciliation caused by clients not yet having received the StatusChange when the server's simulation executes it. Does NOT apply to entity transfers/spawns/destroys — those are physical state changes that cannot be deferred
- **Phase separation**: FrameInterpolate (velocity integration, object sync) and FramePostRender (collision, transfer, destroy, spawn) are strictly separated for deterministic replay
- **World coordinates**: Frames use world-space coordinates keyed by `GridCoord`, with transfer utilities for cross-cell entity movement
- **Include decoupling**: `Frame.h` forward-declares collections; `FrameCollections.h` provides concrete types only where needed
- **Alignment system**: Per-frame alignment state (player/enemy IDs and relationship map) owned by Game and copied into frame state for collision and targeting filtering
- **Render pipeline** (client-only): Three-phase GPU pipeline -- BeginRender, Render, EndRender
- For detailed update flow and architecture diagrams, see [Documents/Architecture/FrameUpdatePipeline.md](../../../../Documents/Architecture/FrameUpdatePipeline.md)

## See Also

- Engine base frame: [../../../../Engine/Source/Frame/CLAUDE.md](../../../../Engine/Source/Frame/CLAUDE.md)
- Game object collections: [Collections/CLAUDE.md](Collections/CLAUDE.md)
  - [Players](Collections/Players/CLAUDE.md) | [Blasters](Collections/Blasters/CLAUDE.md) | [Missiles](Collections/Missiles/CLAUDE.md) | [Spaceships](Collections/Spaceships/CLAUDE.md) | [Targets](Collections/Targets/CLAUDE.md)
