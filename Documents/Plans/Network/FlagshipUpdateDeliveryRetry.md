<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Flagship Reassignment Dropped for Unresolvable Members

## Context

`FleetNavigationController::ProcessFlagshipUpdates` (`FleetNavigationController.cpp:115-183`) delivers each queued `PendingFlagshipUpdate` to every alive fleet member exactly once, then clears the queue unconditionally at `:182`. A member that cannot be resolved on that single attempt silently loses the update — there is no retry and no diagnostic beyond the `iMembersUpdated` count in the `kVerbose` log at `:179-180`.

Three per-member skips reach the unconditional clear:

- member coord absent from `gpGame->mFrameInputs` (`:149-153`);
- member coord absent from `gpGame->mCoordFrames` (`:154-157`);
- `globalPlayerId` not found while scanning that frame's `PlayersPostRender::pGlobalPlayerIds` (`:160-177` — the inner loop simply ends without a match).

`FleetMember::coord` goes stale between transfers: `ServerFleetManager::OnPlayerTransferred` (`ServerFleetManager.cpp:299-319`) is the only site that refreshes it, and it runs on transfer completion. A member mid-transfer at the tick a flagship update drains is therefore the realistic trigger — its recorded coord no longer holds its player row, so the scan misses and the update is dropped for that member. Related suppression on the detection side: `ServerClientManager::DetectPlayerDeaths` skips death detection entirely for a client with a pending subscription update (`ServerClientManager.cpp:298-302`), so the two mid-transfer suppressions overlap in exactly the window that produces flagship churn.

Consequence when the dropped update is the **promotion** entry pushed by `ShiftFlagshipAfterDeath` (`FleetNavigationController.cpp:215`): no player row in the fleet carries `kIsFlagship`, because `bIsFlagship` is computed per member at `:147` and only the delivered rows are written. Followers' flagship-proximity scan finds nothing and the fleet has no leader until `TickFleetTimers` re-fires after `Fleet::fNavigationDelay` (`Fleet.h:39`, 60 s default) and enqueues a fresh update that happens to resolve. The state is self-healing but only on that 60 s cadence, and only if the member resolves on the retry attempt too.

Server-only orchestration: the drop changes which `kUpdateFleet` status changes enter `FrameInput`, and both sides consume the same recorded inputs, so this is a fleet-behavior defect, not a desync.

## Design

Make delivery of a pending flagship update survive a member that is momentarily unresolvable, without letting stale updates accumulate or apply out of order.

- Retain an entry whose delivery was incomplete instead of clearing the whole queue at `:182` — for example, drain into a retained list of undelivered `{update, member}` work and re-attempt on subsequent `ProcessFlagshipUpdates` calls.
- Bound the retention. An entry must expire (tick budget or attempt count) so a permanently gone member cannot pin it forever, and a newer update for the same `{clientGuid, iFleetIndex}` must supersede an older retained one rather than being applied after it — `newWantedCoord` and `uiPendingFleetWantedCoordTicks` are last-writer state, so replaying a stale entry after a newer one would re-issue an obsolete wanted coord.
- Recompute `bIsFlagship` from the **current** `rFleet.iFlagshipIndex` at each delivery attempt, not from a value captured when the entry was queued — the flagship can shift again between attempts.
- Consider whether the cheaper correct fix is refreshing `FleetMember::coord` at the point the member becomes unresolvable (resolving the member by `globalPlayerId` across live coord frames) rather than retrying later. Weigh both at plan execution; the retry path is the one that also covers a coord that is simply not in `mFrameInputs` this tick.

Keep the `kVerbose` delivery log informative enough to distinguish "delivered to all alive members" from "retained N".

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/FleetNavigationController.cpp` — `ProcessFlagshipUpdates` (`:115-183`): the three per-member skips (`:149-153`, `:154-157`, `:160-177`), the per-member `bIsFlagship` computation (`:147`), and the unconditional `mPendingFlagshipUpdates.clear()` (`:182`). Also `TickFleetTimers` (`:32-113`) and `ShiftFlagshipAfterDeath` (`:195-216`) as the two enqueue sites, and `QueueFlagshipUpdate` / `ClearPendingFlagshipUpdates` (`:185-193`) which any retained-state addition must also reset.
- `Projects/BrokenEngineSandbox/Source/Network/Server/FleetNavigationController.h` — `PendingFlagshipUpdate` and `mPendingFlagshipUpdates`; the owner of any retained-delivery state and its load-reset behavior.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp` — `OnPlayerTransferred` (`:299-319`, the sole `FleetMember::coord` refresh), `ProcessFlagshipUpdates` (`:163`) and `TickFleetTimers` (`:155`) forwarding wrappers, `OnPlayerDeath` (`:212`).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp` — `DetectPlayerDeaths` mid-transfer skip (`:298-302`), context for the overlapping suppression window.
- `Projects/BrokenEngineSandbox/Source/Fleet.h` — `FleetMember::coord`, `Fleet::iFlagshipIndex` (`:36`), `Fleet::fNavigationDelay` (`:39`).

## Out of scope

- The nav-mode stall the promoted ship itself suffers once it *does* receive its update — owned by `Documents/Plans/Frame/PromotedFlagshipNavStall.md`.
- Flagship selection policy in `ShiftFlagshipAfterDeath` — unchanged.
- Making `FleetMember::coord` continuously authoritative (a general fleet-member coord-tracking rework) — this plan only needs delivery to survive a stale coord, not to eliminate staleness.
- The `mClientOwnedPlayerIds` / `authorizedCoords` parallel-vector registry refactor — `Documents/Plans/Network/Refactor_ServerClientPlayerRegistry.md`.
- The `DetectPlayerDeaths` mid-transfer death-detection skip itself — cited as the overlapping trigger window, not changed here.
- Any wire-format or client-side change: `kUpdateFleet` payload and `UpdateFleetData` are untouched.

## Acceptance criteria

- A fleet member unresolvable on the tick a flagship update drains still receives that update once it becomes resolvable, within a bounded number of ticks — not on the next 60 s `fNavigationDelay` cadence.
- A promotion after a flagship death results in exactly one player row carrying `kIsFlagship` for that fleet, even when one member is mid-transfer at the draining tick.
- A retained entry cannot outlive its bound, and a newer update for the same fleet always wins over an older retained one (no obsolete `newWantedCoord` re-issued after a newer one).
- Load reset still clears all pending and retained flagship-update state (`ClearPendingFlagshipUpdates` path) — no entry survives a save load.
- Server build compiles; nominal flagship death, promotion, and fleet navigation behave identically when no member is unresolvable.

## Notes

- **Invariant exposure: server-only (`BT_SERVER`) fleet orchestration.** No wire format, no `.pack`, no collection layout, no `Frame::kiVersion` change. `ProcessFlagshipUpdates` writes into `gpGame->mFrameInputs[...].statusChanges`, which feed the CRC'd tick — but as *inputs*, and both sides consume the same recorded `FrameInput`, so changing delivery timing shifts no replay CRC and needs no version bump. Order within a tick's `statusChanges` is the deterministic-grouping contract's concern (`Network/AGENTS.md`); a retry must not reorder changes already appended for a coord this tick.
- Runs inside `BuildFrameInputs`' advancing-update-only work (`Network/Server/AGENTS.md`, "Deterministic Tick Contracts") — retained entries must remain deferred through paused and zero-tick updates exactly as pending ones do today, or a paused server will drain retries against frames it has not built.
- Any new retained container is a heap allocation on the server tick path; place it under the existing `ScopedSuppressAllocationTracking` conventions used by the surrounding fleet-manager sites.
- Verification wants a live harness scenario: fleet with a mid-transfer member at the tick of a flagship death, then a `kIsFlagship` readback across the fleet (see `/agent-harness`).
