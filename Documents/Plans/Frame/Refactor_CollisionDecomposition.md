# Refactor: Collision.cpp Decomposition and DRY

## Context

Source: /external-refactor-clean on `Engine/Source/Frame` (non-recursive). `Collision.cpp` has the directory's deepest in-function complexity: `CollideLayerPair` is 177 lines nesting to depth 5, with two exactly-mirrored result-recording blocks; `InsertObjectIntoZones` duplicates its whole body across a `bIsLayerA` if/else; `SetupZones` duplicates its insert loop. All changes are behavior-identical restructurings — collision results feed CRC'd sim state, so outputs must be bit-identical.

## Design

### Engine/Source/Frame/Collision.cpp
- `CollideLayerPair` (lines 379–555, depth 5): extract the per-pair test body (lines 433–551) into a file-local `TestAndRecordPair` helper, flattening the function to the zone-walk skeleton [~30m]
- Within the extracted body: the two mirrored `PushBack<PendingCollisionResult>` blocks (A's result lines 507–526, B's lines 531–550, plus the duplicated `kDestroyOnCollide` marks) differ only in which layer is "self" — replace with one `RecordCollision(...)` helper called twice with swapped arguments [~15m]
- `InsertObjectIntoZones` (lines 99–132): the `bIsLayerA` if/else bodies (108–117 vs 120–129) are identical modulo `indicesA/iCountA` vs `indicesB/iCountB` and one log letter — select `std::vector<int64_t>& rIndices` / `int64_t& riCount` before the branch and keep one body [~10m]
- Same function, lines 106–107: `sLayers.at(...)` is fetched every zone iteration of this hot per-object loop but consumed only by the rare overflow LOG (lines 112, 123) — move both lookups inside the overflow branches [~5m]
- `SetupZones` (lines 178–275): optional — the two near-identical insert loops (248–257 vs 260–269) differ only in layer and the `bIsLayerA` flag; a 3-line lambda removes one. Skip if the result reads worse (KISS judgment at execution) [~10m]

## Critical files
- `Engine/Source/Frame/Collision.cpp`

## Acceptance criteria
- Identical collision results for identical inputs (same pairs recorded, same order, same prefix-sum spans) — collision output feeds CRC'd downstream phases.
- `CollideLayerPair` reads as the zone-walk skeleton at one or two nesting levels.

## Out of scope
- `SweptSphereTest`'s 8 parameters — canonical `FXMVECTOR/GXMVECTOR/HXMVECTOR` register-convention idiom; do not struct-ify (reviewed, WONTFIX)
- Overflow-path suppression and the `sLayerBaseOffsets` overrun (`Refactor_CollisionOverflowGuards.md` — landed and removed; `AddLayer` overflow is now fatal via `ASSERT(false)`, the other growth branches carry suppression)
- Header surface narrowing (own plan: `Architecture_CollisionHeaderSurface.md`)
- Any zone-grid, mask, or test-semantics change

## Notes
- Determinism exposure: nominally none (move-only restructuring), but this is the hot lockstep collision path — verify a replay reproduces after landing.
- Co-schedule with the other `Collision.cpp` plans (`Architecture_CollisionHeaderSurface.md`, the `Refactor_StyleMechanics.md` Collision items; `Refactor_CollisionOverflowGuards.md` has landed and been removed) in one session — shared file, stale-line risk.
