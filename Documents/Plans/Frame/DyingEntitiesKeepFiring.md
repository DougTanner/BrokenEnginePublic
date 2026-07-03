# Dying Entities Keep Firing Blasters

## Context

The 2026-07-03 Frame review sweep confirmed the same missing gate on both sides of a mirrored pattern — exploding entities continue firing blasters through their death animation:

- **Players.** `PlayersPostRender::Update` runs the full AI block for exploding players (only the transfer lock skips it, `Players.cpp:772-793`), so `AcquireTarget` still sets `kFireBlaster` on a dying ship. `SpawnMissiles` gates on exploding (`PlayersCombat.cpp:401` — `pfNextSecondarySpawnTimes[i] >= 0.0f || (pFlags[i] & kExploding)`), but `SpawnBlasters` (`PlayersCombat.cpp:308-377`) has no `kExploding` check. A player at armor ≤ 0 with an in-range visible spaceship fires blasters (with muzzle audio) from its frozen corpse for the full 0.7 s `kfDestroyTime`. The asymmetry against the missile gate indicates the check was intended.
- **Spaceships.** In `SpaceshipsPostRender::Spawn`, the exploding branch (`Spaceships.cpp:493`) only `continue`s when it actually spawns a stagger explosion (`kExploding && pfDestroyedExplosionTimes[i] <= 0.0f`); an exploding ship whose explosion timer is still counting falls through to the blaster-firing block (`:503-547`). Since `pfNextBlasterSpawnTimes` decays unconditionally (`:672`), a dying ship that hasn't fired for >1 s and faces a visible player shoots mid-death (`kfSpaceshipDestroyTime` 0.25 s).

Deterministic on both sides — behavioral bugs, not desyncs.

## Design

- Add the `kExploding` gate to `PlayersPostRender::SpawnBlasters`, mirroring the existing `SpawnMissiles` gate shape at `PlayersCombat.cpp:401`.
- In `SpaceshipsPostRender::Spawn`, gate the blaster-firing block on `!(pFlags[i] & kExploding)` (or restructure the exploding branch to `continue` for any exploding ship, spawning the stagger explosion when its timer allows) — pick whichever reads cleaner against the surrounding branch at `Spaceships.cpp:493`.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersCombat.cpp` — the `SpawnBlasters` member of `PlayersPostRender`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp` — the `Spawn` member of `SpaceshipsPostRender`

## Out of scope

- Whether exploding entities should still be *targetable* or take damage — existing behavior, unchanged.
- Missile firing paths — already gated.
- The Spaceships prev-vs-current player-scan inconsistency in the same functions — owned by `Frame/FrameTickMinorHardening.md`.

## Notes

- **Invariant exposure.** Both edits are inside the `/fp:strict` CRC'd tick and change CRC'd sim behavior (fewer blasters spawned, RNG draws at the removed spawn sites no longer occur) — replay-visible. No layout change, so no `kiVersion` bump strictly required; straddling replays simply diverge as with any behavior fix (bump only if a batch-partner plan bumps anyway). No wire change. Client and server land together.
- No open decisions — mechanical once the Spaceships branch shape is picked (trivial-choice rule applies).
