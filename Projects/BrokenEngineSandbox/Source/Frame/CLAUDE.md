# Frame - Game-Specific Frame State and Simulation

## Overview

Game-specific frame state for a space combat game, extending the engine's `FrameBase` with collections for players, blasters, missiles, spaceships, and targets. Runs at a 64 fps fixed timestep with phase-separated structures (FrameInterpolate and FramePostRender) enabling deterministic replay and shared CRC validation.

## IMPORTANT: Frame Purity Constraint

Frame code is purely functional. Frame updates must only rely on explicit function parameters -- Frame code must NEVER query Game (`gpGame`) for anything. The Frame does not know which Player is human vs AI. Human player identity, camera shake, death screen transitions, and respawn orchestration are Game-level responsibilities.

## Key Classes/Systems

- **Frame / FrameInterpolate / FramePostRender** (`Frame.h/cpp`) - Hierarchical frame structures extending engine base classes. Collections held via `std::unique_ptr` with forward declarations. Also defines `GameFlags`, `TransferRequest`, `FrameBounds`, and cross-cell transfer utility functions. Provides dual CRCs (full and server-only), `LogDifferences()`, and `ServerRead()` for determinism validation
- **FrameCollections.h** - Aggregation header including all collection types; provides tuple accessors and type list aliases used by engine `ForEach*` dispatch helpers
- **RunFrameTick()** (`FrameTick.h/cpp`) - Unified physics pipeline executing all five phases via `ActiveFrameRef`. Shared by GameBase parallel dispatch and client reconciliation replay. Asserts MXCSR flags at entry to catch corruption by external APIs
- **TerrainUtils** (`TerrainUtils.h/cpp`) - Shared AI terrain-following, obstacle avoidance, and edge-crossing encouragement used by Players and Spaceships. Combines gradient-based contour following, elevation correction, mountain look-ahead, and periodic edge-crossing steering
- **StatusChange.h** - Status change types and `TransferData` struct for cross-cell entity migration. `TransferData` uses deducing-this `SharedMembers()` for shared CRC subset and carries a `global_player_t` and client GUID (two 64-bit halves, not serialized over the network) to preserve player and client identity across cell boundaries. Includes `kWeaponModeChange` (encodes player ID in `vecPosition`) for toggling blaster vs missile mode
- **HealthDamage.h** - Combat balance constants, collision category/mask configuration, and difficulty-scaled damage
- **SmokeSpreadTest** (`SmokeSpreadTest.h/cpp`) - Automated smoke/stress test. Enabled via `kbEnableSmokeSpreadTest` in `Pch.h`

## Architecture Notes

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
