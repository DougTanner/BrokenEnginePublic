<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Collision Zone Pre-Allocation Overflows at High Entity Counts

## Context

Observed repeatedly during a 2026-07-21 harness run at high entity counts:

```
Collision: ZonePair.indicesB overflow (count: 256, capacity: 256) ... Increase kiCollisionZonePreallocate in Collision.h
```

`engine::kiCollisionZonePreallocate = 256` (`Engine/Source/Frame/Collision.h:11`) sizes `ZonePair::indicesA`/`indicesB` at `Collision.cpp:88-89`. When a single zone's occupancy exceeds it, `Collision::InsertObjectIntoZones` (`Collision.cpp:191-201`) logs the `kWarning` above, hits `DEBUG_BREAK()`, then grows the vector under `ScopedSuppressAllocationTracking` (`Collision.cpp:198-200`).

The growth path is correct — nothing is dropped or corrupted — but each overflow costs a `kWarning` per occurrence, a `DEBUG_BREAK()` that halts a Debug session, and a heap allocation inside the tick, which is exactly what the allocation tracker exists to keep out of the main loop. The message itself names the remedy, so the constant is understood to be a tuning value that has fallen behind current entity densities. With an 8x8 zone grid (`kiCollisionZonesX`/`Y`, `Collision.h:9-10`), 256 indices per zone per layer pair is reached by ordinary play at the densities the harness now drives.

Pre-existing and unrelated to any in-flight change.

## Design

- Determine the observed peak per-zone occupancy for the worst layer pair at the entity counts the harness reproduces, rather than guessing a multiplier.
- Raise `kiCollisionZonePreallocate` to cover that peak with headroom, and record the per-frame memory cost of the new value (`zones` count x layer pairs x 2 vectors x value x 8 B) in the change so the trade-off is explicit.
- Leave the overflow growth path in place: it stays the correct safety net, and its `kWarning` + `DEBUG_BREAK()` remain the signal that the constant has fallen behind again. Only reconsider the `DEBUG_BREAK()` if the new value still overflows in normal play, in which case that is the real finding.
- Re-check the sibling pre-allocation constants (`kiCollisionCandidatePreallocate`, `kiCollisionResultPreallocate`, `kiCollisionResultSpanPreallocate`, `Collision.h:12-16`) for the same symptom in the same run before landing; raise only those that actually overflow.

## Critical files

- `Engine/Source/Frame/Collision.h` — `kiCollisionZonePreallocate` (`:11`) and sibling constants (`:9-16`).
- `Engine/Source/Frame/Collision.cpp` — zone sizing (`:88-89`), overflow log/break/grow (`:191-201`).

## Out of scope

- Restructuring the zone grid, changing `kiCollisionZonesX`/`Y`, or altering broad-phase strategy.
- Removing or downgrading the overflow warning as a way of silencing the symptom.
- Raising constants that did not overflow in the reproduction.
- Collision correctness, layer pairing, or swept-pair behavior.

## Acceptance criteria

- A harness run at the entity counts that previously produced the overflow logs completes with no `ZonePair.indices*` overflow warning and no `DEBUG_BREAK()` from `InsertObjectIntoZones`.
- The measured peak occupancy and the resulting memory cost are recorded in the change.
- No new allocation-tracker violations appear in the same run.

## Notes

- Engine-side, shared by client and server; both must be rebuilt and run.
- The constant feeds no CRC, wire, save, or `.pack` data — it only sizes scratch vectors — so no `kiVersion` or protocol exposure. Behavior is identical either side of the change; only the allocation timing differs.
- Live verification via the agent harness at high entity counts is the acceptance signal.
