# NavBuild.cpp Domain Split

## Context

`Engine/Source/Frame/NavBuild.cpp` is 934 lines and approaching the 1000-line `/reduce-file` soft threshold. A codebase sweep flagged it as the strongest un-tracked split candidate. The file holds two cleanly-separable domains that share only a small set of types and one geometry predicate:

- **Contour-build domain** (`NavBuild.cpp:12-545`): marching-squares isocontour extraction, polygon chaining, Clipper2 union/inflate/simplify, and the visibility-graph builder. Functions: file-local `EncodeEdgeKey`, `ExtractContourEdges`, `ChainEdgesIntoPolygons`, `SegmentsIntersect`, `PointInPolygon`, `SegmentIntersectsAnyEdge`, `MidpointInsideObstacle`, `BuildVisibilityGraph`; public entry `BuildNavContour`. This domain owns the `ContourEdge` file-local struct, consumes Clipper2 (`Clipper2Lib::Union`/`InflatePaths`/`SimplifyPaths`/`Area`, available via the PCH `Common/ExternalHeaders.h`), and is the only producer of `NavContour` content.
- **Cell-merge / acceleration / serialization domain** (`NavBuild.cpp:547-end`): per-cell placement merge into world space, broad-phase acceleration build (per-polygon AABBs, CSR edge grid, CSR per-vertex adjacency), and the `NavData` stream Read/Write. Functions: public `BuildCellNavData`, `BuildNavAcceleration`, and the `NavData::Write` / `NavData::Read` members. This domain is the only user of `gpIslandTerrain`/`IslandTemplate` (`Frame/IslandTerrain.h`), `IslandPlacement` (`Frame/IslandChainPlacement.h`), the `kbDebugNavCrossingCheck` PCH constant, `kiNavZonesX`/`kiNavZonesY`/`NavGridCell` (`NavBuild.h`), and `common::Write`/`common::Read`.

**The single cross-domain dependency**: file-local `SegmentsIntersect` (`NavBuild.cpp:271-292`) is called by `SegmentIntersectsAnyEdge` (contour domain) AND by the `kbDebugNavCrossingCheck` diagnostic block inside `BuildCellNavData` (cell-merge domain). It is the only symbol that must cross the new TU boundary.

This is a pure code-organization refactor: **NO behavior change, NO algorithm change, NO serialized-layout change.** The acceleration layer is derived (not serialized), and the serialized `vertices`/`polygonOffsets`/`visEdgeA`/`visEdgeB` layout in `NavData::Write`/`Read` is untouched, so **`kiNavDataVersion` does NOT change** and there is **no CRC / save-compat / network-protocol impact**. The determinism contract is unaffected: the split only relocates function bodies; the same code runs in the same order on both client and server. (NavData acceleration is rebuilt deterministically on both sides; relocating `BuildNavAcceleration` to another TU does not change that.)

NavBuild is compiled into BOTH the client and the server executables (it is not client-gated), so the build wiring must update all four project files.

## Design

Mirror the recently-completed `DataPacker/Source/BakeIslandIntermediates.cpp` split (one TU keeps the public header + a domain; new TU(s) take the other domain(s); a private `*Internal.h` holds shared types/constants/forward-decls; file-static helpers move with the domain that calls them; the one cross-TU helper is promoted from the anonymous namespace to external linkage, forward-declared in the private header).

### Translation units after the split

1. **`NavBuild.cpp`** (kept) — owns the public header `NavBuild.h` and the **contour-build domain**. Retains `BuildNavContour` (the public entry) plus its file-local helpers: `EncodeEdgeKey`, `ExtractContourEdges`, `ChainEdgesIntoPolygons`, `PointInPolygon`, `SegmentIntersectsAnyEdge`, `MidpointInsideObstacle`, `BuildVisibilityGraph`, and the `ContourEdge` struct. Keeps the Clipper2 usage. Includes the new private header for the promoted `SegmentsIntersect` decl. (Rationale for keeping the contour domain in `NavBuild.cpp`: the file name reads as "build the nav contour", and `BuildNavContour` is the natural primary entry point — same convention as `BakeIslandIntermediates.cpp` keeping the top-level entry.)

2. **`NavCellData.cpp`** (new) — the **cell-merge / acceleration / serialization domain**: `BuildCellNavData`, `BuildNavAcceleration`, `NavData::Write`, `NavData::Read`. Includes `NavBuild.h` (for the `NavData`/`NavContour` public types, `kiNavZonesX`/`kiNavZonesY`/`NavGridCell`, and the `BuildCellNavData`/`BuildNavAcceleration` public decls), `Frame/IslandTerrain.h` (for `gpIslandTerrain`/`IslandTemplate`), `Frame/IslandChainPlacement.h` (for `IslandPlacement`), and the new private header (for `SegmentsIntersect`). `kbDebugNavCrossingCheck` and `common::Write`/`Read` reach it via the PCH. (Name `NavCellData.cpp` chosen to echo its primary public symbol `BuildCellNavData`; trivial choice, swap to `NavMerge.cpp` if preferred at execution time.)

3. **`NavBuildInternal.h`** (new, private) — NOT part of the public API. Holds only the one cross-TU forward declaration:
   - `bool SegmentsIntersect(XMFLOAT2 f2A1, XMFLOAT2 f2A2, XMFLOAT2 f2B1, XMFLOAT2 f2B2);` inside `namespace engine`.
   Its definition moves out of `NavBuild.cpp`'s anonymous namespace to **external linkage in `engine::`** so both TUs link to it. Place the definition in `NavBuild.cpp` (the contour domain is where its sibling geometry predicates `PointInPolygon`/`SegmentIntersectsAnyEdge` live, and that domain remains its heaviest caller). Add a one-line header comment matching the `BakeIslandIntermediatesInternal.h` convention (states it is the private cross-TU header for the two NavBuild TUs and lists what it holds).

   `ContourEdge` stays file-local to `NavBuild.cpp` (single-TU type — do NOT promote it to the private header). All other helpers are single-TU and stay file-local (in their respective anonymous namespaces).

### Include hygiene per TU

- `NavBuild.cpp`: `#include "NavBuild.h"`, `#include "NavBuildInternal.h"` (for the promoted decl it now defines), `#include "Frame/IslandChainPlacement.h"` and `#include "Frame/IslandTerrain.h"` are **removed** (no longer used by the contour domain — verify `gpIslandTerrain`/`IslandPlacement` have no remaining reference here after the move).
- `NavCellData.cpp`: `#include "NavBuild.h"`, `#include "NavBuildInternal.h"`, `#include "Frame/IslandTerrain.h"`, `#include "Frame/IslandChainPlacement.h"`.
- `NavBuildInternal.h`: `#pragma once`; no includes needed beyond the PCH-provided `XMFLOAT2` (DirectXMath types arrive via the PCH, same as in the existing headers — confirm at execution; if not, the forward-decl uses `XMFLOAT2` which is a PCH type).

### Build wiring (CLIENT + SERVER — the key difference from the DataPacker single-tool split)

`NavBuild.cpp`/`.h` are listed in both the client and server project files. Add `NavCellData.cpp` and `NavBuildInternal.h` to **all four**, next to the existing `NavBuild` entries, in the same Frame filter:

- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` — `NavBuild.cpp` at `:537` (`ClCompile`), `NavBuild.h` at `:354` (`ClInclude`).
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj.filters` — `NavBuild.cpp` at `:1064`, `NavBuild.h` at `:594` (record the Frame filter label these sit under).
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.vcxproj` — `NavBuild.cpp` at `:436` (`ClCompile`), `NavBuild.h` at `:333` (`ClInclude`).
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.vcxproj.filters` — `NavBuild.cpp` at `:668`, `NavBuild.h` at `:414`.

Both files are NOT client/server-gated (no `#if defined(BT_CLIENT/SERVER)` wrap), so neither new file needs gating and both must appear in BOTH vcxproj pairs (per [VisualStudio2026/CLAUDE.md](../../../Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/CLAUDE.md): a file wrapped in `BT_CLIENT`/`BT_SERVER` belongs only to the matching project; an ungated file belongs to both).

## Critical files

- `Engine/Source/Frame/NavBuild.cpp` — source of the split. Keeps the contour-build domain (`BuildNavContour` + `EncodeEdgeKey`/`ExtractContourEdges`/`ChainEdgesIntoPolygons`/`PointInPolygon`/`SegmentIntersectsAnyEdge`/`MidpointInsideObstacle`/`BuildVisibilityGraph` + `ContourEdge`); gains the promoted external-linkage definition of `SegmentsIntersect`; loses the cell-merge/accel/serialize functions; loses the `IslandTerrain.h`/`IslandChainPlacement.h` includes.
- `Engine/Source/Frame/NavCellData.cpp` (new) — receives `BuildCellNavData`, `BuildNavAcceleration`, `NavData::Write`, `NavData::Read`.
- `Engine/Source/Frame/NavBuildInternal.h` (new, private) — `engine::SegmentsIntersect` forward declaration only.
- `Engine/Source/Frame/NavBuild.h` — public header; **unchanged** (all public decls `BuildNavContour`/`BuildCellNavData`/`BuildNavAcceleration`/`NavData`/`NavContour`/`NavGridCell`/`kiNavZonesX`/`kiNavZonesY`/`kiNavDataVersion` stay here; the definitions of two of them simply move to the new TU). Verify no edit is needed.
- The four vcxproj/filters listed under **Build wiring** above.
- `Engine/Source/Frame/CLAUDE.md` — the **NavBuild / NavQuery** bullet describes NavBuild as one unit; update it (via the `update-claude-docs` skill in the code-change process) to note the contour-build vs cell-merge TU split and that `kiNavDataVersion` is unaffected.

## Out of scope

- **Any behavior, algorithm, or numeric change** — no marching-squares tweak, no Clipper2 retune, no acceleration-grid change, no serialization-format change. Byte-for-byte identical `NavData`.
- **`kiNavDataVersion` bump** — explicitly NOT bumped; the serialized layout is untouched. Bumping it here would be a bug.
- **Renaming the shared types** (`ContourEdge`, `NavContour`, `NavData`) or any public function. Keep all public names identical so `NavQuery.cpp` and `PlayersNavigation.cpp` are unaffected.
- **`Frame/NavBuildDebugCrossingCheckPerf.md`** (queued) — gates/accelerates the `BuildCellNavData` crossing loop and fixes the `NavBuild.cpp` LOG-float sites + a `ComputeNavigation` dead local. It edits the same functions this split relocates. Out of scope here; see File-Groups coordination below.
- **`Frame/EvaluateRecastDetourNavmesh.md`** (queued, read-only eval) — evaluates the `BuildCellNavData`/`NavQuery*` interfaces. No edits land from it, so it never conflicts; if it ever returns "go", its integration follow-up supersedes both this split and the crossing-check plan since Detour would own the builder.
- **`NavQuery.cpp`** — not touched; it consumes `NavData`/`NavGridCell` from the unchanged public header.
- **Splitting `NavQuery.cpp`** or any other Frame file.

## Acceptance criteria

- `NavBuild.cpp` and the new `NavCellData.cpp` each compile in both client and server builds; both executables link (the promoted `engine::SegmentsIntersect` resolves across the two TUs).
- `NavBuild.h` is unchanged (or changed only if a forward-decl gap is found at execution); all public symbols keep their names and signatures.
- `kiNavDataVersion` is unchanged (still 12).
- A diff of each relocated function body against the pre-split source shows ZERO logic changes (move-only).
- `NavCellData.cpp` and `NavBuildInternal.h` appear in all four vcxproj/filters pairs under the Frame filter.
- No new file is client/server-gated; both new files build into both executables.
- `Frame/CLAUDE.md` NavBuild bullet reflects the TU split.

## Coordination note

This plan and the two queued NavBuild plans (`NavBuildDebugCrossingCheckPerf.md`, `EvaluateRecastDetourNavmesh.md`) form the existing `Order.md` File-Group **"`Engine/Source/Frame/NavBuild.{h,cpp}` / `NavQuery.cpp` + `Projects/.../Players/PlayersNavigation.cpp`"**. Add this plan to that group. Because this split relocates `BuildCellNavData` (and its crossing-check block) into `NavCellData.cpp`, **landing this split FIRST changes which file the crossing-check plan must edit** — execute the split before `NavBuildDebugCrossingCheckPerf.md`, or have that plan re-target `NavCellData.cpp` at execution time. `EvaluateRecastDetourNavmesh.md` lands no edits and is order-independent.
