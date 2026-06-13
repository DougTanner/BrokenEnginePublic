# Refactor: Nav/Island Function Decomposition

## Context

Source: /external-refactor-clean on `Engine/Source/Frame` (non-recursive). Four functions in the nav/island cluster exceed ~100 lines or nest 4–5 control-flow levels; each has a clean extraction seam that cuts the host function to a readable linear core. All extractions are move-only relocations within the same TU (file-local helpers) — zero behavior change, zero determinism exposure.

## Design

### Engine/Source/Frame/NavQuery.cpp
- `AStarPath` (lines 366–577, 212 lines): the six captured-lambda heap helpers `HeapLess`/`HeapSwap`/`SiftUp`/`SiftDown`/`HeapPush`/`HeapPop` (lines 401–480) are a self-contained indexed min-heap — extract into a file-local struct (holding `pOpenSet`/`pHeapPos`/`iHeapCount` with member functions) in the anonymous namespace, cutting ~80 lines and one indirection level from the A* loop [~30m]
- `NavQueryDirection` (lines 601–717, 117 lines): extract the A*-miss "nearest visible vertex" fallback (lines 684–713, 4 nesting levels) into a file-local helper, leaving an ~85-line linear early-out structure [~15m]

### Engine/Source/Frame/NavBuild.cpp
- `BuildCellNavData` (lines 547–660, 114 lines after the per-polygon density-analysis LOG block was deleted): extract the 5-deep `if constexpr (kbDebugNavCrossingCheck)` diagnostic block (lines 609–657, compiled out in all three Pch.h configs) into a file-local `DebugCheckCrossingEdges(const NavData&)` — it already needs the cross-domain `SegmentsIntersect` that `NavBuildSplit.md` promotes [~15m]
- `BuildVisibilityGraph` (lines 371–421): the adjacency test (lines 381–397) re-scans `polygonOffsets` inside the O(V²) pair loop (4–5 nesting levels, O(V²·P) work). Precompute a per-vertex polygon-index array once (O(V)) so adjacency becomes an O(1) compare — removes two nesting levels; identical results (clarity win first, boot-perf win second — server boot path only) [~20m]

### Engine/Source/Frame/IslandTerrain.cpp (client-only span)
- `AcquireTextureSlot` (lines 504–619, 116 lines): extract the first-mint block (lines 525–607 — slot pick, elevation create, 5× array pointer wiring, 5× binding registration) into a file-local helper beside `CreateElevationTextureFromHeightmap` (line 476) [~20m]

## Critical files
- `Engine/Source/Frame/NavQuery.cpp`
- `Engine/Source/Frame/NavBuild.cpp` (post-split: `NavCellData.cpp` for the `BuildCellNavData` item)
- `Engine/Source/Frame/IslandTerrain.cpp` (post-split: `IslandTerrainResidency.cpp` for the `AcquireTextureSlot` item)

## Out of scope
- Any behavior, algorithm, or numeric change — every extraction must produce identical results (nav results steer CRC'd sim positions)
- `BuildNavContour` (121 lines), `IslandTerrain::IslandTerrain()` (125 lines), `IslandChainPlacement::Generate` (98 lines, after the TEMP fill-diagnostic deletion) — reviewed and deliberately left intact (linear, well-sectioned; KISS)
- The lazy `pEndVisible` A* optimization (own plan: `Refactor_AStarEndVisibleLazy.md`)
- File splits (owned by `Frame/NavBuildSplit.md` / `Frame/IslandTerrainSplit.md`)

## Notes
- **Ordering**: land after (or with) `Frame/NavBuildSplit.md` and `Frame/IslandTerrainSplit.md` — both splits relocate functions this plan edits (`BuildCellNavData` → `NavCellData.cpp`, `AcquireTextureSlot` → `IslandTerrainResidency.cpp`); execute against the post-split files.
- No CRC/serialization/`kiNavDataVersion` exposure; the `BuildVisibilityGraph` precompute changes work done but not results.

## Verification Notes (2026-06-10)
- All four extraction seams verified against source:
  - `AStarPath` `NavQuery.cpp:366-577` (212 lines); the six heap lambdas span `:401-480` exactly (`HeapLess`:401, `HeapSwap`:412, `SiftUp`:422, `SiftDown`:436, `HeapPush`:460, `HeapPop`:468) and capture only `rMemory` arrays + `iHeapCount` — the proposed struct (pOpenSet/pHeapPos/iHeapCount) is a clean move.
  - `NavQueryDirection` `:601-717` (117 lines); A*-miss fallback at `:684-713` as cited.
  - `BuildCellNavData` `NavBuild.cpp:547-660` (114 lines, after the per-polygon density-analysis LOG block deletion); the `kbDebugNavCrossingCheck` block is `:609-657`, the constant is `false` in all three configs (`Pch.h:34/51/68`), and the block calls `SegmentsIntersect` (`:648`) — the cross-domain dependency `NavBuildSplit.md` promotes, as stated.
  - `BuildVisibilityGraph` `:371-421`; the adjacency test (`:380-397`) re-walks `polygonOffsets` per (i,j) pair. The per-vertex polygon-index precompute is result-identical: adjacency is `same polygon && (consecutive local indices || first-last wrap)` — derivable O(1) from a vertex→polygon array plus the offsets. Server-boot-only confirmed (`BuildNavContour` → `BuildVisibilityGraph` at `:542`, called from `WaitForElevationMaps` inside the server-gated NavContour build, `IslandTerrain.cpp:201`).
- One cite corrected: first-mint block in `AcquireTextureSlot` ends at `:607` (the `return iSlot;`), not 608; helper target `CreateElevationTextureFromHeightmap` is at `IslandTerrain.cpp:476`. Slot pick `:536-547`, 5× pointer wiring `:558-562`, 5× `Register(...)` `:592-596` all verified.
