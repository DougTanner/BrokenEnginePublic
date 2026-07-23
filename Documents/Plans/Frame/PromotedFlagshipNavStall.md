<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Promoted Flagship Holds Flagship-Follow Nav Mode

## Context

When a flagship dies, the wingman promoted in its place is blocked from **both** exits out of flagship-follow navigation (mode 5) and holds that mode for up to `Fleet::fNavigationDelay` (60 s default, `Fleet.h:39`). The whole fleet loiters at the death site behind it.

Verified chain:

1. `FleetNavigationController::ShiftFlagshipAfterDeath` (`FleetNavigationController.cpp:212-215`) sets `rFleet.iFlagshipIndex = iNewFlagship`, then `rFleet.wantedCoord = rFleet.members.at(iNewFlagship).coord` — the fleet's wanted coord becomes **the promoted ship's own coord** — and resets `rFleet.fFrameChangeTimer = rFleet.fNavigationDelay`.
2. `ProcessFlagshipUpdates` drains that into a `kUpdateFleet`; the Spawn-phase handler (`Players.cpp:294-307`) sets `kIsFlagship` on the promoted ship and writes that same coord into `pFleetWantedCoords`.
3. In `PlayersPostRender::ComputeNavigation`, the flagship-proximity block is guarded `!(flags & kIsFlagship) && (fleetWantedCoord == rStaticData.coord)` (`PlayersNavigation.cpp:215`) — the promoted ship now fails the first half.
4. The fleet-override block is guarded `!(fleetWantedCoord == rStaticData.coord)` (`PlayersNavigation.cpp:173`) — it fails that too, because its wanted coord *is* its own coord.
5. Nothing else in `ComputeNavigation` leaves mode 5: the frame-change-timer path (`PlayersNavigation.cpp:250`) is mode `-1` only, and the tail `else if (riNavDirection >= 0)` handles modes 0-3.

The promoted ship therefore keeps pathfinding to `rVecIslandDestination`, still pinned to the dead flagship's last position, until `FleetNavigationController::TickFleetTimers` (`FleetNavigationController.cpp:32-113`) fires after `fNavigationDelay` and issues a new cardinal wanted coord. Its flagship gate (`FleetNavigationController.cpp:96`, `!bFoundFlagship || (iFlagshipNavDirection >= 0 && iFlagshipNavDirection <= 3)`) does pass mode 5, so recovery does eventually happen — after up to 60 s. The surviving wingmen follow the now-stationary promoted ship, so the fleet loiters as a group.

Deterministic on both sides (the client replays the same `kUpdateFleet`), so this is a stuck-AI defect, not a desync.

The follower case is **not** this defect and is already resolved: a plain wingman recovers in one tick once the reassignment lands, as recorded in the mode-5 comment (`PlayersNavigation.cpp:286-296`) and `Frame/Collections/Players/AGENTS.md`. That comment names this promoted-ship case as the known, unfixed exception; this plan is its acceptance gap.

## Design

Give the promoted ship a bounded exit from mode 5. Two shapes, with materially different invariant exposure — **resolve at `/external-grill-plan` before implementing**:

- **Option A — frame-local exit (recommended for behavior; costs a version bump).** In `ComputeNavigation`, add the missing promoted-flagship case: when the entity carries `kIsFlagship`, is in mode 5, and `fleetWantedCoord == rStaticData.coord`, flip to island-seek (mode 4) and set `rVecIslandDestination = XMVectorZero()`. Recovery is one tick and needs no server round trip. **RNG draw parity is preserved exactly**: mode 5 draws 3 per tick (`PlayersNavigation.cpp:301,304,305`) and mode 4's entry block draws 3 (`:342,353,354`) gated on `XMVectorGetW(rVecIslandDestination) == 0.0f` (`:334`), so zeroing the destination on the flip tick keeps the count at 3. This changes CRC'd nav flags — see Notes.
- **Option B — server-side timer release (no version bump).** Leave `ComputeNavigation` untouched and change `ShiftFlagshipAfterDeath` so the promoted ship is released promptly — e.g. do not re-arm `fFrameChangeTimer` to the full `fNavigationDelay`, letting `TickFleetTimers` issue a fresh cardinal within a tick or two (its `:96` gate already admits a mode-5 flagship). Because `kUpdateFleet` reaches the sim only as recorded `FrameInput` status changes, changing *when* the server emits them alters no frame code and therefore does **not** shift the CRC of an existing replay — no `Frame::kiVersion` bump. Cost: `wantedCoord` is fleet-wide, so releasing the timer also cuts short the rally window that aiming `wantedCoord` at the promoted ship was presumably meant to give out-of-cell members. Quantify that tradeoff before choosing.

Do not introduce new SOA state or new RNG draws under either option.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp` — `PlayersPostRender::ComputeNavigation`: fleet-override block (`:172-212`), flagship-proximity block (`:214-247`), mode-5 branch and its 3-draw mirror (`:283-330`), mode-4 entry draws (`:331-355`). Option A edits the proximity-block guard; Option B leaves this file unchanged apart from the stale comment.
- `Projects/BrokenEngineSandbox/Source/Network/Server/FleetNavigationController.cpp` — `ShiftFlagshipAfterDeath` (`:195-216`, the `wantedCoord`/timer assignment), `TickFleetTimers` (`:32-113`, the 60 s recovery path and its `:96` nav-direction gate). Option B edits this file.
- `Projects/BrokenEngineSandbox/Source/Fleet.h` — `Fleet::fNavigationDelay` (`:39`), `Fleet::iFlagshipIndex` (read only; the stall duration).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp` — `kUpdateFleet` handler (`:286-310`) writing `kIsFlagship` and `pFleetWantedCoords` (read only; establishes the state the guards see).
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` — `Frame::kiVersion` base (`:36`, currently `120 +`). **Option A only.**
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/AGENTS.md` — the navigation invariant naming the promoted ship as the unfixed exception; update to match whichever option lands.

## Out of scope

- The plain-follower case — already correct (one-tick recovery via the reassignment landing in the next Spawn phase); do not re-add a frame-local no-flagship fallback for it.
- Agent-injected players that belong to no `Fleet` and therefore never receive a `kUpdateFleet` at all — harness-only, named in the same mode-5 comment, separate concern.
- Flagship *selection* policy (which surviving member is promoted) — `ShiftFlagshipAfterDeath`'s round-robin scan is unchanged.
- Mode 4/5 steering behavior itself (`NavQueryDirection` contour following) — unchanged.
- Reliability of `kUpdateFleet` delivery when a member's coord cannot be resolved — owned by `Documents/Plans/Network/FlagshipUpdateDeliveryRetry.md`.
- Changing `fNavigationDelay`'s default or making it per-member.

## Acceptance criteria

- After a flagship dies and a wingman is promoted, the promoted ship leaves nav mode 5 within a bounded, observably short window (one tick under Option A; within the ticks it takes `TickFleetTimers` to re-fire under Option B) — not up to `fNavigationDelay`.
- The surviving fleet no longer loiters at the death site for the navigation-delay window; members resume island-seek or cardinal transit.
- Client and server produce identical nav modes and identical RNG draw counts across the promotion tick and the tick after — verified by frame-CRC parity in a live client/server harness run through a scripted flagship death (see `/agent-harness`).
- Under Option A only: `Frame::kiVersion`'s base is bumped so a save/replay written by pre-fix code is rejected as version-incompatible instead of false-desyncing on the shifted nav flags.
- Under Option B only: an existing replay recorded before the change still validates (frame code unchanged), confirmed by a replay-determinism harness run.

## Coordination

- **Frame version/save/replay batch — applies only if Option A is chosen.** `pFlags` is in `PlayersPostRender::SharedCrcMembers()` (`Players.h:295-297`), so a frame-local mode transition shifts computed frame CRCs and, per the rule at `Frame.cpp:33-35`, requires bumping the `120 +` base of `Frame::kiVersion`, invalidating existing saves and replays. Co-land behind one consolidated `Frame::kiVersion` change with `Documents/Plans/Frame/PlayerTransferUuidPreservation.md`. The cell-transfer sentinel plan that previously anchored this batch has landed and consumed its own collection `kiVersion` bumps, so each remaining member owns its own increment of the `Frame.cpp:36` base rather than deferring to a last lander. Option B carries no bump and leaves that batch untouched — record the chosen option in this section at grill time so the batch members are not left expecting a co-land that will not happen.
- The cell-transfer sentinel work that shared `PlayersNavigation.cpp`'s player `TransferRequest` build region has landed, so there is nothing left to co-schedule there. It left `PlayersNavigation.cpp` itself untouched but reshaped the arrival contract — `Spawn` now restores carried state under a `SpawnInfo::bTransfer` flag rather than through magnitude sentinels — so re-resolve this plan's transfer-region citations against the landed tree before implementing.

## Notes

- **Invariant exposure.** Inside the `/fp:strict` CRC'd tick. Option A changes CRC'd nav-flag transitions → replay-visible, `Frame::kiVersion` base bump required, client and server land together; no wire, `.pack`, layout, or threading exposure under either option. Option B is server-only (`BT_SERVER`) fleet orchestration with no frame-code change and therefore no version bump.
- **RNG-stream hazard is the main correctness bar for Option A.** Any mode flip must keep the per-tick draw count identical on both builds; the 3-draw mirror at `PlayersNavigation.cpp:297-305` exists precisely for this and must stay intact. The flip conditions read only shared state (`pFlags`, `pFleetWantedCoords`, `rStaticData.coord`), so both sides evaluate them identically.
- **Pre-staged decision for `/external-grill-plan`:** Option A vs Option B — specifically, is a one-tick frame-local recovery worth a save/replay invalidation when a server-side timer release achieves bounded recovery for free, and what rally window (if any) must `wantedCoord`-at-own-coord preserve for out-of-cell members?
- Verification needs a live client/server harness scenario (fleet with a flagship and at least two wingmen, scripted flagship kill, then nav-mode and CRC readback), not inspection alone.
