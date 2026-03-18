# Frame - Game-Specific Frame State and Simulation

## Overview

Game-specific frame state for a space combat game, extending the engine's `FrameBase` with collections for players, blasters, missiles, spaceships, and targets. Runs at a 64 fps fixed timestep with phase-separated structures (FrameInterpolate and FramePostRender) enabling deterministic replay and server CRC validation.

## IMPORTANT: Frame Purity Constraint

Frame code is purely functional. Frame updates must only rely on explicit function parameters -- Frame code must NEVER query Game (`gpGame`) for anything. The Frame does not know which Player is human vs AI. Human player identity, camera shake, death screen transitions, and respawn orchestration are Game-level responsibilities.

## Key Classes/Systems

- **Frame / FrameInterpolate / FramePostRender** (`Frame.h/cpp`) - Hierarchical frame structures extending engine base classes. Collections held via `std::unique_ptr` with forward declarations to minimize include dependencies. Provides `ServerCrc()`, `LogDifferences()`, and `ServerRead()` for cross-build determinism validation
- **FrameCollections.h** - Aggregation header including all collection types; provides `GameInterpolateCollections()` / `GamePostRenderCollections()` tuple accessors
- **RunFrameTick()** (`FrameTick.h/cpp`) - Unified physics pipeline executing all five phases (Interpolate, PostRender with Collision, Transfer, Destroy, Spawn) for a single Frame. Shared by both GameBase parallel dispatch and client reconciliation replay. Asserts MXCSR flags (flush-denormals, round-to-nearest) at entry to catch corruption by external APIs (audio, Vulkan, etc.)
- **TerrainUtils** (`TerrainUtils.h/cpp`) - Shared AI terrain-following and obstacle avoidance used by Players and Spaceships
- **StatusChange.h** - Status change types (spawn, respawn, transfer, destroy) and `TransferData` struct for cross-cell entity migration with full entity state
- **HealthDamage.h** - Combat balance constants, collision category/mask configuration, and difficulty-scaled damage
- **SmokeSpreadTest** (`SmokeSpreadTest.h/cpp`) - Automated smoke/stress test that orbits the player around the origin while continuously firing, and spawns spaceships at random angles. Enabled via `kbEnableSmokeSpreadTest` in `Pch.h`

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
