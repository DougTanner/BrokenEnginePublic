# Architecture: Nav Geometry Predicate Dedup

## Context

Source: /external-architecture-review on `Engine/Source/Frame` (non-recursive). The build-time visibility graph and the runtime query duplicate their core geometry predicates: `SegmentsIntersect` is byte-identical in `NavBuild.cpp:271-292` and `NavQuery.cpp:13-34` (same `1e-10f`/`1e-6f` epsilons), and the winding-number point-in-polygon core is duplicated (`NavBuild.cpp:295-328` vs `NavQuery.cpp:198-227`). If an epsilon is ever tuned in one copy only, the visibility graph and the runtime query disagree — paths thread through walls. Shared nav types already live in `NavBuild.h` (`NavGridCell`, `NavBuild.h:26-35`); the predicates belong beside them.

## Design

### Engine/Source/Frame/NavBuild.cpp / NavQuery.cpp (+ the shared header)
- Promote one `SegmentsIntersect` definition to external linkage and delete the `NavQuery.cpp:13-34` copy. `Frame/NavBuildSplit.md` already promotes it into a private `NavBuildInternal.h` for its two TUs — extend that header to serve `NavQuery.cpp` too (or hoist the declaration into `NavBuild.h` beside `NavGridCell` if a three-TU "private" header reads wrong) [~15m]
- Consolidate the winding-number point-in-polygon core: first verify the two copies (`NavBuild.cpp:295-328`, `NavQuery.cpp:198-227`) are behaviorally identical (boundary semantics, broad-phase pre-checks may differ between the wrappers); share the identical inner core, keep any caller-specific wrapping local [~30m]

## Critical files
- `Engine/Source/Frame/NavBuild.cpp` (or post-split `NavBuildInternal.h`)
- `Engine/Source/Frame/NavQuery.cpp`
- `Engine/Source/Frame/NavBuild.h`

## Out of scope
- Changing any epsilon or predicate behavior — consolidation must be bit-identical (nav results steer sim entities whose positions feed the replay CRC)
- Perf changes to the predicates
- Splitting `NavQuery.cpp`

## Notes
- **Ordering dependency**: execute after (or in the same session as) `Frame/NavBuildSplit.md` — it relocates and re-linkages `SegmentsIntersect` and owns the `NavBuildInternal.h` shape this plan extends.
- Determinism exposure: nominally none (move-only consolidation of identical code), but it edits sim-feeding nav code — verify byte-identical behavior before landing.

## Verification Notes (2026-06-10)
- `SegmentsIntersect` bodies diffed token-by-token: `NavBuild.cpp:271-292` and `NavQuery.cpp:13-34` are identical (same locals, same `1e-10f` denominator guard, same `kfSegmentEpsilon = 1e-6f`, same return expression); only the leading comments differ. Byte-identical claim holds for the code.
- Winding-number cores characterized as the plan anticipated: `NavBuild.cpp:295-328` is a bare single-polygon `PointInPolygon(point, pVertices, iVertexCount)`; `NavQuery.cpp:182-234` (`PointInAnyPolygon`) wraps a line-for-line identical winding loop (`:198-227`) in a multi-polygon iteration with a per-polygon AABB broad phase (`:186-192`) and offset indexing (`pVertices[iStart + i]`). Same boundary semantics (`<=`/`>` y-tests, `fCross > 0` / `< 0`). Consolidation shape: have `PointInAnyPolygon` call the shared single-polygon core per polygon after its AABB rejection — bit-identical (broad phase only skips polygons whose core result is necessarily zero-winding).
- `NavBuild.cpp`'s core is also consumed by `MidpointInsideObstacle` (`:352-368`) passing `&rVertices.at(iStart)` + count — the shared signature must stay pointer+count to serve both.
- `NavBuildSplit.md` re-read: it promotes `SegmentsIntersect` to external `engine::` linkage declared in `NavBuildInternal.h` — extending that header to `NavQuery.cpp` (third TU) or hoisting to `NavBuild.h` both remain viable; ordering note stands (the split has not landed; `NavBuild.cpp` is still one 910-line TU).
