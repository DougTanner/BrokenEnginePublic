# Architecture: Fleet Reload State Ordering

Source: `code-review` and final-audit Opus subagents during execution of `Architecture_FleetRngDeterminism.md`. Both flagged the same pre-existing concern independently.

## Problem

`ServerSession::ResetClientsForLoad` (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:389`) executes the following sequence on every `ServerLoad` / `Quickload` / `SaveLoadReplay`:

1. `ReadGrid` runs first (called from `Engine/Source/GameSaveLoad.cpp:50,109,183`) and inside it `ServerFleetManager::ReadFleetData` (`ServerFleetManager.cpp:720`) populates three maps from disk: `mFleets`, `mPlayerToGuid`, `mGuidToClientId`.
2. Then `ResetClientsForLoad` (`ServerSession.cpp:389`) iterates clients and per-client calls `mpFleetManager->OnResetForLoad(rClient.iClientId, rClient.clientGuid)` (`ServerSession.cpp:442`) — which reads the loaded `mFleets` and pushes flagship intentions into `mPendingFlagshipUpdates`.
3. Finally `ResetClientsForLoad` calls `mpFleetManager->ResetState()` (`ServerSession.cpp:449`), and `ResetState` (`ServerFleetManager.cpp:803`) calls `.clear()` on `mFleets`, `mPlayerToGuid`, `mGuidToClientId`.

After step 3, the loaded fleet topology is gone. `mPendingFlagshipUpdates` survives (its clear at line 396 happens BEFORE `OnResetForLoad`), but the actual fleet membership / GUID maps are empty.

The narrow seed-lifecycle plan from this session (`Architecture_FleetRngDeterminism.md`, executed) restored `mRandomEngine.uiState` correctly because `ResetState` no longer touches the RNG. But the `.clear()` calls on the three maps remain, and the comment added in that plan ("constructor TimeSeeds once; ReadFleetData restores from save for replay determinism") inadvertently ratifies the surrounding code as intentional.

## Two interpretations

**(a) Intentional pattern.** The loaded `mFleets` is consumed by `OnResetForLoad` only to seed `mPendingFlagshipUpdates`, then deliberately discarded. After load, `mFleets` / `mPlayerToGuid` / `mGuidToClientId` are rebuilt organically as clients reconnect (`ServerFleetManager::OnClientConnected` populates `mGuidToClientId`) and respawn (`ServerFleetManager::OnPlayerSpawned` at `:434` populates `mFleets` and `mPlayerToGuid`). If true, the design is sound but the comment in `ResetState` should call this out so future readers don't restore the loaded data.

**(b) Bug.** Someone moved or added the `ResetState()` call without realizing it sat after `OnResetForLoad`. On every quickload the fleet topology silently rebuilds from network traffic instead of from disk, defeating the point of `ReadFleetData`. If true, the three `.clear()` calls should be removed from `ResetState` (or `ResetState` should be reordered to run before `OnResetForLoad`).

## Investigation steps

This is a bounded investigation, not yet a fix. Execute in order:

1. **Trace a single Quickload from start to first post-load tick.** Add temporary `LOG(kNetwork, kDebug, ...)` lines that report `mFleets.size()`, `mPlayerToGuid.size()`, `mGuidToClientId.size()` at:
   - End of `ServerFleetManager::ReadFleetData` (`ServerFleetManager.cpp:781` — after the new seed read)
   - End of `ResetClientsForLoad`'s per-client loop (`ServerSession.cpp:443`, after `OnResetForLoad` runs for the last client)
   - End of `ResetClientsForLoad` (`ServerSession.cpp:459`, after `ResetState` clears)
   - End of the first `ProcessFlagshipUpdates` call after load
   - End of the first frame-tick that processes a respawn after load

2. **Run the scenario**: record a session with at least 3 fleets, save, kill server, restart, load, observe logs. Compare to a fresh-start no-load run with the same fleet count.

3. **Decide**:
   - If `mFleets` is non-empty at the first post-load tick (rebuilt via `OnPlayerSpawned`), interpretation (a) holds. Action: remove the temporary logs, update the `ResetState` comment to clarify "loaded `mFleets`/`mPlayerToGuid`/`mGuidToClientId` are intentionally cleared because they get rebuilt by `OnPlayerSpawned`/`OnClientConnected` during reconnect."
   - If `mFleets` is empty at the first post-load tick (no rebuild path), interpretation (b) holds. Action: remove the three `.clear()` calls from `ResetState` (`ServerFleetManager.cpp:805-807`), or restructure `ResetClientsForLoad` so `ResetState` runs before `ReadFleetData` repopulates. The cleanest fix is likely splitting `ResetState` into `ClearPendingRequests()` (the four pending-vector clears) and `ClearSessionState()` (the three map clears), with only `ClearPendingRequests` being called per-load.

## Files affected

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp` — possibly `ResetState` body (remove 3 lines or split function)
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.h` — possibly add a new method declaration
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:449` — possibly switch to a different reset call

## Notes

- This concern is independent of replay determinism (the seed lifecycle is correct regardless of which interpretation holds).
- The `mPendingFlagshipUpdates` deque IS preserved across `ResetState` (only its per-call clear at `ServerSession.cpp:396` runs), which is the single load-time invariant the existing comment at `ServerSession.cpp:450` documents.
- If interpretation (b) is correct, the bug has likely been present for many sessions but masked by the fact that quickload is gated on `kbDebugInput` (debug-only) and replay determinism issues would only surface on a specific record/playback scenario. Worth checking `git blame` on `ServerSession.cpp:449` to understand when the `ResetState` call was added.
