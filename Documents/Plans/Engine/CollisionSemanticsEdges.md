# Collision Semantics Edges (Multi-Hit Destroy + Sweep Segment Mismatch)

## Context

Two engine collision-phase semantic edges, surfaced via Blasters by the 2026-07-03 Frame review sweep. Both are deterministic on both sides — gameplay-correctness questions, not desyncs.

**(a) `kDestroyOnCollide` entity can damage multiple targets in one tick.** The per-`i` collide loop checks layer A's `kAlreadyCollided` only at the top of the iteration (`Collision.cpp:529`), and `TestAndRecordPair` (`Collision.cpp:448`) checks only B's flag; `RecordCollision` (`Collision.cpp:431-434`) sets A's flag after the first hit, but the remaining B candidates in the same `i` iteration still record. One blaster overlapping two spaceships deals `kfBlasterDamage` to **both** before being destroyed once. Whether simultaneous-contact multi-hit is intended is a design call (grill item).

**(b) Entity sweep and terrain collision test opposite time segments.** The swept entity test is forward-predictive: positions are post-integration and `SweptSphereTest` (`Collision.cpp:171-211`) scales relative velocity by `kfDeltaTime` with t ∈ [0, 1] — testing `[P, P+v·dt]`, next tick's motion. Terrain collision instead back-marches the traversed segment `[P−v·dt, P]` (`BlastersUpdate.cpp:296-317`). Consequences: the blaster's first traversed segment (spawn → first integrated position, ~speed·dt) is never swept against entities, so point-blank targets can be tunneled; and in `PostCollision` (`BlastersUpdate.cpp:279-283`) the entity-hit check runs before the terrain check and `continue`s, so a blaster whose forward segment crosses terrain before reaching a target registers the entity hit one tick early, "through" the mountain.

## Design

- **(a)** If single-hit is intended: early-out the inner B loop on `rLayerA.pFlags[i] & kAlreadyCollided` (or extend `TestAndRecordPair` to check A's flag symmetrically with B's). If multi-hit is intended: document it at `RecordCollision` and close the item.
- **(b)** Align the two tests on the same traversed segment. Simplest coherent shape: make the entity sweep test the traversed segment `[P−v·dt, P]` (previous → current position, matching the terrain march) rather than the predictive one — eliminating both the uncovered first segment and the hit-through-terrain-early edge (terrain check then wins on the same segment where it occurs first). Alternative: keep predictive sweep but add a spawn-tick sweep of the initial segment and reorder terrain-before-entity in `PostCollision`. The engine sweep is shared by all four collision layers — audit Players/Missiles/Spaceships consumers for assumptions about the tested segment before switching (mirrored-pattern check per update-affected-code §3).

## Critical files

- `Engine/Source/Frame/Collision.cpp` — the collide loop, `TestAndRecordPair`, `RecordCollision`, `SweptSphereTest`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/BlastersUpdate.cpp` — `PostCollision` ordering + terrain march
- Consumers to audit, not necessarily edit: `PlayersCombat.cpp`, `MissilesUpdate.cpp`, `SpaceshipsCombat.cpp` collision-result handling

## Out of scope

- Collision matrix / alignment-filter composition (`HealthDamage.h`) — verified correct and symmetric.
- The PreCollision `thread_local` layer-array lifetime pattern — verified sound.
- Broad-phase or performance work.

## Acceptance criteria

- (a) resolved per the grill decision: either one `kDestroyOnCollide` hit per tick maximum (a blaster overlapping two targets damages exactly one), or the multi-hit behavior documented at the recording site.
- (b) the segment every collision test covers is the same traversed interval; a blaster spawned adjacent to a target cannot pass through it untested, and a terrain strike suppresses an entity hit beyond it in the same segment.

## Notes

- **Invariant exposure: high sim sensitivity.** All changes are inside the `/fp:strict` CRC'd tick and change hit outcomes → replay-visible behavior change; no layout/wire/`kiVersion` change. The sweep math must keep the exact deterministic float forms (`XMVector*` function forms, no re-association). Client and server land together.
- **Single open decision for `/external-grill-plan`:** (a)'s intent — single-hit (recommended: the flag name `kDestroyOnCollide` and the one-destroy outcome imply one hit) vs multi-hit-by-design. (b)'s segment choice follows mechanically once (a) is settled; prefer the traversed-segment alignment unless the consumer audit surfaces a predictive-sweep dependency.
- Shares `Engine/Source/Frame/Collision.cpp` with `Frame/Refactor_FrameRootQuickWins.md` (collision pair-context struct) — co-schedule or refresh citations; land its mechanical items first if interleaved.
