# Server Pause Request-Preservation Semantics

## Context

Server tick-orchestration gap surfaced by the Network audit, verified against current control flow: requests injected/drained while the server is paused (`iFullTicks == 0`) are silently lost because `BuildFrameInputs` wipes `mFrameInputs` every `ServerUpdate` and no tick consumes them while paused. Does not touch the wire format.

**Decision (2026-07-03):** the former item (b) — completing the local debug-menu fresh-game path (`kResetFrame` without `kQuickload` in `GameSaveLoad::Quickload`, GameSaveLoad.cpp:152-157) — is **dropped as moot**. That branch is unreachable on the server build: the `MenuInput` passed to `ServerUpdate` is always default-constructed (`Main.cpp:270-284`; `ProcessInput` runs only under `BT_CLIENT`), so `kResetFrame`/`kQuickload`/`kQuicksave` are never set. `Engine/Architecture_GameBaseDeadVirtuals.md` **owns deleting** that whole path (`GameSaveLoad::Quicksave`/`Quickload(MenuInput)` bodies + declarations, and the `ServerUpdate` call sites); after that deletion the only fresh-game entry point is `GameSaveLoad::ServerReset()` (GameSaveLoad.cpp:66-78), which already runs the complete sequence (`CreateNewFrame` → `SetNextGlobalId(1)` → `Reset` → `ResetState` → `ResetClientsForLoad` → `ComputeActiveSet`). This plan must NOT touch `GameSaveLoad::Quickload` — if it executes before the GameBase plan, leave the dead branch alone. A shared `FreshGameReset()` helper is likewise dropped (YAGNI: one caller remains).

## Design

### Preserve requests received while paused

**Mechanism (verified):** `GameBase::ServerUpdate` calls `PrepareActiveSet()` → `BuildFrameInputs` every cycle, independent of `iFullTicks` (`GameBase.cpp:123,441,450`). `ServerBroadcaster::BuildFrameInputs` (`ServerBroadcaster.cpp:17`) starts with `gpGame->mFrameInputs.clear()` (`:22`). The consuming loop `for (i < iFullTicks)` (`GameBase.cpp:128`, consumption at `:181`/`:235-238`) never runs at `iFullTicks == 0`, so the next call wipes the untouched map. Two drains feed that doomed map:
- **`ProcessFlagshipUpdates`** (→ `FleetNavigationController::ProcessFlagshipUpdates`; line citation stale, re-verify against current `ServerBroadcaster.cpp`) genuinely drains its own queue — `mPendingFlagshipUpdates.clear()` (`FleetNavigationController.cpp:181`) — so a paused update loses the `kUpdateFleet` from **both** the queue and `mFrameInputs`. This also drops the flagship updates the load path re-queues via `OnResetForLoad` if the load lands on a paused cycle (self-heals slowly via timer refire).
- **`ProcessUpdatePlayerRequests`** (line citation stale, re-verify against current `ServerBroadcaster.cpp`) injects `kUpdatePlayer` (weapon-mode toggles) but does **not** clear `mPendingUpdatePlayerRequests` itself; that queue is cleared at the **start** of the next `PreTickNetwork` via `mpBroadcaster->ClearPendingRequests()` (`ServerSession.cpp:282` → `ServerBroadcaster.cpp:266-269`) and refilled from packets each poll (`QueueUpdatePlayerRequest`, `ServerSession.cpp:140`). Net lifetime one cycle → a toggle sent while paused is injected, wiped, then its request cleared before any tick sees it (user must retoggle).

**Fix — Decision (2026-07-03): single pause-flag policy, two edit sites, `FleetNavigationController` untouched.** Make both request classes persist-until-served while paused, matching the existing pattern for the new-subscription and resync queues (they explicitly stay queued across polls when paused — `Engine/Source/Network/CLAUDE.md:15`, `ServerSessionBase.cpp:90`, consumers clear only after servicing). Gate on `gpGame->mGameFlags & engine::GameFlags::kPaused` (the flag `GameBase::ServerUpdate` reads at `GameBase.cpp:137` to zero `iFullTicks`), not `iFullTicks` — the gate sites run before `iFullTicks` is computed.

1. **`ServerSession::PreTickNetwork` (`ServerSession.cpp:282`):** skip `mpBroadcaster->ClearPendingRequests()` while paused so `mPendingUpdatePlayerRequests` survives. (`mpFleetManager->ClearPendingRequests()` on the next line is a different queue family — network fleet CRUD requests refilled each poll — leave it as is.)
2. **`ServerBroadcaster::BuildFrameInputs` (`ServerBroadcaster.cpp:17-113`):** cache `const bool bPaused = ...` once, then wrap the injection work in two `if (!bPaused)` blocks that preserve current statement order around the unconditional `TickFleetTimers()` (must keep running every cycle — it is `mfLastDeltaTime`-scaled and self-quiescent during pause, per `Server/CLAUDE.md`):
   - Block 1 (before `TickFleetTimers`): the waiting-spawn loop (`:32-50`) and `ProcessUpdatePlayerRequests()`. NOTE (2026-07-09, unrelated to this drift-refresh's own plan): a "pending-destroy loop" this plan expected between the waiting-spawn loop and `ProcessUpdatePlayerRequests()` no longer exists in current source (removed by `Network/DeadMachinerySweep.md`, landed before this citation refresh) — re-verify this block's contents against current source before executing, line citations here are unreliable pending that.
   - Block 2 (after `TickFleetTimers`): `ProcessFlagshipUpdates()`, the `mSpawns` save loop (`:96-102`), and the pre-spawn snapshot refresh (`:104-112`). Also confirm whether the AgentHarness3-added agent-injection block (`ServerBroadcaster.cpp:65-93`, gated on `gpGame->mfLastDeltaTime > 0.0f`) needs the same pause-deferral treatment this plan is designing, or is already handled by it — check before executing.

   Do **not** reorder `TickFleetTimers` relative to the spawn loop — spawns deliberately read the pre-timer-fire fleet `wantedCoord`. Skipping `ProcessFlagshipUpdates()` wholesale (rather than gating its internal `mPendingFlagshipUpdates.clear()` at `FleetNavigationController.cpp:181`) keeps the drain+clear pairing intact and leaves `FleetNavigationController` out of the diff. Skipping the spawn loop also resolves the id-minting rider below for free. Skipping the snapshot refresh is safe: its only consumer, `FinalizeNewClients`, runs from `BroadcastTick` (`ServerSession.cpp:78`) inside the per-tick loop, which never executes while paused.

**Pause-flag edge (analyzed — do not "fix"):** the `PreTickNetwork` gate reads `kPaused` before `ParseReceivedGamePackets` can toggle it this cycle. Both transitions are safe: pause turning ON this cycle → the clear ran, but last cycle's requests were already consumed by last cycle's (unpaused) tick; pause turning OFF this cycle → the clear is skipped, the surviving queue is injected by this cycle's (now unpaused) `BuildFrameInputs`, consumed by this cycle's tick, and cleared next cycle. No double-apply: injection only happens on unpaused cycles.

**Rider (covered by Block 1):** the waiting-spawn loop mints `gpGame->GenerateGlobalId()` per `BuildFrameInputs` call (`ServerBroadcaster.cpp:34`), so a client stuck in `mClientsWaitingForSpawn` across paused cycles burns one monotonic int64 id per cycle (waste, not a lost spawn). The Block 1 gate removes it.

## Critical files

- `Projects/.../Network/Server/ServerBroadcaster.cpp` — `BuildFrameInputs` (two `if (!bPaused)` blocks)
- `Projects/.../Network/Server/ServerSession.cpp` — `PreTickNetwork` (gate the `mpBroadcaster->ClearPendingRequests()` call)

## Out of scope

- The `mFrameInputs` wipe itself and the "advance sim-time by `mfLastDeltaTime` not `kfDeltaTime`" rule (`Server/CLAUDE.md`) — correct and untouched; this plan only stops draining request queues into the doomed map while paused.
- Reworking the new-subscription / resync persist-until-served queues — they are the model, not the target.
- `FleetNavigationController.cpp` — deliberately untouched (see fix rationale).
- `GameSaveLoad::Quickload`/`Quicksave` and the `kResetFrame` fresh-game branch — owned by `Engine/Architecture_GameBaseDeadVirtuals.md` (deletion); see Context decision note.
- Any wire/packet change.
- The `ServerBroadcaster` role-split and `mSpawns` rename (`Network/AuditSweepQuickWins.md`).
- The `mPendingPlayerDestroys` queue — removed by `Network/DeadMachinerySweep.md`; no longer relevant.

## Acceptance criteria

- A weapon-mode toggle sent while the server is paused is applied on the first unpaused tick (request survives the pause).
- Flagship updates queued while paused (e.g. re-queued by a load that lands on a paused cycle, or by fleet spawn/death bookkeeping) are not dropped.
- No global ids are minted for waiting spawns during paused cycles.

## Notes

- **Invariant exposure.** Server tick-orchestration semantics (when request queues are drained relative to the pause gate). No wire-format change, no CRC/`kiVersion`/determinism-math change. Gameplay-visible → needs playtest.
- **Decision (2026-07-03) — gate granularity:** resolved to the two-block `BuildFrameInputs` guard + one gated clear in `PreTickNetwork` (design above). Rationale: one cached flag and two contiguous blocks beat four scattered per-site gates; it keeps `TickFleetTimers` unconditional, avoids splitting `ProcessFlagshipUpdates`' drain from its clear, and covers the spawn-id rider without extra code.
- **Decision (2026-07-03) — fresh-game reset / `FreshGameReset()` helper:** dropped as moot; see Context. Cross-plan ownership: `Engine/Architecture_GameBaseDeadVirtuals.md` owns all `GameSaveLoad::Quicksave`/`Quickload` edits; this plan owns all `ServerBroadcaster.cpp`/`ServerSession.cpp` pause-gating edits. No shared edit sites remain between the two plans.
