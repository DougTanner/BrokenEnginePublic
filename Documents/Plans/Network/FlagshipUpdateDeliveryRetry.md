<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Flagship Reassignment Dropped for Unresolvable Members

## Context

`FleetNavigationController::ProcessFlagshipUpdates` (`Projects/BrokenEngineSandbox/Source/Network/Server/FleetNavigationController.cpp:115-183`) delivers each queued `PendingFlagshipUpdate` to every alive fleet member exactly once, then clears the queue unconditionally at `:182`. A member that cannot be resolved on that single attempt silently loses the update — there is no retry and no diagnostic beyond the `iMembersUpdated` count in the `kVerbose` log at `:179-180`.

Three per-member skips reach the unconditional clear:

- member coord absent from `gpGame->mFrameInputs` (`:149-153`);
- member coord absent from `gpGame->mCoordFrames` (`:154-157`);
- `globalPlayerId` not found while scanning that frame's `PlayersPostRender::pGlobalPlayerIds` (`:160-177` — the inner loop simply ends without a match).

`FleetMember::coord` goes stale between transfers: `ServerFleetManager::OnPlayerTransferred` (`ServerFleetManager.cpp:299-319`) is the only site that refreshes it, and it runs on transfer completion. A member mid-transfer at the tick a flagship update drains is therefore the realistic trigger — its recorded coord no longer holds its player row, so the scan misses and the update is dropped for that member. Related suppression on the detection side: `ServerClientManager::DetectPlayerDeaths` skips death detection entirely for a client with a pending subscription update (`ServerClientManager.cpp:298-302`), so the two mid-transfer suppressions overlap in exactly the window that produces flagship churn.

Consequence when the dropped update is the **promotion** entry pushed by `ShiftFlagshipAfterDeath` (`FleetNavigationController.cpp:215`): no player row in the fleet carries `kIsFlagship`, because `bMemberIsFlagship` is computed per member at `:147` and only the delivered rows are written. Followers' flagship-proximity scan finds nothing and the fleet has no leader until `TickFleetTimers` re-fires after `Fleet::fNavigationDelay` (`Fleet.h:39`, 60 s default) and enqueues a fresh update that happens to resolve. The state is self-healing but only on that 60 s cadence, and only if the member resolves on the retry attempt too.

Server-only orchestration: the drop changes which `kUpdateFleet` status changes enter `FrameInput`, and both sides consume the same recorded inputs, so this is a fleet-behavior defect, not a desync.

## Design

Make delivery of a pending flagship update survive a member that is momentarily unresolvable, by retaining undelivered per-member work and re-attempting it on subsequent `ProcessFlagshipUpdates` calls, without letting stale updates accumulate or apply out of order. (A coord-refresh-at-miss alternative — resolving the member by `globalPlayerId` across live coord frames instead of retrying — was considered and rejected: it does not cover a coord that is simply absent from `mFrameInputs` on the draining tick, which the retry path does. This plan implements the retry path.)

### Retained state

Add to `FleetNavigationController` (`FleetNavigationController.h`), beside `mPendingFlagshipUpdates`, a retained-delivery container of entries, one per `{clientGuid, iFleetIndex}`, each holding:

- the update payload: `newWantedCoord`, `uiPendingFleetWantedCoordTicks`;
- the list of `globalPlayerId`s still undelivered;
- a remaining-attempt budget, initialized to `engine::kiTickRate` (one second of advancing ticks — well under the 60 s `fNavigationDelay` fallback cadence).

Exact struct/member naming is the implementer's choice.

### `ProcessFlagshipUpdates` flow

Rework the function body to, in order:

1. **Supersede** — drop any retained entry whose `{clientGuid, iFleetIndex}` also appears in the fresh `mPendingFlagshipUpdates` queue. `newWantedCoord` and `uiPendingFleetWantedCoordTicks` are last-writer state; replaying a stale entry after (or in the same tick as) a newer one would re-issue an obsolete wanted coord. This ordering guarantees no obsolete coord is ever appended to a coord's `statusChanges` after a newer one.
2. **Re-attempt retained entries** — for each surviving entry, re-validate the fleet exists (`rFleets` lookup, `iFleetIndex` bound, `iFlagshipIndex` bound, mirroring the existing guards at `:119-134`; drop the entry if not), then run the existing per-member resolution (`:146-177`) for each still-undelivered `globalPlayerId`: skip dead members (drop them from the entry), read the member's **current** `rMember.coord` (which `OnPlayerTransferred` refreshes on transfer completion, so a completed transfer makes the retry succeed), and recompute `bMemberIsFlagship` from the **current** `rFleet.iFlagshipIndex` at each attempt — never from a value captured when the entry was queued, because the flagship can shift again between attempts. Remove each delivered member from the entry; remove the entry when its member list empties. Decrement the attempt budget once per call; when it reaches zero, drop the entry and emit a `kWarning` log naming the guid, fleet index, and undelivered member count.
3. **Drain the fresh queue** — the existing loop at `:117-181`, unchanged except that an alive member skipped by any of the three skips (`:149-153`, `:154-157`, `:160-177` no-match) is recorded into a new retained entry for that update instead of being silently lost. Keep the unconditional `mPendingFlagshipUpdates.clear()` at `:182`.

Extend the `kVerbose` log at `:179-180` with the retained-member count so it distinguishes "delivered to all alive members" from "retained N".

### Reset and allocation

- `ClearPendingFlagshipUpdates` (`:190-193`) also clears the retained container. Its sole caller is the load-reset path (`ServerSession.cpp:580`), so no `ServerSession` edit is needed — no entry survives a save load.
- Retained-container growth occurs only inside `FleetNavigationController::ProcessFlagshipUpdates`, whose sole caller `ServerFleetManager::ProcessFlagshipUpdates` (`ServerFleetManager.cpp:163-169`) already holds `ScopedSuppressAllocationTracking`; update its `// Heap:` comment to mention retained-entry growth. No new suppress scope.
- Retained entries are touched only inside `ProcessFlagshipUpdates`, which `BuildFrameInputs` calls only on advancing updates, so they remain deferred through paused and zero-tick updates exactly as pending ones do today (`Network/Server/AGENTS.md`, "Deterministic Tick Contracts").

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the acceptance criteria, and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (includes, declarations) the named change requires.

**In scope:**

- `Projects/BrokenEngineSandbox/Source/Network/Server/FleetNavigationController.h` — add the retained-entry struct and the retained container member to `FleetNavigationController`, beside `mPendingFlagshipUpdates`.
- `Projects/BrokenEngineSandbox/Source/Network/Server/FleetNavigationController.cpp` — `ProcessFlagshipUpdates` (`:115-183`): the supersede/re-attempt/drain flow above, the retained-entry recording at the three per-member skips, the per-attempt `bMemberIsFlagship` recomputation, and the extended `kVerbose` log; `ClearPendingFlagshipUpdates` (`:190-193`): clear the retained container.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp` — `ProcessFlagshipUpdates` (`:163-169`): `// Heap:` comment update only.

**Out of scope:**

- The nav-mode stall the promoted ship itself suffers once it *does* receive its update — owned by `Documents/Plans/Frame/PromotedFlagshipNavStall.md`.
- Flagship selection policy in `ShiftFlagshipAfterDeath` — unchanged, as are `TickFleetTimers`, `QueueFlagshipUpdate`, and their enqueue call sites (`FleetNavigationController.cpp:109`, `:215`; `ServerFleetManager.cpp:295`).
- Making `FleetMember::coord` continuously authoritative (a general fleet-member coord-tracking rework) — this plan only needs delivery to survive a stale coord, not to eliminate staleness. `OnPlayerTransferred` is unchanged.
- The `mClientOwnedPlayerIds` / `authorizedCoords` parallel-vector registry refactor — completed separately.
- The `DetectPlayerDeaths` mid-transfer death-detection skip itself (`ServerClientManager.cpp:298-302`) — cited as the overlapping trigger window, not changed here.
- Any wire-format or client-side change: `kUpdateFleet` payload and `UpdateFleetData` are untouched.
- `ServerSession.cpp` — the load-reset call site is reached through `ClearPendingFlagshipUpdates` and needs no edit.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/FleetNavigationController.cpp` — `ProcessFlagshipUpdates` (`:115-183`), `ClearPendingFlagshipUpdates` (`:190-193`); read-only context: `TickFleetTimers` (`:32-113`) and `ShiftFlagshipAfterDeath` (`:195-216`) as enqueue sites.
- `Projects/BrokenEngineSandbox/Source/Network/Server/FleetNavigationController.h` — `PendingFlagshipUpdate`, `mPendingFlagshipUpdates`; owner of the new retained-delivery state.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp` — `ProcessFlagshipUpdates` wrapper (`:163-169`, comment edit); read-only context: `OnPlayerTransferred` (`:299-319`, the sole `FleetMember::coord` refresh), `TickFleetTimers` wrapper (`:155-161`), `OnPlayerDeath` (`:212-244`).
- Read-only context: `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp` — `DetectPlayerDeaths` mid-transfer skip (`:298-302`); `Projects/BrokenEngineSandbox/Source/Fleet.h` — `FleetMember::coord`, `Fleet::iFlagshipIndex` (`:36`), `Fleet::fNavigationDelay` (`:39`); `ServerSession.cpp:580` load-reset call site.

## Risk tier

**Tier 2 — scoped behavior.** One subsystem's server-only (`BT_SERVER`) fleet-orchestration runtime behavior. Invariants preserved:

- No wire format, no `.pack`, no collection layout, no `Frame::kiVersion` change. `ProcessFlagshipUpdates` writes into `gpGame->mFrameInputs[...].statusChanges`, which feed the CRC'd tick — but as *inputs*, and both sides consume the same recorded `FrameInput`, so changing delivery timing shifts no replay CRC and needs no version bump.
- Order within a tick's `statusChanges` is the deterministic-grouping contract's concern (`Network/AGENTS.md`); the supersede-first ordering ensures a retry never re-issues an obsolete wanted coord after a newer one for the same fleet.
- Retained work runs only inside `BuildFrameInputs`' advancing-update-only path; paused and zero-tick updates never drain retries.
- Server tick path stays allocation-tracked; growth stays under the existing `ScopedSuppressAllocationTracking` in `ServerFleetManager::ProcessFlagshipUpdates`.

## Acceptance criteria

- A fleet member unresolvable on the tick a flagship update drains still receives that update once it becomes resolvable, within the `engine::kiTickRate`-attempt budget — not on the next 60 s `fNavigationDelay` cadence.
- A promotion after a flagship death results in exactly one player row carrying `kIsFlagship` for that fleet, even when one member is mid-transfer at the draining tick.
- A retained entry cannot outlive its attempt budget (dropped with a `kWarning` log), and a newer update for the same `{clientGuid, iFleetIndex}` always wins over an older retained one — no obsolete `newWantedCoord` is ever re-issued after a newer one.
- Load reset still clears all pending and retained flagship-update state through `ClearPendingFlagshipUpdates` — no entry survives a save load.
- Server build compiles; nominal flagship death, promotion, and fleet navigation behave identically when no member is unresolvable.

## Verification

Live harness scenario (see `/agent-harness`): fleet with a mid-transfer member at the tick of a flagship death, then a `kIsFlagship` readback across the fleet confirming exactly one flagship row within the retry budget.
