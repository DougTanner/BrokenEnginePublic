<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-03T22:18:18.000Z","dependsOn":[]} -->
# Player Transfer Update Loss

> **Claim restriction — re-plan before implementing.** This plan was rewritten on
> 2026-07-21 after its original design was disproven. It needs a full review /
> refactor / re-write pass before implementation, not mechanical execution of the
> text below.
>
> The filename still says `PlayerTransferUuidPreservation.md`; the uuid mechanism is
> gone and the name is now a misnomer. Renaming is a queue identity change (remove +
> add), so it was left alone — fold the rename into the re-write pass if worthwhile.

## Status

Rewritten 2026-07-21 following `/plan-audit` and `/external-grill-plan`; restructured
2026-07-24 to the current plan standard with every citation re-verified against the
working tree. The bug is confirmed and reproducible. The original design — carry the
collection uuid through transfer and re-add with `AddIndexableElementWithId` — is
**disproven and must not be revived**; see "Superseded claims" below. The mechanism
recommended here is evidence-backed but has not itself been through a plan audit.

## Context — the confirmed bug

`kUpdatePlayer` and `kUpdateFleet` mutate an existing player row, addressed by
collection uuid through `idToIndexMap` (`Players.cpp:312-332` and `286-310`). Both are
consumed in the **Spawn** phase, because `ProcessSpawnStatusChanges` (`Players.cpp:257`)
runs from `PlayersPostRender::Spawn` (`Players.cpp:387`).

The tick pipeline runs Update (phase 2) → Collision (3) → **Transfer (4)** → Destroy/Spawn
(5) (`FrameTick.cpp:65-89`). On the tick a player crosses a cell boundary, Transfer removes
its row (`PlayersPostRender::Transfer`, `PlayersNavigation.cpp:67`) before Spawn runs, so
the Spawn-phase lookup misses and the change is dropped with a `kWarning`
(`Players.cpp:329`). Dropped requests are not retried — both issuers clear their pending
lists after processing.

Practical impact: a `kUpdateFleet` issued on a member's crossing tick is lost until the
next flagship direction change, leaving a wingman navigating to the wrong cell for up to
tens of seconds; a `kUpdatePlayer` weapon-mode toggle is silently dropped. No desync —
both sides drop identically.

## Root cause — status changes are routed to the wrong phase

`ProcessSpawnStatusChanges` is a misnomer. It handles four status-change types whose
semantics map to three different phases, all executed in the entity-creation phase
because that is what the driving case (spawn) needed:

| Status change | Operation | Phase its contract matches | Runs in today |
|---|---|---|---|
| `kSpawnPlayer` / `kRespawnPlayer` | creates a row | Spawn | Spawn — correct |
| `kDestroyPlayer` | removes a row | Destroy | Spawn — one phase late |
| `kUpdatePlayer` / `kUpdateFleet` | mutates an existing row | Update | Spawn — three phases late |

The bug is a direct consequence: a mutation of an existing row is applied in the
creation phase, which runs after the removal phase.

The phases themselves are sound, and the pipeline already provides the needed hook.
`RunFrameTick` threads `FrameInput` into exactly two phases — `FramePostRender::Update`
and `FramePostRender::Spawn` (`FrameTick.cpp:73,86`) — but the Update-phase hook is
**dead**: `FramePostRender::Update` forwards `rFrameInput` to `FramePostRenderBase::Update`
(`Frame.cpp:167`), which marks it `[[maybe_unused]]` and never reads it
(`FrameBase.cpp:204-224`). Neither `ForEachPostRenderUpdate` overload takes it. Nothing in
phase 2 reads `statusChanges` at all.

So this is not a new split across phases — it is populating a deliberately provided,
currently empty hook.

## Design

Route the two update arms to the Update phase:

1. In `Players.cpp`, add a new static member function
   `PlayersPostRender::ProcessUpdateStatusChanges(Frame&, const FrameInput&, const engine::FrameStaticData&)`
   (signature parallel to `Spawn`, `Players.h:213`) that loops `rFrameInput.statusChanges`
   handling only `kUpdatePlayer` and `kUpdateFleet`. Move both arms verbatim out of
   `ProcessSpawnStatusChanges`, together with the fleet-log machinery that serves only
   the `kUpdateFleet` arm — the `iUpdateFleetCount` / `updateFleetNewCoord` /
   `iUpdateFleetFlagshipGlobalId` locals (`Players.cpp:262-264`) and the post-loop
   `kVerbose` LOG (`Players.cpp:379-382`). The `kWarning` at `Players.cpp:329` moves with
   its arm; update its `"ProcessSpawnStatusChanges::..."` message prefix to the new
   function name.
2. Call it from `FramePostRender::Update` **after** `PlayersPostRender::Update(...)`
   (`Frame.cpp:174`) and before `ForEachPostRenderUpdate` (`Frame.cpp:177`), using the
   already-plumbed `rFrameInput`.
3. `ProcessSpawnStatusChanges` keeps only the `kDestroyPlayer` and
   `kSpawnPlayer` / `kRespawnPlayer` arms.

Two mechanical constraints that decide correctness:

- **Ordering is load-bearing.** The pending-weapon-mode countdown decrements inside
  `PlayersPostRender::Update`'s per-player loop, reading *previous* and writing *current*
  (`Players.cpp:763,767,789-792,850,855`), while the update arms write *current* directly
  (`Players.cpp:307,323`). Calling after that loop preserves today's countdown semantics
  exactly. Calling before it would silently shorten every countdown by one tick.
- **Lookup validity.** `idToIndexMap` lives on Interpolate, is copied in phase 1, and no
  rows are added or removed before phase 5 — so it is stable and correct at Update time.

No wire change, no uuid field, no `TransferData` member, no phase-order change. The
existing `TransferData` fields already carry the resulting state to the destination cell
(`PlayersNavigation.cpp:104-116`), so the transfer case is covered without new
serialization: `uiPlayerFlags` carries `kPendingUseMissiles` and `kIsFlagship`, and
`uiPendingWeaponModeTicks`, `fleetWantedCoord`, `uiPendingFleetWantedCoordTicks`, and
`fNavigationDelay` all transfer.

After the fix the `kWarning` at `Players.cpp:329` becomes meaningful again: a
`kUpdatePlayer` miss is then a genuine anomaly rather than the expected transfer-tick drop.

Known gap to resolve during the re-write pass: `kUpdatePlayer` also writes
`pfFrameChangeTimers` (`Players.cpp:325`), which `TransferData` does **not** carry.
Arrival's magnitude select (`Players.cpp:469`) always falls through to the fresh random
draw because no transfer path sets `SpawnInfo::fFrameChangeTimer` — the documented legacy
non-defect in `Frame/Collections/AGENTS.md`. That is pre-existing behavior for every
transfer, not a regression, but decide explicitly whether it stays out of scope.

## Scope contract

The scope below is both target and ceiling: make the smallest complete change that
satisfies the acceptance criteria and add nothing else — no abstractions, configuration,
refactors, or fixes to adjacent code encountered along the way. Naming a file grants
permission to touch only the named regions, plus the mechanical necessities (includes,
declarations) the named change requires.

In scope:

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp` — inside
  `ProcessSpawnStatusChanges` (`:257`) only: remove the `kUpdateFleet` arm (`:286-310`),
  the `kUpdatePlayer` arm (`:312-332`), the fleet-log locals (`:262-264`), and the
  trailing fleet `kVerbose` LOG (`:379-382`); add the new
  `PlayersPostRender::ProcessUpdateStatusChanges` definition containing exactly that
  moved code. No other function in the file changes.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.h` — one
  declaration for `ProcessUpdateStatusChanges` in the `PlayersPostRender` phase-hook
  block (`:203-213`). Nothing else in the header changes; `kiVersion` (`:193`) is
  untouched.
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` — inside
  `FramePostRender::Update` (`:160-178`): one call site between `:174` and `:177`; and
  the `Frame::kiVersion` base literal bump `120` → `121` (`:36`). No other function
  changes.
- `Projects/BrokenEngineSandbox/Source/Frame/AGENTS.md` — adjust the Invariants bullets
  that describe status-change consumption ("Status changes are consumed here..." /
  "...transfer/spawn/destroy status changes apply immediately") to record that
  `kUpdatePlayer` / `kUpdateFleet` are consumed in the Update phase while spawn and
  destroy handling stays in Spawn.

Out of scope:

- `kDestroyPlayer`'s phase placement — also misrouted, but issuer-less dead machinery.
  (Previously routed to `Network/DeadMachinerySweep.md`, which no longer exists; leave
  it untouched here regardless.)
- Retry or queueing semantics in `ServerBroadcaster` / `FleetNavigationController` — the
  routing fix removes the need.
- Other collections' transfer handling — blasters/missiles/spaceships are not targeted by
  uuid-addressed status changes.
- The "0 = unset" sentinel fixes in the same functions — landed already:
  `PlayersPostRender::Spawn` now selects carried-vs-default arrival state on the
  `SpawnInfo::bTransfer` flag (`Players.h:341`, `Players.cpp:459-460`) rather than on
  field magnitude, so no sentinel work remains here.

## Risk tier and invariants

Tier 3 — determinism/CRC surface. **Invariant exposure: high.** Moving CRC'd mutations
one phase earlier shifts computed frame CRCs, so the `Frame.cpp:36` base bumps and
saves/replays invalidate. The change is inside the `/fp:strict` CRC'd tick; client and
server land together. No wire or layout change, so `PlayersPostRender::kiVersion`
(`Players.h:193`, currently 19) is **not** the lever — see "Superseded claims".

## Acceptance criteria

- `kUpdatePlayer` and `kUpdateFleet` are consumed in the Update phase; `Players.cpp`'s
  Spawn-phase handler contains only spawn and destroy arms.
- A `kUpdatePlayer` weapon toggle injected while a player is mid-crossing is observable in
  the destination cell after the crossing completes. Because injection cannot be
  tick-scheduled, drive this by injecting on consecutive ticks across a crossing and
  asserting the toggle is never lost, rather than by targeting the exact transfer tick.
- Client and server `sharedCrc` match across the transfer tick in a replay-determinism run.
- Countdown behavior is unchanged on non-transfer ticks: a weapon toggle still takes effect
  `kiTickRate` ticks after issue.

## Superseded claims — do not reuse

The original plan asserted these; each is false or unusable and was verified as such:

- **"Preserving the uuid closes the window."** False. Both issuers push the StatusChange
  into `mFrameInputs[sourceCoord]` and re-resolve the uuid from the *source* cell's frame
  (`Network/Server/ServerBroadcaster.cpp:211-238`;
  `Network/Server/FleetNavigationController.cpp:149-166`). The destination cell never
  receives the change, so the uuid it would look up is irrelevant. Uuid preservation
  would additionally require routing work, and would not substitute for it.
- **"The collections hub AGENTS.md documents an `AddIndexableElementWithId` transfer/reconnect
  convention."** Fabricated. No `AGENTS.md` in the repository mentions `AddIndexableElementWithId`
  or `WithId`. The original acceptance criterion "the hub-convention grep now includes Players"
  therefore tested nothing.
- **Wire budget.** Had the uuid been added, `kTransferPlayer` is already 116 bytes against
  `kiMaxStatusChangeBytesPerItem = 120` (`Engine/Source/Network/NetworkSerialization.h:17`),
  so an `int64_t` overflows the per-item cap and the ASSERT in `SerializeGroup`
  (`Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp:239`).
- **Cross-cell uuid uniqueness is not guaranteed.** uuid is `(uiFrameId << 48) | counter`
  (`FrameBase.h:183-189`) with `uiFrameId` minted from an unguarded per-process `uint16_t`
  (`Engine/Source/GameBase.h:161`); the client also mints frame ids locally in
  `Game::CreateFrameAtCoord` (`Game.cpp:355,741`). Any future design that makes uuid
  identity load-bearing across cells must establish this invariant first —
  `idToIndexMap.insert_or_assign` (`Collection.h:502`) would silently repoint a live row
  on collision.
- **Wrong version lever.** No collection layout changes, so `PlayersPostRender::kiVersion`
  (`Players.h:193`) is not the lever. Per `Frame.cpp:33-36`, a change that shifts computed
  frame CRCs without a collection layout change bumps the `120 +` base at `Frame.cpp:36`.
- **Untestable acceptance criterion.** "Issued on the same tick a player transfers" is not
  schedulable: `BuildInjectedChange` (`AgentCommandsServer.cpp:398`) has no tick
  parameter and requires `IsCoordActive(coord)`.
- **`TransferRequest::iEntityId`** (`Frame/Frame.h:87`) is set from the source uuid at
  `PlayersNavigation.cpp:108` and never read repo-wide — `CollectTransfers` copies only
  `rRequest.data` (`Network/Server/ServerTransferManager.cpp:96`). It is dead either way;
  consider removing it. (The plan that owned dead-machinery removal,
  `Network/DeadMachinerySweep.md`, no longer exists.)

## Coordination

- Frame version/save/replay batch: this plan shifts CRC'd tick state, so co-landing with the
  other batch members keeps the save/replay invalidation to one window behind a single bump of
  the `120 +` base at `Frame.cpp:36`. There is no last-lander bump to
  wait for — the plan that anchored this batch has landed and consumed its own collection
  bumps (`MissilesPostRender` 7→8, `PlayersPostRender` 18→19, `SpaceshipsInterpolate` 1→2) —
  so a member landing alone owns its own increment of that base literal. Nondirectional
  batching, not a dependency: it does not block landing this plan alone.
- The batch covers `Documents/Plans/Frame/TransferArrivalGracePeriodDiscarded.md`,
  `Documents/Plans/Frame/MissileTransferSpawnAttributes.md`, and
  `Documents/Plans/Frame/MissileVelocityWInvariantBreach.md`, which shift CRC'd tick state on
  the same transfer seam — co-land to consolidate the invalidation into one bump.
- `Documents/Plans/Frame/PromotedFlagshipNavStall.md` joins this batch only if it resolves to its
  frame-local Option A (CRC'd nav-flag transition); its server-side Option B carries no bump.
  Check that plan's recorded option before assuming a co-land. Option A also edits
  `PlayersNavigation.cpp`, which this plan cites for the Transfer-phase row removal — refresh
  those citations if it lands first.
- **Shared per-item wire budget.** `engine::kiMaxStatusChangeBytesPerItem = 120`
  (`Engine/Source/Network/NetworkSerialization.h`) caps every serialized StatusChange item,
  enforced by the per-item `ASSERT` in `SerializeGroup`. `kTransferPlayer` is **116 B today**,
  so only 4 B of headroom remain. The recommended design adds no wire bytes and needs none of
  it, but any re-write that revives a `kTransferPlayer` payload change must re-measure rather
  than trust the 116 B figure: `Documents/Plans/Frame/TransferArrivalGracePeriodDiscarded.md`
  Option B removes 4 B from that item, and
  `Documents/Plans/Network/StatusChangeWireVersionGate.md` governs the version gate any such
  wire-layout change must honour.

## Notes

- The fix is symmetric by construction — both builds run the same phase code — which is the
  main reason it was preferred over patching the outgoing transfer request on the server.
- The `TransferData`/codec co-schedule on this surface is discharged: that work landed, adding
  `float fDeltaRotation` to `TransferData` and its `SharedMembers()` tie, and widening the
  `StatusChangeItemWireSize` spaceship arm to 68 B and the missile arm to 80 B.
  `kTransferPlayer` is unchanged at 116 B under the 120 B ceiling, so the budget arithmetic
  above holds — but re-resolve any `StatusChange.h` / `NetworkSerialization.cpp` citation
  against the landed tree before reusing it, since the struct and both codec arms moved.
- The repository documentation hygiene pass (`/update-claude-docs`) inspects the affected
  AGENTS.md scope; `Documents/Architecture/FrameUpdatePipeline.md` is updated only if it
  depicts status-change consumption per phase.
- Line citations were re-verified on 2026-07-24 against the working tree
  (`Players.cpp`, `Players.h`, `PlayersNavigation.cpp`, `Frame.cpp`, `FrameTick.cpp`,
  `FrameBase.cpp`, `FrameBase.h`, `GameBase.h`, `Collection.h`, `Game.cpp`,
  `ServerBroadcaster.cpp`, `FleetNavigationController.cpp`, `ServerTransferManager.cpp`,
  `NetworkSerialization.h/.cpp`, `AgentCommandsServer.cpp`, `Frame/Frame.h` all resolve
  as cited).
