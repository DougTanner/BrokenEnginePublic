# Architecture: Fleet RNG Determinism Under Replay

Source: /external-architecture-review on Projects/BrokenEngineSandbox/Source/Network/Server (recursive)

`ServerFleetManager::mRandomEngine` is a `common::RandomEngine` seeded from wall clock via `TimeSeed()` (`ServerFleetManager.cpp:803` inside `ResetState()`). It drives random direction picks in `TickFleetTimers` (line 216) and spawn navigation-delay jitter in `OnPlayerSpawned` (line 477). Under replay playback, `ServerSession::ResetClientsForLoad` (`ServerSession.cpp:449`) calls `ResetState()`, which re-seeds from wall clock — different from the recorded run's seed. Result: replayed runs diverge from the recorded fleet navigation / spawn paths, defeating replay determinism.

## Changes

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.h
- `mRandomEngine` should accept an explicit seed on construction/reset. Add a constructor/reset parameter `uint64_t uiInitialSeed` that `ServerSession` supplies. [~15m]

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp
- Line 803: `ResetState()` currently calls `mRandomEngine.TimeSeed()`. Change to accept seed as parameter; default `TimeSeed` only when no seed is supplied (live play). [~15m]

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp
- Line 449 `ResetClientsForLoad`: pass the replay-recorded seed (stored in save file / recorded at record-time) into `mpFleetManager->ResetState(recordedSeed)`. If `GameSaveLoad` doesn't currently persist the fleet RNG seed, extend the save format. [~1h]

### Save Format (wherever `GameSaveLoad::ServerSave` writes)
- Persist `mpFleetManager->mRandomEngine.State()` (or the seed) into the replay record. On `ServerLoad`, restore it before `mpFleetManager->ResetState()`. [~45m]

## Verification
- Record a replay with known flagship navigation. Save the CRC sequence.
- Playback: compare resulting flagship positions / spawn timings frame-by-frame. Must match bit-exactly.
- If `NetworkSimulation` is disabled (`keNetworkSimulation == kDisabled`), replay should be fully deterministic.

## Verification Notes
Verified against commit d08678d3 — all cited line numbers accurate:
- `ServerFleetManager.cpp:216` (Random direction in TickFleetTimers), `:477` (OnPlayerSpawned navigation-delay jitter), `:794-803` (`ResetState()` body with `TimeSeed()` at 803)
- `ServerSession.cpp:389` (`ResetClientsForLoad` opens), `:449` (`mpFleetManager->ResetState()` call)
- Replay chain confirmed: `ResetClientsForLoad` -> `ResetState()` -> `TimeSeed()` re-seeds from wall clock, defeating determinism as described.
- Save-format extension is genuinely required (GameSaveLoad delegates fleet persistence to `ServerSession::WriteFleetData`/`ReadFleetData` per `ServerSession.cpp:463-468` — seed must be added there).
