<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Collision Zone Pre-Allocation Overflows at High Entity Counts

## Context

Observed repeatedly during a 2026-07-21 harness run at high entity counts:

```
Collision: ZonePair.indicesB overflow (count: 256, capacity: 256) ... Increase kiCollisionZonePreallocate in Collision.h
```

`engine::kiCollisionZonePreallocate = 256` (`Engine/Source/Frame/Collision.h:11`) sizes `ZonePair::indicesA`/`indicesB` in the `LayerPairZones` constructor (`Engine/Source/Frame/Collision.cpp:88-89`). When a single zone's occupancy exceeds it, `Collision::InsertObjectIntoZones` (`Collision.cpp:191-201`) logs the `kWarning` above, hits `DEBUG_BREAK()`, then doubles the vector under `ScopedSuppressAllocationTracking` (`Collision.cpp:198-200`).

The growth path is correct — nothing is dropped or corrupted — but each overflow costs a `kWarning` per occurrence, a `DEBUG_BREAK()` that halts a Debug session, and a heap allocation inside the tick, which is exactly what the allocation tracker exists to keep out of the main loop. The message itself names the remedy, so the constant is understood to be a tuning value that has fallen behind current entity densities. With an 8x8 zone grid (`kiCollisionZonesX`/`kiCollisionZonesY`, `Collision.h:9-10`), 256 indices per zone per layer pair is reached by ordinary play at the densities the harness now drives.

Pre-existing and unrelated to any in-flight change.

## Design

This is a measured constant retune, not a code change to the collision algorithm:

1. **Measure, don't guess.** Reproduce the overflow with an agent-harness run at the entity counts from the 2026-07-21 report. Derive the observed peak per-zone occupancy for the worst layer pair from the overflow warning lines themselves: each warning logs the count/capacity at the moment of overflow, and the doubling growth means the largest capacity that stops overflowing bounds the peak. The largest `count` value logged across the run is the measured peak.
2. **Raise the constant.** Set `kiCollisionZonePreallocate` to the smallest power of two greater than or equal to 2x the measured peak. Record in the change description both the measured peak and the resulting per-frame memory cost: `kiCollisionZonesX * kiCollisionZonesY` zones x layer pairs x 2 vectors x new value x 8 B per `int64_t`, per thread — so the trade-off is explicit.
3. **Keep the safety net.** Leave the overflow log/`DEBUG_BREAK()`/grow path in `InsertObjectIntoZones` untouched: it stays the correct fallback, and its `kWarning` + `DEBUG_BREAK()` remain the signal that the constant has fallen behind again. If the new value still overflows in normal play, stop and report that as the real finding — do not remove or downgrade the break in this plan.
4. **Check siblings in the same run.** Inspect the same run's logs for the analogous overflow warnings tied to `kiCollisionCandidatePreallocate`, `kiCollisionResultPreallocate`, and `kiCollisionResultSpanPreallocate` (`Collision.h:14-16`). Raise a sibling by the same rule (smallest power of two >= 2x its observed peak) only if it actually overflowed; otherwise leave it at its current value.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the acceptance criteria and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities the named change requires.

### In scope

- `Engine/Source/Frame/Collision.h` — the numeric value of `kiCollisionZonePreallocate` (line 11); the numeric values of `kiCollisionCandidatePreallocate`, `kiCollisionResultPreallocate`, and `kiCollisionResultSpanPreallocate` (lines 14-16) only for those proven to overflow in the reproduction run. No other edits to this header.
- Harness runs and log inspection to measure peaks and verify the fix; both client and server rebuilt for verification.

### Out of scope

- Any edit to `Engine/Source/Frame/Collision.cpp` — the `LayerPairZones` constructor sizing (`:88-89`) and the `InsertObjectIntoZones` overflow path (`:191-201`) are read-only references, not change sites.
- Restructuring the zone grid, changing `kiCollisionZonesX`/`kiCollisionZonesY`, or altering broad-phase strategy.
- Removing or downgrading the overflow warning or `DEBUG_BREAK()` as a way of silencing the symptom.
- Raising constants that did not overflow in the reproduction, including `kiCollisionLayerPreallocate` and `kiCollisionLayerPairPreallocate` (`Collision.h:12-13`).
- Collision correctness, layer pairing, or swept-pair behavior.

## Critical files

- `Engine/Source/Frame/Collision.h` — the only file edited.
- `Engine/Source/Frame/Collision.cpp` — read-only: zone sizing and overflow path referenced above.

## Risk tier

Tier 2 — scoped runtime behavior of one engine subsystem, verified by an observable harness scenario. No Tier-3 trigger: the constants size thread-local scratch vectors only and feed no CRC, wire/protocol, serialization, save/replay, or `.pack` data, so there is no `kiVersion` or protocol exposure. Invariant to preserve: simulation behavior is identical either side of the change; only allocation timing differs.

## Acceptance criteria

- An agent-harness run at the entity counts that previously produced the overflow logs completes with no `ZonePair.indices*` overflow warning and no `DEBUG_BREAK()` from `InsertObjectIntoZones`.
- The measured peak occupancy and the resulting per-frame memory cost (formula in Design step 2) are recorded in the change description.
- No new allocation-tracker violations appear in the same run.
- Any sibling constant raised is backed by an overflow warning for it in the reproduction run, recorded alongside.

## Notes

- Engine-side, shared by client and server; both must be rebuilt and run.
- Live verification via the agent harness at high entity counts is the acceptance signal; a diff alone is not decisive because the target is a runtime symptom.
