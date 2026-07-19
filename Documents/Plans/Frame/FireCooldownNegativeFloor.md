# Fire-Cooldown Timers Drift Unbounded-Negative

## Context

The three player cooldowns `pfShieldCooldowns`/`pfDestroyedExplosionTimes`/`pfShieldDownSoundCooldowns` are floored at `std::max(0.0f, …)` in `PlayersPostRender::Update` (`Players.cpp:728-730`) because they are 'ready when `<= 0`'. Two more per-tick timers of the same unbounded-negative-drift class need a *different* fix — they fire only on strictly `< 0.0f`, so a `0.0f` floor would suppress all firing.

Both fields decrement per tick and reset to a positive cooldown only when the entity actually fires, so an entity that cannot fire (no target) drifts the timer unbounded-negative with no floor:

- **Spaceship blaster cooldown** (`SpaceshipsPostRender`). `pfNextBlasterSpawnTimes` is decremented every tick into a local — `SpaceshipsPostRender::Update`, `Spaceships.cpp:679`: `float fNextBlasterSpawnTime = rPrevious.pfNextBlasterSpawnTimes[i] - fDeltaTime;` — and stored back at `Spaceships.cpp:719`. It is reset to `kfBlastersSpawnCooldown` only when the ship fires, in `SpaceshipsPostRender::Spawn` (`Spaceships.cpp:527`), which fires on the strict check `pfNextBlasterSpawnTimes[i] < 0.0f && bSpawnBlaster` (`Spaceships.cpp:525`). A spaceship with no visible/facing alive player never fires, so its timer drifts unbounded-negative — the `NearestAlivePlayerPosition`/`IsVisible` checks `continue` before the reset, while a non-facing ship instead reaches the `:525` fire check with `bSpawnBlaster` false (set from the facing-cone test at `:523`), so the reset is skipped. Carried in `SharedMembers()` (`Spaceships.h:145`) → **CRC'd** via the auto-dispatch `SharedCollectionCrc`.
- **Player secondary/missile cooldown** (`PlayersPostRender`). `pfNextSecondarySpawnTimes` is decremented in place unconditionally — `PlayersPostRender::SpawnMissiles`, `PlayersCombat.cpp:388`: `rCurrentPostRender.pfNextSecondarySpawnTimes[i] -= fDeltaTime;` — and reset to `kfMissileSpawnInterval` only past the fire gate (`PlayersCombat.cpp:406`), which requires the value to be strictly negative (`>= 0.0f` → `continue` at `PlayersCombat.cpp:401`). A player not firing missiles — the common case (`kFireMissile`/`kUseMissiles` gates at `:390-399` `continue` first) — drifts this unbounded-negative. Carried in `SharedCrcMembers()` (`Players.h:298`) → **CRC'd**. (`PlayersPostRender::Update` only load/stores the field forward unchanged at `Players.cpp:723`/`:810`; the sole mutation is the `SpawnMissiles` decrement.)

**Severity: LATENT-only, same as the three player cooldowns already floored at `0.0f`.** The fire checks treat *any* negative value as "ready", so the unbounded drift changes no gameplay today — an entity that has drifted very negative fires on exactly the same tick it would with a floored value. The hazard is real but future-facing: each is an unbounded-negative-drift that becomes a live bug the moment any future code *adds to* rather than *sets* the field, and (secondarily) removes the theoretical float-magnitude growth over extreme durations. This is the acceptance gap the `0.0f` floor on the three player cooldowns closed for those fields but left open for these two.

## Design

Floor each timer at a small **negative** sentinel that is strictly below the `< 0.0f` fire threshold, so a floored timer still satisfies "ready to fire". Gameplay is invariant to the exact floor value: any value `< 0.0f` passes the fire check identically, so a firing frame still resets to the positive cooldown exactly as today — the only observable change is the stored float magnitude (and therefore the frame CRC).

- **Spaceships** — floor the decrement at `Spaceships.cpp:679` (before the store at `:719` and before `Spaceships.cpp:525` reads it this tick): `fNextBlasterSpawnTime = std::max(<negative sentinel>, rPrevious.pfNextBlasterSpawnTimes[i] - fDeltaTime);`.
- **Players** — floor the in-place decrement at `PlayersCombat.cpp:388`: `rCurrentPostRender.pfNextSecondarySpawnTimes[i] = std::max(<negative sentinel>, rCurrentPostRender.pfNextSecondarySpawnTimes[i] - fDeltaTime);`.

The floor is **not** the mechanical `std::max(0.0f, …)` used for the three already-floored player cooldowns — `0.0f` would break firing here. This is a deliberate design choice, distinct from that `0.0f` transform.

**Single open decision for `/external-grill-plan`:** the sentinel value. Because any negative value is gameplay-equivalent, this is a robustness/readability choice, not a behavior choice. Recommended: a small shared named constant (e.g. `-1.0f`) per collection alongside the existing cooldown constants — clearer than a bare literal and self-documenting as "floored, still fires". Alternative: reuse the negated cooldown magnitude (`-kfBlastersSpawnCooldown` / `-kfMissileSpawnInterval`) so the floor sits exactly one cooldown period below zero. The plan must NOT pick `0.0f` or any non-negative floor.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp` — the `Update` member of `SpaceshipsPostRender` (decrement of `pfNextBlasterSpawnTimes`, `:679`; store `:719`); fire site is the `Spawn` member (`:525`/`:527`, read-only for this change)
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersCombat.cpp` — the `SpawnMissiles` member of `PlayersPostRender` (in-place decrement of `pfNextSecondarySpawnTimes`, `:388`; fire gate `:401`/reset `:406`, read-only for this change)
- If the sentinel is introduced as a named constant, it lives in the same TU beside the existing cooldown constants — `kfBlastersSpawnCooldown` (`Spaceships.cpp:69`) and `kfMissileSpawnInterval` (`PlayersCombat.cpp:38`) — so no header edit is needed
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` — `Frame::kiVersion` base bump (`:11-14`)

## Out of scope

- The **exploding-entity fire gates** in these same `Spawn`/`SpawnMissiles`/`SpawnBlasters` functions — already present in current code (the blaster/missile spawn paths `continue` on `kExploding`); a "should a dying entity fire" behavior concern, orthogonal to the cooldown-drift floor.
- `Missiles::fDeltaRotationDelay` (`MissilesUpdate.cpp:178,192`) — its "0 = unset" transfer sentinel is owned by `Frame/TransferSentinelConflation.md`; route any clamp there.
- `Missiles::fExaustDelay` (`MissilesUpdate.cpp:99`) — dead field, deleted by `Frame/MissileLifetimeAndTargetLifecycle.md`; do not clamp.
- `Spaceships.cpp:678` `pfDestroyedExplosionTimes` decrement — adversarial review confirmed it self-resets on cross (harmless, not unbounded); not a floor candidate.
- The three player cooldowns already floored at `0.0f` in `PlayersPostRender::Update` (`Players.cpp:728-730`) — done.
- The two `FrameTick.cpp` `elevationGrid`/`islandRenderQueries` `.empty()` gates, `PlayersCombat.cpp:420` `AcquireTarget` (phase-mandated), and `CollectionController.h:59` `operator==` (no consumer) — non-issues (fixed-size elevation/render-query grids are never legitimately empty, the `AcquireTarget` split is phase-mandated, and the `operator==` has no consumer).
- Any change to firing behavior, RNG draws, cooldown magnitudes, or wire/save layout — this is a floor only; no gameplay, RNG-stream, or layout change.

## Acceptance criteria

- Both timers are floored at a strictly-negative sentinel; a spaceship with no visible/facing player and a player not firing missiles no longer drift their cooldown unbounded-negative.
- Firing is unchanged: a spaceship that becomes able to fire, and a player that presses fire, still trigger on the same tick as before (the floored value is still `< 0.0f`). Verify by inspection that the fire gates (`Spaceships.cpp:525`, `PlayersCombat.cpp:401`) still pass for the floored value.
- Within a single build, client/server CRC parity holds (both run identical floored code; the fields feed the CRC identically on both sides).
- `Frame::kiVersion` base is bumped 118→119 (or the shared batch bump if co-scheduled — see Notes), so a save/replay written by pre-floor code is rejected as version-incompatible (fresh-game fallback) instead of false-desyncing on the shifted timer CRC.

## Coordination

- Frame version/save/replay batch with `Documents/Plans/Frame/TransferSentinelConflation.md`, `Documents/Plans/Frame/PlayerTransferUuidPreservation.md`, `Documents/Plans/Frame/MissileLifetimeAndTargetLifecycle.md`: co-land behind one consolidated `Frame::kiVersion` change and one save/replay invalidation; the last lander owns the bump.
- `Documents/Features/Frame/SweptShipTerrainCollision.md`: reciprocal `Frame.cpp` version-bump and deterministic replay coordination; co-schedule or serialize the changes so one landing owns the reconciled version/replay verification.

## Notes

- **Invariant exposure: CRC / `kiVersion`.** Both fields are CRC'd (Spaceships `pfNextBlasterSpawnTimes` in `SharedMembers()`, auto-dispatched via `SharedCollectionCrc`; Players `pfNextSecondarySpawnTimes` in `SharedCrcMembers()`). The floor changes the stored float magnitude, which shifts the computed frame CRC even though the fire/no-fire outcome is identical — so per the `Frame.cpp:11-13` rule the base `Frame::kiVersion` must bump (118→119 if landing alone) to invalidate straddling saves/replays. The current code already bumped `Frame::kiVersion`'s base to 118 for the analogous cooldown clamps and the exploding-entity fire gate, so this floor's bump is 118→119 if it lands alone. No wire/`.pack` change; no RNG-stream change (no `common::Random` draw added or removed); no layout change; no never-interleave.
- **Co-schedule with the Frame transfer/version-bump batch** (`Frame/TransferSentinelConflation.md`, `Frame/PlayerTransferUuidPreservation.md`, `Frame/MissileLifetimeAndTargetLifecycle.md`) so one `Frame::kiVersion` change (one save/replay invalidation) covers this floor plus their bumps — last-lander owns the consolidated bump. Also shares `Spaceships.cpp`/`PlayersCombat.cpp` with the game Frame sim-TU cluster (`Frame/TransferSentinelConflation.md`, `Frame/FlagshipLossNavFallback.md`) — co-schedule or refresh citations between sessions; all change CRC'd tick behavior, so batching consolidates replay divergence into one landing.
- **No open decisions beyond the sentinel value** (Design, above) — recommend a small shared named negative constant. Mechanical once chosen. Client and server land together.
