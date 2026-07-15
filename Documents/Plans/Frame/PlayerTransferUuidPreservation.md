# Player Transfer Uuid Preservation

## Context

Verified 2026-07-03: the player cell-transfer arrival path re-adds players via plain `engine::AddIndexableElement` (`Players.cpp:425`), minting a **new** collection uuid — `TransferData` (`StatusChange.h:77-165`) carries `globalPlayerId` but no collection uuid, so preservation is structurally impossible today. This contradicts the engine collections hub AGENTS.md convention ("Transfer/reconnect re-adds use `AddIndexableElementWithId` so the server-issued ID survives" — whose only actual user is `SmokeTrails.cpp:97`; `AddIndexableElementWithId` exists at `Collection.h:566`).

Consequence — a one-tick lost-update window: `kUpdatePlayer`/`kUpdateFleet` StatusChanges target `iPlayerUuid` through `idToIndexMap` (`Players.cpp:272,289,316`). Both live issuers re-resolve the uuid fresh each tick by scanning `pGlobalPlayerIds` (`ServerBroadcaster::ProcessUpdatePlayerRequests`, `ServerBroadcaster.cpp:216-233`; `FleetNavigationController::ProcessFlagshipUpdates`, `FleetNavigationController.cpp:158-176`), so the window is exactly the transfer tick: uuid resolved pre-tick, StatusChange consumed in that tick's Spawn phase, which runs **after** Transfer/Destroy removed the row. Dropped requests are not retried (`mPendingUpdatePlayerRequests`/`mPendingFlagshipUpdates` cleared after processing). Practical impact: a `kUpdateFleet` issued on a member's crossing tick is lost for that member until the next flagship direction change (up to tens of seconds of a wingman navigating to the wrong cell); a `kUpdatePlayer` weapon-mode toggle is silently dropped. No desync — both sides drop identically. (`kDestroyPlayer` would be the severe case but is currently issuer-less dead machinery — see `Network/DeadMachinerySweep.md`.)

## Design

Carry the uuid through transfer and re-add with it:

1. Add the collection uuid to the player arm of `TransferData` (`StatusChange.h`) and its wire codec in `NetworkSerialization.cpp`; populate it in the player `TransferRequest` build (`PlayersNavigation.cpp:88` region).
2. On arrival, `PlayersPostRender::Spawn` re-adds via `engine::AddIndexableElementWithId` with the carried uuid instead of `AddIndexableElement` (`Players.cpp:425`) — aligning Players with the documented hub convention.
3. Both sides mint transfers deterministically today, so the carried uuid is identical client/server; in-flight `kUpdatePlayer`/`kUpdateFleet` StatusChanges targeting the pre-transfer uuid then resolve in the destination cell instead of dropping.
4. Update the hub AGENTS.md if wording needs the Players example, and remove the `kWarning` drop-log path's "expected during transfer" caveat if one gets added meanwhile.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h` — `TransferData` player uuid field
- `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` — player `TransferData` codec arms
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp` — `TransferRequest` build
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp` — arrival `Spawn` re-add
- `Projects/BrokenEngineSandbox/Source/SpawnTransfer.cpp` — arrival dispatch pass-through

## Out of scope

- Retry/queueing semantics in `ServerBroadcaster`/`FleetNavigationController` — with the uuid preserved, the window closes without them.
- Other collections' transfer uuid handling (blasters/missiles/spaceships are not targeted by uuid-addressed StatusChanges; leave as-is).
- The dead `kDestroyPlayer`/`kRespawnPlayer` machinery — `Network/DeadMachinerySweep.md`.
- The "0 = unset" sentinel fixes in the same functions — `Frame/TransferSentinelConflation.md`.

## Acceptance criteria

- A `kUpdatePlayer` weapon toggle and a `kUpdateFleet` update issued on the same tick a player transfers are applied in the destination cell (uuid lookup succeeds), on both client and server.
- `Players.cpp` contains no plain `AddIndexableElement` on the transfer path; the hub-convention grep (`AddIndexableElementWithId`) now includes Players.

## Coordination

- Frame version/save/replay batch with `Documents/Plans/Frame/TransferSentinelConflation.md`, `Documents/Plans/Frame/MissileLifetimeAndTargetLifecycle.md`, `Documents/Plans/Frame/BlasterWindTrailTransferParams.md`, `Documents/Plans/Frame/FireCooldownNegativeFloor.md`: co-land behind one consolidated `Frame::kiVersion` change and one save/replay invalidation; the last lander owns the bump.

## Notes

- **Invariant exposure: high.** `TransferData` layout change → StatusChange wire/save payload change → bump `PlayersPostRender::kiVersion` (propagates to `Frame::kiVersion`, invalidates saves/replays). Uuid allocation order feeds `idToIndexMap` iteration-independent lookups only, but the uuid counter itself is CRC-relevant shared state — re-adding with a carried id must keep the mint counter behavior identical on both sides (verify the `AddIndexableElementWithId` counter contract against SmokeTrails' usage). Inside the `/fp:strict` CRC'd tick; client and server land together.
- **Single open decision for `/external-grill-plan`:** fix (recommended — carries one `uint64_t`, closes the window, restores the documented convention) vs accept-and-document (add the caveat to the hub AGENTS.md and downgrade the `kWarning`; zero code risk, keeps the lost-update behavior).
- Co-schedule with `Frame/TransferSentinelConflation.md` + `Frame/MissileLifetimeAndTargetLifecycle.md` — shared `TransferData`/codec surface, one shared version bump (see Order.md Dependencies).
