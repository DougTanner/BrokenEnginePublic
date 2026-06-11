# Architecture: Collision Header Surface Narrowing

## Context

Source: /external-architecture-review on `Engine/Source/Frame` (non-recursive), implementation details verified by the refactor pass (repo-wide grep). `Collision` is one of the directory's deep modules — a 5-method interface hiding zone partitioning, swept-sphere math, and prefix-sum result bucketing — but `Collision.h:68-112` publicly exposes four implementation-detail types (`PendingCollisionResult`, `ZoneRange`, `ZonePair`, `LayerPairZones`) referenced **nowhere** outside `Collision.h`/`Collision.cpp`. Moving them into the .cpp drops ~45 lines of internals from the header and makes the interface honest.

## Design

### Engine/Source/Frame/Collision.h / Collision.cpp
- Move `PendingCollisionResult` (lines 69–74) into `Collision.cpp` — used only at cpp lines 296, 299, 337, 507, 531; zero header references [~5m]
- Move `ZoneRange` (lines 77–83) into the cpp; keep a `struct ZoneRange;` forward declaration in the header — its only header uses are private member-function declarations (return type at :135–136, const-ref param at :137), fine with an incomplete type [~10m]
- Move `ZonePair` (lines 86–92) and `LayerPairZones` (lines 95–112) into the cpp; forward-declare `LayerPairZones` — header uses are private declarations (:137–138) and the static member declaration `static thread_local std::vector<LayerPairZones> sLayerPairZones;` (:150), which is declaration-only (no instantiation; `std::vector<T>` accepts incomplete `T` since C++17; the type is complete at the definition `Collision.cpp:22` and all use sites). Compile-check this declaration specifically [~15m]
- While moving, add member initializers to `PendingCollisionResult` and `ZoneRange` (style rule 36 — currently none; latent rather than live since all construction is designated-init) [~5m]
- Keep `kiCollisionZonesX/Y` and `kiCollisionZonePreallocate` in the header (referenced by declarations and the game-side preallocate documentation contract)

## Critical files
- `Engine/Source/Frame/Collision.h`
- `Engine/Source/Frame/Collision.cpp`

## Out of scope
- Any change to the public 5-method interface or the zone-grid constants
- `CollideLayerPair` restructuring (own plan: `Refactor_CollisionDecomposition.md`)
- Overflow-path fixes (own plan: `Refactor_CollisionOverflowGuards.md`)

## Notes
- No determinism/CRC/serialization exposure — type relocation only, both builds compile-checked.
- Co-schedule with the other `Collision.{h,cpp}` plans in one session (shared file).

## Verification Notes (2026-06-10)
- Repo-wide grep: `PendingCollisionResult`, `ZoneRange`, `ZonePair`, `LayerPairZones` appear only in `Collision.h`, `Collision.cpp`, and plan documents — zero external consumers confirmed.
- Cites verified: `PendingCollisionResult` (h:69-74; cpp uses at 296/299/337/507/531), `ZoneRange` (h:77-83; header uses only in private declarations :135-137 — return-by-value and const-ref param declarations are legal with an incomplete type), `ZonePair` (h:86-92, used only inside `LayerPairZones`), `LayerPairZones` (h:95-112; header uses at :137-138 and the static member declaration :150). Moving lines 68-112 ≈ 45 lines as claimed.
- The `static thread_local std::vector<LayerPairZones>` claim checks out in principle: a static data member *declaration* of `std::vector<T>` does not require complete `T` (C++17 incomplete-type support for vector with the default allocator), and the out-of-class definition at `Collision.cpp:22` will see the complete type once the structs move above it. The plan's explicit compile-check caveat is retained — MSVC conformance on this corner is the residual risk.
- Member-initializer item confirmed against style rule 36 (`C++StyleGuide.txt:149`); both structs currently have uninitialized members and all current construction is designated-init (`Collision.cpp:60-66, 507-520, 531-544`) — latent, as stated.
- `kiCollisionZonesX/Y`/`kiCollisionZonePreallocate` must indeed stay in the header (also note `kiCollisionLayerPreallocate` backs the `sLayerBaseOffsets` member declaration at h:157, and `kiCollisionLayerPairPreallocate`/result constants are used by Collision.cpp — the plan's "keep constants" item covers this).
