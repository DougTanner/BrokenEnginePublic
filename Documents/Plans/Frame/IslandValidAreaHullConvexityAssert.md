# DataPacker: Assert Valid-Area Hull is Convex + CCW at Bake Time

## Context

The island-chain placement overhaul (`IslandChainPlacement`) packs islands by testing their rotated
**valid-area hull** for overlap with `common::ConvexHullsOverlap` (a Separating Axis Theorem test in
`Common/MathUtils.h`). SAT is only correct when **both** input polygons are **convex** and consistently
wound (CCW). The runtime consumes `IslandTemplate::mpf2ValidAreaVertices` on faith — the convex/CCW
contract is documented in a comment (`IslandTerrain.h`) and produced by DataPacker's
`BuildValidAreaHull` (`DataPacker/Source/ExportJobs/ExportIsland.cpp`, Andrew's monotone chain, which
*does* produce a CCW convex hull) but is **never verified**.

If the bake ever emits a non-convex or wrongly-wound polygon (e.g. a future change to the hull
generator, or a degenerate/collinear input), SAT silently under-reports overlap and islands visibly
intersect near the waterline with no assert firing — a hard-to-trace regression.

This was surfaced by the code-review step of the placement overhaul (finding #2) and deferred as
out-of-scope (it is producer-side, in DataPacker, not in the placement change).

## Problem

No bake-time validation that the exported valid-area hull satisfies the convex + CCW precondition that
the runtime SAT packing depends on.

## Proposed change

In `DataPacker/Source/ExportJobs/ExportIsland.cpp`, right after `BuildValidAreaHull` produces the hull
(and only when it has >= 3 vertices), assert:

1. **CCW winding**: the signed area (shoelace) is strictly positive.
2. **Convexity**: every consecutive edge cross product has the same (positive) sign — i.e. the polygon
   turns left at every vertex.

Use the existing `Cross` lambda pattern already in that file. Both checks are O(n) over the small hull
vertex list and run only at bake time (no runtime cost). Match the file's existing `ASSERT` style.

## Critical files

- `DataPacker/Source/ExportJobs/ExportIsland.cpp` — add the convex + CCW assert after `BuildValidAreaHull`.

## Notes

- Pure producer-side validation; no runtime or format change, no `kiNavDataVersion` bump.
- If the assert ever fires on a real asset, that island's hull generation needs investigation — do not
  weaken the runtime SAT to tolerate non-convex input.

## Verification

- Rebuild DataPacker (Release) and run a full island export; confirm no convex/CCW assert fires on the
  current asset set (it should not — monotone chain already yields a CCW convex hull).
