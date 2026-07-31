# BrokenEngineSandbox Source - Game Layer

## Overview

BrokenEngineSandbox is a tech demo that shows off and stress-tests engine features — not a game: there is no human player and no win condition. AI-driven spaceship fleets (a flagship plus its wingmen) fight continuously spawning enemies over the island ocean, so "the client player" always means a ship the AI drives, never one a person steers. All game code lives in the `game` namespace.

Game implementation built on Engine and Common. `Game` owns the per-side sessions, the coordination of cell simulation across the unbounded grid, shared fleet data, client selection/settings, and server persistence.

## Ownership

- `Fleet` is the shared data model and uses persistent identifiers assigned by the server. `FleetSelection` is client-only focus and navigation UI state.
- `ClientSession` and `ServerSession` are game-policy wrappers that compose engine-owned session runtimes. See `Network/AGENTS.md`.
- `GameSaveLoad` is server-only and owns save/load/replay. Its persistence and replay contracts live in `Save/AGENTS.md`.
- Agent commands are game-dispatched, but the build-agnostic shared handlers, transport, synthetic input, and UI snapshots are engine-owned; the dispatcher tries the engine shared handlers before its side-specific ones. See `Agent/AGENTS.md`.

## Architecture

- Client subscriptions cover the current cell plus visible neighbors within the authorized 3x3 ring. Change the client's cell through `Game::SetClientGridCoord()` so the visible-neighbor cache is invalidated.
- Cross-cell transfers are serialized `StatusChange`s carrying spawn state. They are removed from `FrameInput` before normal Spawn processing.
- Player/enemy alignments are copied into each new `FramePostRender`; frame code reads the snapshot, not `gpGame`.
- Persisted client settings are versioned POD written through the engine versioned-file helpers. Bump the owning version on layout change.
- Save/replay compatibility is gated by `Frame::kiVersion`; `Version.h::kiGameVersion` is informational.
- `Pch.h` owns compile-time switches. `kbDesyncDebugFrames` is a manual, disabled-by-default diagnostic switch that must match between client and server; it controls full-frame buffering/serving and client debug-frame stalling without changing the wire contract.
- The game PCH force-includes shared aggregation/layout headers. Consumer TUs do not repeat those includes; DataPacker has a separate PCH.

## Subsystems

- `Agent/AGENTS.md` - Harness command dispatch, queries, and network diagnostic probes
- `Frame/AGENTS.md` - Game simulation and collections
- `Graphics/AGENTS.md` - Camera controller
- `Input/AGENTS.md` - Game input mapping
- `Network/AGENTS.md` - Game protocol and sessions
- `Profile/AGENTS.md` - Game profiling counters
- `Save/AGENTS.md` - Server save/load/replay
- `Server/AGENTS.md` - Server monitoring window
- `Ui/AGENTS.md` - HUD and settings

## See Also

- `../../../Engine/Source/AGENTS.md`
- `../../../Documents/FloatingPointDeterminism.txt`
