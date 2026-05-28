# Speed up / gate the O(edges^2) debug crossing-detection loop in BuildCellNavData

## Context

`engine::BuildCellNavData` (`Engine/Source/Frame/NavBuild.cpp`, the double loop currently around lines
604-652, the `[DEBUG-nav-crossing]` check) tests **every obstacle edge against every other obstacle edge**
for a proper intersection — O(total_edges^2) — on **every cell's first NavData build**. With single-island
cells this was cheap; with multi-island archipelago cells (8-40 islands, thousands of edges after the chain
overhaul) it becomes a one-time-per-cell build hitch on the dispatch thread.

It is a pure debug diagnostic: the `DEBUG_BREAK()` is already commented out (`NavBuild.cpp:647`); the loop
only LOGs crossings (self-intersecting contour or overlapping placements that would make the visibility
graph misbehave). It is **build-time** work, not the per-frame `ComputeNavigation` bottleneck that was just
optimized (broad-phase edge grid + AABB rejects + A\* heap/CSR + per-player throttle) — which is why it was
deferred out of that effort.

## Design

### Primary: gate and/or accelerate the crossing check

- **Option 1 (recommended, KISS): compile-time gate.** Wrap the whole double loop behind a build-time debug
  toggle (the existing `kbDebug*`-style `inline constexpr` flag convention in `Pch.h`) so release/profile
  builds skip it entirely and never pay the O(E^2) cost. The check exists only to catch a producer-side
  contour/placement defect during development; it does not need to run in shipped builds.
- **Option 2 (only if the check must stay on in debug for large cells): reuse the edge grid.** The
  broad-phase acceleration `engine::BuildNavAcceleration` already builds a per-cell obstacle-edge grid
  (`NavData::gridEdgeOffsets` / `gridEdges`, bucketed from `NavData::edgeA` / `edgeB`). Reformulate the
  crossing test to only compare edges that share a grid cell — O(E^2) → roughly O(E × local). **Ordering
  caveat:** `BuildNavAcceleration` currently runs at the **end** of `BuildCellNavData`
  (`NavBuild.cpp:654`), *after* this debug loop, so the grid does not yet exist when the check runs. If
  Option 2 is chosen, build the edge grid (or at least the `edgeA`/`edgeB` + CSR buckets) before the debug
  check, or move the check after `BuildNavAcceleration`.

Recommend **Option 1** as the simplest fix; reach for Option 2 only if there is a demonstrated need to keep
the diagnostic active on large cells in debug builds. The two are not mutually exclusive (a gated Option-2
loop is also valid), but do the simplest thing that removes the hitch.

### Secondary quick-wins (low priority — not the main driver)

Discovered while reviewing the same files during the broad-phase optimization. All pre-existing; none
introduced by that work. Fold in opportunistically, but the crossing-check gate is the reason this plan
exists.

1. **LOG-float convention violations in `NavBuild.cpp`.** The LOG calls around lines 427, 510, and 600 pass
   raw `float` / bare `{}` placeholders without `common::Wb(...)` wrapping (e.g. `BuildNavContour`'s
   `worldThreshold` log at :427, the `total vertices/polygons` log at :510, the per-polygon bounds
   `kVerbose` log at :600). These run under `ScopedSuppressAllocationTracking` at build time, so they do not
   trip the main-loop allocation tracker — but they still violate the project LOG-float rule (wrap each
   float with `common::Wb(value, precision)`, placeholder stays `{}`). Fix for convention compliance.

2. **Dead local in `ComputeNavigation`.** `Projects/.../Players/PlayersNavigation.cpp`, around line 152:
   `XMVECTOR vecArea = rStaticData.vecArea;` is assigned but never read in the function. Remove it.

3. **(Optional, behavioral) mode-5 flagship-follow heading staleness.** The just-shipped throttle lets
   mode 5 (flagship follow, `riNavDirection == 5` in `ComputeNavigation`) reuse a cached `rVecAiDirection`
   for up to ~`kiNavRecomputeInterval` (16) ticks even though the flagship moves every tick. Could force an
   immediate recompute when the flagship has moved beyond a threshold from the last cached
   `rVecIslandDestination`. Low priority and **behavioral** (must preserve client/server determinism — the
   trigger must derive only from serialized state, same as the existing `(tick + globalId)` cadence).
   Currently backstopped by terrain-push and the mode-transition force-recompute, so the staleness is
   bounded and self-correcting. Defer unless visibly objectionable.

4. **(Optional, traceability only) grid-DDA boundary false-negative in `SegmentBlockedByObstacle`.** The
   Amanatides-Woo DDA in `engine::SegmentBlockedByObstacle` (`NavQuery.cpp:42`) has an astronomically-rare
   theoretical false-negative when a crossing lands within ~1 ULP of a grid-cell boundary (the DDA's
   incremental cell stepping can disagree with `NavGridCell`'s direct placement). It is **deterministic**
   (identical on client/server under `/fp:strict`, so no desync) and **self-correcting** (the next throttled
   recompute plus the unconditional terrain-push recovers it). Already documented in a code comment. Listed
   here only for traceability; if ever desired, harden by dilating the edge-bucket range by one cell, or by
   visiting the corner-adjacent cell when the two DDA `tMax` values are within epsilon. Do **not** action
   without a reproduced symptom.

## Critical files

- `Engine/Source/Frame/NavBuild.cpp` — `engine::BuildCellNavData` crossing-detection double loop (gate
  and/or accelerate); the three LOG-float sites (~427, ~510, ~600); `engine::BuildNavAcceleration` edge grid
  reused by Option 2.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp` —
  `PlayersPostRender::ComputeNavigation` dead `vecArea` local (~152); the optional mode-5 recompute trigger.
- `Engine/Source/Frame/NavQuery.cpp` — `engine::SegmentBlockedByObstacle` (~42), DDA boundary note
  (traceability only).
- `Projects/BrokenEngineSandbox/Source/Pch.h` — home of the `kbDebug*`-style compile-time toggle if Option 1
  introduces a new flag (reuse an existing one if it fits).

## Out of scope

- The **per-frame pathfinding optimization itself** — broad-phase edge grid, AABB rejects, A\* heap/CSR, and
  the per-player throttle already landed; this plan does not revisit them.
- **Replacing the pathfinder** with Recast/Detour — see `Frame/EvaluateRecastDetourNavmesh.md`.
- The DDA hardening (secondary item 4) beyond documenting it — no change without a reproduced symptom.
- Any `engine::kiNavDataVersion` / serialization change — all items here touch derived/debug/build-time code,
  not the serialized `NavData` layout.

## Acceptance criteria

- The O(E^2) crossing check no longer hitches a multi-island cell's first build in profile/release — either
  it is compiled out (Option 1) or its complexity is reduced via the edge grid (Option 2), confirmed by the
  build-time timer / observation on a worst-case archipelago cell.
- The three `NavBuild.cpp` LOG calls wrap their float arguments per the project LOG-float rule.
- The dead `vecArea` local is removed.
- No determinism regression: the crossing check is diagnostic-only (no shared state), the LOG and dead-local
  fixes are no-ops on behavior, and any mode-5 trigger (if taken) derives only from serialized state.

## Notes

- Scores: **Effort 2** (Small — primarily a flag-gate plus a couple of mechanical quick-wins, one subsystem),
  **Impact 3** (Real — removes a genuine multi-island build hitch and clears convention violations),
  **Risks 1** (Low — debug-only / build-time code, compile-checked, narrow scope, easily reverted). Score =
  2 − 3 + 1 = **0**, tier **Small**.
- Items 1-2 are unconditional quick-wins; items 3-4 are explicitly optional and should not expand the diff
  unless a symptom is reproduced.
- If Option 2 is chosen, re-confirm the `BuildNavAcceleration` call position (`NavBuild.cpp:654`) at
  execution time — line numbers drift.
