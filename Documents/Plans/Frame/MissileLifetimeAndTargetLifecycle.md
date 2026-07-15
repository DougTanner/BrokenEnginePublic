# Missile Lifetime Cap + Target Lifecycle Hardening

## Context

The 2026-07-03 Frame review sweep confirmed missiles have **no lifetime bound at all**: the `pfTimes` member of `MissilesPostRender` is accumulated every tick (`MissilesUpdate.cpp:96`, stored `:219`), CRC'd (`SharedMembers()`, `Missiles.h:133`), diff-logged, and carried through `TransferData` (`Missiles.cpp:383-384`) — but repo-wide grep shows **zero consumers**. Same for `pfExaustDelays`. A targetless missile over open ocean (position Z held above sea level, terrain hit impossible since ocean elevation < 0, velocity Z zeroed at `MissilesUpdate.cpp:208`) flies straight forever, transferring cell-to-cell across the unbounded grid as a permanent entity. Deterministic on both sides — an entity leak plus dead CRC'd state, not a desync.

Three adjacent Target-lifecycle contracts from the same sweep fold in here (same files, same version bump):

- Exploding missiles hold their target subscription for the full death animation: `Explode` (`Missiles.cpp:485-535`) sets `kExploding` but releases `puiTargets[i]` only in `Destroy` → `RemoveOwnedObjects` (`Missiles.cpp:342-353`) ~11 ticks later. `Frame::GetMissileTarget`'s under-subscription preference (`Frame.cpp:471-477`) counts these dead subscribers, diverting fresh missiles away from targets whose attackers are already dead.
- `TargetsPostRender::Remove` computes `IdToIndex(rId)` before branching (`TargetsUpdate.cpp:60`), so an invalid id throws `std::out_of_range` from the map `.at()`; the `BeginExplosion` caller at `SpaceshipsCombat.cpp:52` passes unguarded (unlike the mirrored guard at `Spaceships.cpp:387` and the missile-side checks at `Missiles.cpp:345`). Currently unreachable-in-anger; fragile contract.
- Missile target validation checks the **previous** frame's `idToIndexMap` (`MissilesUpdate.cpp:144`) but the guarded `TargetsPostRender::Remove` at `:156` indexes the **current** map — a latent mid-tick throw if any future path removes a target row before Missiles Update.

## Design

1. **Lifetime (single open decision — see Notes).** Recommended: implement expiry — a `kfMissileLifetime` constant in `HealthDamage.h`; in `MissilesPostRender::Update`, when the accumulated `pfTimes` exceeds it, route through the existing `Explode` path (self-destruct, no area damage or reuse `Explode` as-is — implementer picks the simpler). This gives `pfTimes` its consumer and bounds the entity population. Alternative: delete `pfTimes` + `pfExaustDelays` outright (SOA members, `SharedMembers`/`Members` walks, `LogDifferences`, `TransferData` fields + wire codec, `SpawnInfo`) — smaller state but leaves missiles unbounded.
2. **Release subscription at explode.** Move the `puiTargets` unsubscribe (the `TargetsPostRender::Remove(..., {kMissile})` release currently in `RemoveOwnedObjects`) into `Explode`, nulling the SOA slot; make `RemoveOwnedObjects` tolerate the already-released slot (it already guards on the map, `Missiles.cpp:345`).
3. **`TargetsPostRender::Remove` contract.** Move the `IdToIndex` lookup after (or guard it with) a `contains` check so an invalid id is a no-op or ASSERT-documented contract rather than an uncaught throw; align the `SpaceshipsCombat.cpp:52` caller with the guarded siblings. Note the `kDestination` path re-resolves via `RemoveIndexableElement` anyway — the early lookup is also wasted work on that path.
4. **Prev-vs-current map guard.** Change `MissilesUpdate.cpp:144` to validate against the current frame's `pTargets->idToIndexMap` (matching `RemoveOwnedObjects`), so validation and mutation agree.
5. **`SharedMembers()` pre-emption.** Add `SharedMembers() == Members()` to `TargetsInterpolate` and `TargetsPostRender` (`Targets.h:53,84`) per the engine collections hub recommendation, so a future client-only member cannot silently enter the CRC/wire path.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/MissilesUpdate.cpp` — `MissilesPostRender::Update` (expiry, map guard)
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp` — `Explode`, `RemoveOwnedObjects`, `Spawn`, `TransferRequest` build
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.h` — `kiVersion` bump, member walks
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/TargetsUpdate.cpp` — `TargetsPostRender::Remove`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/Targets.h` — `SharedMembers()` pre-emption, `kiVersion` bump
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/SpaceshipsCombat.cpp` — `BeginExplosion` caller guard
- `Projects/BrokenEngineSandbox/Source/Frame/HealthDamage.h` — `kfMissileLifetime` (expiry option)

## Out of scope

- Where transfer requests into never-simulated cells go (dropped vs frame-instantiating) — Network/Game territory; the lifetime cap bounds the leak regardless.
- `GetMissileTarget`'s selection heuristics themselves (under-subscription preference, tie-breaking) — verified deterministic, unchanged.
- The transfer "0 = unset" sentinel conflation on `fDeltaRotationDelay`/`fExhaustDelay` — owned by `Frame/TransferSentinelConflation.md`.
- Spaceship/player target usage beyond the single unguarded `BeginExplosion` call.

## Acceptance criteria

- A missile that never hits anything is destroyed after the lifetime cap (or, under the delete option, `pfTimes`/`pfExaustDelays` no longer exist anywhere: SOA, CRC walks, wire, `LogDifferences`).
- An exploding missile's target row shows the decremented subscriber count on the explode tick, not the destroy tick.
- No call path can reach the `idToIndexMap.at()` inside `TargetsPostRender::Remove` with an id absent from the current frame's map.

## Coordination

- Frame version/save/replay batch with `Documents/Plans/Frame/TransferSentinelConflation.md`, `Documents/Plans/Frame/PlayerTransferUuidPreservation.md`, `Documents/Plans/Frame/BlasterWindTrailTransferParams.md`, `Documents/Plans/Frame/FireCooldownNegativeFloor.md`: co-land behind one consolidated `Frame::kiVersion` change and one save/replay invalidation; the last lander owns the bump.

## Notes

- **Invariant exposure: high.** Everything here is inside the `/fp:strict` CRC'd tick. Item 1 (either option) and item 2 change CRC'd behavior/state → bump `MissilesPostRender::kiVersion` (+ `Targets*::kiVersion` if walks change), which propagates into `Frame::kiVersion` and invalidates saves/replays. The delete option also changes the `TransferData` missile payload wire layout (`NetworkSerialization.cpp`). Item 2 changes subscriber counts, which are CRC'd shared state — client and server must land together (they always do; single codebase).
- **Single open decision for `/external-grill-plan`:** expiry (recommended — bounds the leak, gives the fields a consumer, keeps wire layout) vs delete-the-dead-fields (smaller state, leaves missiles immortal). If expiry: does end-of-life use the full `Explode` (area damage + visuals) or a silent destroy?
- Co-schedule with `Frame/TransferSentinelConflation.md` and `Frame/PlayerTransferUuidPreservation.md` — shared `TransferData`/codec surface and one shared version bump (see Order.md Dependencies).
