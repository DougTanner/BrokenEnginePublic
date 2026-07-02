# Server Pause + Fresh-Game Reset Semantics

## Context

Two server tick-orchestration gaps surfaced by the Network audit, both verified against current control flow. (a) Requests injected/drained while the server is paused (`iFullTicks == 0`) are silently lost because `BuildFrameInputs` wipes `mFrameInputs` every `ServerUpdate` and no tick consumes them while paused. (b) The local debug-menu fresh-game path (`kResetFrame` without `kQuickload`) wipes fleet state but skips the client-resync that its networked sibling `ServerReset` runs, leaving connected clients stale until a CRC-mismatch resync bails them out. Neither touches the wire format.

## Design

### (a) Preserve requests received while paused

**Mechanism (verified):** `GameBase::ServerUpdate` calls `PrepareActiveSet()` → `BuildFrameInputs` every cycle, independent of `iFullTicks` (`GameBase.cpp:123,441,450`). `ServerBroadcaster::BuildFrameInputs` (`ServerBroadcaster.cpp:17`) starts with `gpGame->mFrameInputs.clear()` (`:22`). The consuming loop `for (i < iFullTicks)` (`GameBase.cpp:128`, consumption at `:181`/`:235-238`) never runs at `iFullTicks == 0`, so the next call wipes the untouched map. Two drains feed that doomed map:
- **`ProcessFlagshipUpdates`** (`:78` → `FleetNavigationController::ProcessFlagshipUpdates`) genuinely drains its own queue — `mPendingFlagshipUpdates.clear()` (`FleetNavigationController.cpp:181`) — so a paused update loses the `kUpdateFleet` from **both** the queue and `mFrameInputs`. This also drops the flagship updates the load path re-queues via `OnResetForLoad` if the load lands on a paused cycle (self-heals slowly via timer refire).
- **`ProcessUpdatePlayerRequests`** (`:68`) injects `kUpdatePlayer` (weapon-mode toggles) but does **not** clear `mPendingUpdatePlayerRequests` itself; that queue is cleared at the **start** of the next `PreTickNetwork` (`ServerSession.cpp:264`) and refilled from packets each poll (`QueueUpdatePlayerRequest`, `:130`). Net lifetime one cycle → a toggle sent while paused is injected, wiped, then its request cleared before any tick sees it (user must retoggle).

**Fix:** make both request classes persist-until-served while paused, matching the existing pattern for the new-subscription and resync queues (they explicitly stay queued across polls when paused — `Engine/Source/Network/CLAUDE.md:15`, `ServerSessionBase.cpp:90`, consumers clear only after servicing). Because the pause state is known before `iFullTicks` is computed, gate on the pause/timestep flag:
- `ProcessFlagshipUpdates`: skip the drain while paused so `mPendingFlagshipUpdates` survives.
- `ProcessUpdatePlayerRequests`: skip the drain **and** gate the `PreTickNetwork` clear of `mPendingUpdatePlayerRequests` (`ServerSession.cpp:264`) so the queue survives — gating the drain alone is insufficient since the clear runs first each cycle.

**Rider (same fix):** the waiting-spawn loop mints `gpGame->GenerateGlobalId()` per `BuildFrameInputs` call (`ServerBroadcaster.cpp:34`), so a client stuck in `mClientsWaitingForSpawn` across paused cycles burns one monotonic int64 id per cycle (waste, not a lost spawn). Gating spawn-id minting on non-paused cycles removes the gap; harmless either way given int64 range.

### (b) Complete the local-menu fresh-game reset

There are two fresh-game entry points; only the **local debug-menu** one is incomplete:
- **Complete (networked):** client Enter → `kClientResetRequest` → `ServerSession.cpp:203-207` → `GameSaveLoad::ServerReset()` (`GameSaveLoad.cpp:66-78`) runs `CreateNewFrame` → `SetNextGlobalId(1)` → `Reset()` → `mpFleetManager->ResetState()` → `ResetClientsForLoad()` → `ComputeActiveSet()`.
- **Incomplete (local menu):** `GameSaveLoad::Quickload`'s `kResetFrame`-without-`kQuickload` branch (`GameSaveLoad.cpp:152-157`) runs only `CreateNewFrame` → `Reset()` → `mpFleetManager->ResetState()`. It skips `ResetClientsForLoad()` **and** `SetNextGlobalId(1)`.

`ResetClientsForLoad` (`ServerSession.cpp:487-540`) is what broadcasts `engine::gpServer->BroadcastLoadNotification()` (`:493`), clears + rebuilds `mClientOwnedPlayerIds`/`authorizedCoords` (`:511-520`), resets the client/transfer/broadcaster managers, and drains buffered frames + pending queues. Skipping it means no load notification goes out and the parallel vectors point at players `CreateNewFrame` just wiped — connected clients recover only via the CRC-mismatch resync path.

**Fix:** align the local-menu branch with `ServerReset` — add `game::gpServerSession->ResetClientsForLoad()` (and `SetNextGlobalId(1)`) after the `ResetState()` in the `:152-157` branch. Prefer factoring the shared fresh-game sequence so the two paths can't drift again (both already call `CreateNewFrame` + `Reset` + `ResetState`).

## Critical files

- `Projects/.../Network/Server/ServerBroadcaster.cpp` — `BuildFrameInputs`, `ProcessUpdatePlayerRequests` (a)
- `Projects/.../Network/Server/FleetNavigationController.cpp` — `ProcessFlagshipUpdates` (a)
- `Projects/.../Network/Server/ServerSession.cpp` — `PreTickNetwork` clear gating (a); `ResetClientsForLoad` (reference, b)
- `Projects/.../Save/GameSaveLoad.cpp` — `Quickload` `kResetFrame` branch, `ServerReset` (b)

## Out of scope

- The `mFrameInputs` wipe itself and the "advance sim-time by `mfLastDeltaTime` not `kfDeltaTime`" rule (`Server/CLAUDE.md`) — correct and untouched; (a) only stops draining request queues into the doomed map while paused.
- Reworking the new-subscription / resync persist-until-served queues — they are the model, not the target.
- Any wire/packet change — (b) reuses the existing `BroadcastLoadNotification` on its existing path.
- The `ServerBroadcaster` role-split and `mSpawns` rename (`Network/AuditSweepQuickWins.md`).

## Acceptance criteria

- A weapon-mode toggle sent while the server is paused is applied on the first unpaused tick (request survives the pause).
- Flagship updates re-queued by a load that lands on a paused cycle are not dropped.
- The local-menu `kResetFrame` fresh game emits a load notification and leaves `mClientOwnedPlayerIds`/`authorizedCoords` consistent for connected clients (no CRC-mismatch-only recovery), matching networked `ServerReset`.

## Notes

- **Invariant exposure.** Server tick-orchestration semantics (when request queues are drained relative to the pause gate) and the fresh-game reset flow. No wire-format change, no CRC/`kiVersion`/determinism-math change. Gameplay-visible → needs playtest.
- **Grill decision (a):** the pause gate must sit before `iFullTicks` is known, so gate on the timestep/pause flag (available at `PreTickNetwork` time), not `iFullTicks` directly. Open sub-question: gate each drain + the `PreTickNetwork` clear individually (minimal diff) vs. a single "is-paused → skip request processing" guard spanning the drains and the clear (cleaner, but must not also skip the sim-time-progression calls like `TickFleetTimers` that are already correctly `mfLastDeltaTime`-scaled). Default: minimal per-site gating.
- **Grill decision (b):** inline the missing calls into the `Quickload` branch (minimal) vs. extract a shared `FreshGameReset()` helper that both `ServerReset` and the menu path call (prevents future drift; recommended). Default: extract the helper.
