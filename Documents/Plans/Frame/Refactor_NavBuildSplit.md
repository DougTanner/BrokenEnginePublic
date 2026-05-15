# Refactor: Split NavBuild.cpp

## Context

`Engine/Source/Frame/NavBuild.cpp` is 1262 lines, well past the `/reduce-file` threshold. The auto-crop islands change (May 2026) flagged it during post-implementation review but did not touch the underlying structure — the file was already large.

Secondary concern from the same review: `BuildNavContour` reserves `iHeightmapWidth * iHeightmapHeight` `ContourEdge` entries at line 1053. Marching squares only emits edges in cells straddling the threshold (perimeter-sized, not area-sized), so the reserve is one to two orders of magnitude larger than the working set. At the current 8192 `texturePixels` (post-crop ~1004×1072 downsampled heightmap), this is ~17 MB / island template at boot; at higher bake resolutions it scales linearly.

## Out of scope

- The anisotropic `BuildNavContour` / `ExtractContourEdges` signature change — already landed with the auto-crop work.
- Any change to NavQuery (`NavQuery.cpp`) — file size is healthy.
- Reworking the marching-squares case table or polygon ops algorithms; this is a mechanical split, not an algorithm change.

## Design

Split `NavBuild.cpp` along its natural seam lines into ≤4 translation units sharing the existing `NavBuild.h`. Suggested decomposition based on the existing top-down structure of the file:

1. **`NavBuildContour.cpp`** — Marching-squares edge extraction (`ExtractContourEdges`, the case-switch and lerp helper) and edge-chaining into polygons (`ChainEdgesIntoPolygons` and the `ContourEdge` struct).
2. **`NavBuildPolygon.cpp`** — Per-polygon refinement: `SimplifyPolygon`, `SmoothDenseClusters`, `InflatePolygon`, `MergeOverlappingPolygons` (and the boolean-union plumbing they sit on).
3. **`NavBuildVisibility.cpp`** — Visibility-graph construction (`BuildVisibilityGraph` plus its segment-intersection helpers).
4. **`NavBuild.cpp`** (slimmed) — `BuildNavContour` orchestrator + `BuildCellNavData` UV→world rebase. Both are short coordinator functions that thread the above through a single sequence.

Anonymous-namespace helpers used by exactly one TU move into that TU. Anything shared between two TUs (e.g., `ContourEdge`) goes into an internal `NavBuildInternal.h` next to the headers, kept out of `NavBuild.h` so consumers don't see it.

### Reserve correction (drop-in)

At the new `NavBuildContour.cpp`'s `BuildNavContour` callsite (originally `NavBuild.cpp:1053`), replace `contourEdges.reserve(width * height)` with a perimeter-bounded estimate — e.g., `4 * (width + height)` (one edge per boundary cell, room for diagonal cases). Confirm against a couple of real islands that the vector does not grow beyond that estimate post-bake before deleting the over-large reserve.

## Critical files

- `Engine/Source/Frame/NavBuild.cpp` — split into the four files above.
- `Engine/Source/Frame/NavBuild.h` — unchanged public surface.
- `Engine/Source/Frame/NavBuildInternal.h` — new internal-only header for `ContourEdge` (if it lives in more than one new TU).
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/*.vcxproj{,.filters}` — add the three new files to both client and server projects.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.vcxproj{,.filters}` — same.

## Acceptance criteria

- `NavBuild.cpp` ≤ 350 lines; no other resulting TU exceeds the same threshold.
- Public API in `NavBuild.h` byte-identical to today (no signature drift).
- `kiNavDataVersion` does **not** change — the on-disk NavData format is unchanged; this is a pure code reorganization plus one reserve fix.
- Determinism check: client + server still pass the cross-build CRC validation on the existing islands; per-template `NavContour` vertex CRC is identical before/after.
- `BuildNavContour` peak heap (measured via the allocation tracker on a 1004×1072 island template) drops by ≥10× relative to current.

## Order.md row

```
| <N> | [Frame/Refactor_NavBuildSplit.md](Frame/Refactor_NavBuildSplit.md) | Medium | 3 | 3 | 2 | 2 | Split 1262-line NavBuild.cpp into contour / polygon / visibility translation units; fix oversized `ContourEdge` reserve while down there. Pre-existing debt surfaced by the auto-crop islands review. |
```
