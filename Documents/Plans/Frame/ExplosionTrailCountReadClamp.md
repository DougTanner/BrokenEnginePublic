<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-25T20:08:22.007Z","dependsOn":[]} -->
# Clamp Deserialized Explosion Trail Counts

## Context

Found by adversarial review during the completed collection-difference-count safety session and confirmed against the current tree. Pre-existing and independent of that change: it reproduces with equal row counts on both sides, so the common-row bound landed there neither introduces nor mitigates it.

`ExplosionsInterpolate` stores trail state as a fixed array of per-slot row arrays: `float* pfTrailTimes[kiMaxExplosionTrails]` (`Engine/Source/Frame/Collections/Explosions/Explosions.h:153`), with `kiMaxExplosionTrails = 8` (`Explosions.h:23`). The per-row slot count `int32_t* __restrict piTrailCounts` (`Explosions.h:149`) is clamped to that maximum only at spawn — `std::min(rInfo.uiTrailCount, kiMaxExplosionTrails)` (`ExplosionsSpawn.cpp:51`).

`piTrailCounts` is a shared serialized member (`Explosions.h:166`, `SharedMembers()`), so it arrives from replay `.fullframes` snapshots, saves, and network full-state. Nothing re-clamps or validates it on the read path: `Collection::Read` (`Collection.h:323-338`) validates count/capacity relationships and the ID map, but never per-element member values. A stream carrying `piTrailCounts[i] > 8` therefore survives deserialization intact.

The corrupt value is also self-propagating: `ExplosionsUpdate.cpp:35` reads `rPrevious.piTrailCounts[i]` and line 46 stores it unchanged into `rCurrent`, so an out-of-range count survives every subsequent tick rather than decaying.

Four consumers then index the fixed 8-pointer arrays with that unclamped value:

- `Engine/Source/Frame/Collections/Explosions/Explosions.cpp:283-286` — **shared path, both builds** (outside the `BT_CLIENT` guard closed at line 271). The end-time loop `for (int32_t j = 0; j < iTrailCount; ++j)` reads `pfTrailTimes[j][i]`, where `iTrailCount` comes from `piTrailCounts[i]` at line 250.
- `Engine/Source/Frame/Collections/Explosions/Explosions.cpp:324` — `ExplosionsInterpolate::LogDifferences`, `for (int64_t j = 0; j < piTrailCounts[i]; ++j)` reading `pfTrailTimes[j][i]` and `rOther.pfTrailTimes[j][i]`. Reachable from `DifferenceStreamReader::ValidateChecksum` (`Engine/Source/File/DifferenceStream.h:382`) on checksum mismatch. Only the first snapshot is CRC-validated (`DifferenceStream.h:315-317`), so a later snapshot's corrupt value reaches this loop.
- `Engine/Source/Frame/Collections/Explosions/ExplosionsUpdate.cpp:76` — client-only trail-position sync (inside the `BT_CLIENT` guard opened at line 71), indexing `pTrails[j][i]`, `pfTrailTimes[j][i]`, and `pVecTrailEndPositions[j][i]`. Runs every tick against the copied-forward count, so this is the highest-frequency exposure.
- `Engine/Source/Frame/Collections/Explosions/Explosions.cpp:257-259` — client-only trail cleanup indexing `pTrails[j][i]` and `pfTrailTimes[j][i]`.

The safe pattern already exists in the same collection: the copy loop at `ExplosionsUpdate.cpp:49` bounds on `kiMaxExplosionTrails` and tests `j < iTrailCount` inside, and `ExplosionsSpawn.cpp:128` loops a freshly clamped local. Note that `ExplosionsUpdate.cpp` contains both shapes — the safe loop at 49 and the unguarded one at 76 — so the file cannot be treated as uniformly safe. Only the four sites above trust the stored value as a bound.

## Design

Normalize the trail count at the read boundary so every present and future consumer inherits the guarantee, rather than hardening each consumer.

**Policy decision (resolved, do not re-open): clamp into `[0, kiMaxExplosionTrails]`; do not throw.** The two candidate policies were rejection via `common::CorruptStreamException` — the pattern used elsewhere in `Collection::Read` — and clamping. Rejection is wrong here for a concrete reason: the replay path deserializes at `mFullFramesStream >> savedFrame` (`DifferenceStream.h:366`), inside `ValidateChecksum`, which has no catch on the playback tick path. A throw there would propagate out of playback and destroy the diagnostic this collection's difference logging exists to produce. Clamping is also safe in a way rejection-worthy fields are not: `piTrailCounts` is a count of *fixed, already-allocated* slots, not a size that drives allocation or buffer layout, so a clamped value cannot produce a torn layout or an overrun — it only limits iteration.

1. Clamp each row's `piTrailCounts` value into `[0, kiMaxExplosionTrails]` on the `ExplosionsInterpolate` read path, after rows are read and before any consumer can observe them. Apply it on every read path that populates the member (build-local `Read` and cross-build `ServerRead`), so replay, save, and network full-state are covered identically.
2. Log a `kWarning` when a value is clamped, naming the row and the offending value. A corrupt replay must stay diagnosable, which is the whole reason this path is not allowed to throw.
3. Leave all four consumer loops exactly as they are once the boundary guarantees the invariant. Do not add per-consumer clamps as well. Because `ExplosionsUpdate.cpp:46` copies the count forward unchanged, clamping once at read is sufficient — no per-tick re-clamp is needed, and adding one would be redundant.

Determinism note: the clamp changes deserialized bytes only for out-of-range input. Valid input is untouched, so shared CRC and replay/save round-trips are bit-identical to today. Both sides run the same clamp on read, so a corrupt stream cannot make one side iterate further than the other.

## Critical files

- `Engine/Source/Frame/Collections/Explosions/Explosions.h` — `piTrailCounts`, `pfTrailTimes`, `kiMaxExplosionTrails`, `SharedMembers()`
- `Engine/Source/Frame/Collections/Explosions/Explosions.cpp` — three of the four consuming loops (lines 257-259, 283-286, 324); read-only reference, not edited by the chosen design
- `Engine/Source/Frame/Collections/Explosions/ExplosionsUpdate.cpp` — the fourth consuming loop (line 76) and the copy-forward at lines 35 and 46; read-only reference, not edited by the chosen design
- `Engine/Source/Frame/Collections/Collection.h` — `Collection::Read` (lines 323-338) and `SharedCollectionRead`, the boundary the clamp attaches to
- Read-only reference: `Engine/Source/File/DifferenceStream.h` (`ValidateChecksum`, lines 315-317 and 360-386), `Engine/Source/Network/Client/Client.cpp` receive catch, `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` `ReadGrid` catch

## Out of scope

- Any change to CRC composition, `SharedMembers()`/`Members()` tuples, collection layout, `kiVersion`, `.pack` layout, wire protocol, or serialization order. The clamp must not alter the format or the bytes of valid input.
- Generic per-element member validation for other collections. Only the explosion trail count is proven exposed; a general mechanism is a separate architectural decision.
- The ID-map bijection and capacity-stride ceilings addressed by completed collection-read hardening — different root cause and boundary, no file-region overlap.
- Changing any consumer loop's diagnostic behavior, and converting any existing `CorruptStreamException` path to clamping.

## Risk tier and invariants

Tier 3: shared engine collection deserialization on the deterministic reconstruction path, reached from replay, save/load, and client live-network full-state. Known-good input must round-trip byte-identically with the shared CRC unchanged; only corrupt input changes outcome, from an out-of-bounds read to a bounded, logged iteration.

## Acceptance criteria

- A replay snapshot, save, or full-state carrying `piTrailCounts[i] > kiMaxExplosionTrails` is clamped at the read boundary, emits one `kWarning`, and never reaches `Explosions.cpp:257`, `:283`, `:324`, or `ExplosionsUpdate.cpp:76` as a loop bound above 8 — including on later ticks, after the count has been copied forward by `ExplosionsUpdate.cpp:46`.
- A stream carrying a negative `piTrailCounts[i]` is clamped to 0 and logged the same way, so the lower bound of the declared `[0, kiMaxExplosionTrails]` range is verified rather than assumed.
- With an over-max value present, a checksum mismatch at the affected tick completes its `LogDifferences` report and playback continues — no out-of-bounds access, no exception escaping `ValidateChecksum`.
- A known-good save, replay, and network full-state round-trip byte-identically; shared CRC unchanged.
- Debug client and server builds pass.

## Notes

- Scoring: Effort 2, Impact 3, Risks 2. Narrow trust-boundary clamp closing an out-of-bounds read on corrupt replay/save/network input.
- Line citations are tip-of-2026-07-25; refresh them if the explosion collection or collection read path moves first.
