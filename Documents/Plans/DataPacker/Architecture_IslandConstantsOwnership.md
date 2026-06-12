# Architecture: Island Bake Constants Ownership

## Context
Source: /external-architecture-review on `DataPacker/Source/ExportJobs` (recursive). The `ExportJobs/` ↔ `ExportJobs/Island/` folders are near-circularly coupled: the bake TUs include upward (`BakeIslandIntermediatesInternal.h:11`, `BakeIslandIntermediates.cpp:6` → `ExportJobs/ExportIsland.h`) solely for two constants, incidentally pulling `ExportJob.h`'s class into all three bake TUs. Separately, intermediate-file name literals are duplicated across three TUs with no shared constants — the shared-constant pattern already exists (`kpcBakedDimensionsFile`, `kpcIslandIntermediatesDir`) but is incomplete.

## Design

### Move shared constants down to the bake layer
- Move `kiElevationDivisor` and `kpcIslandIntermediatesDir` from `ExportIsland.h` (line 8 area) into `Island/BakeIslandIntermediates.h` (beside `kpcBakedDimensionsFile`). The bake TUs are the producers; `ExportIsland.cpp` already includes `BakeIslandIntermediates.h` (line 3), so the consumer keeps access with no new edge. Then drop the `ExportJobs/ExportIsland.h` includes from `BakeIslandIntermediatesInternal.h:11` and `BakeIslandIntermediates.cpp:6` — this removes the upward folder edge and stops pulling `ExportJob.h` into the three bake TUs [~30m]

### Name the duplicated intermediate-file literals
- Add constants beside `kpcBakedDimensionsFile` for `"Elevation.r32"`, `"AmbientOcclusion.r16"`, `"MeshProcessed.bin"`, currently duplicated as raw literals at `BakeRoute.cpp:40-46, 164-167, 321, 339`, `ProcessBakedRegion.cpp:139, 154, 239`, `ExportIsland.cpp:156, 211, 318` [~15m]

### BakedDimensions.json key constants
- The JSON key strings are duplicated between writer (`ProcessBakedRegion.cpp:253-262`) and reader (`ReadBakedDimensions` in `BakeIslandIntermediates.cpp:236-244`). Hoist the key names into shared constants in `BakeIslandIntermediatesInternal.h` (both TUs already include it) [~15m]

### Disambiguate the three island version axes
- `ExportIsland::Version(28)` (`ExportIsland.h:32`) numerically coincides with `kiBakeVersion = 28` (`BakeRoute.cpp:65`) while being independent axes (chunk cache vs bake sentinel vs split sentinel). Add a one-line comment at each declaring independence so a future bump doesn't try to keep them in lockstep [~5m]

## Critical files
- `DataPacker/Source/ExportJobs/ExportIsland.h`, `ExportIsland.cpp`
- `DataPacker/Source/ExportJobs/Island/BakeIslandIntermediates.h`, `BakeIslandIntermediates.cpp`, `BakeIslandIntermediatesInternal.h`, `BakeRoute.cpp`, `ProcessBakedRegion.cpp`

## Out of scope
- Merging or restructuring the bake TU split itself — the four-TU layout is documented intentional (Gaea 3 migration seam, `DataPacker/Source/CLAUDE.md`).
- Any change to the on-disk intermediate formats or `BakedDimensions.json` schema — names only move into constants; bytes identical.
- `kfSeaBottomMeters`/`kfUnderwaterMaskThresholdMeters` — already correctly single-sourced in `Common/DataFile.h`.

## Notes
- No determinism/CRC/pack-layout exposure — output bytes are unchanged; this only renames compile-time constant ownership.
- One trivial choice (constants in `BakeIslandIntermediates.h` vs `Internal.h`): public reader-visible names (`kiElevationDivisor`, file names consumed by `ExportIsland.cpp`) go in `BakeIslandIntermediates.h`; JSON keys used only by the two bake TUs go in `Internal.h`.

## Verification Notes
- Verified — no invalid items. The upward includes (`BakeIslandIntermediatesInternal.h:11`, `BakeIslandIntermediates.cpp:6`) confirmed used solely for the two constants (`kiElevationDivisor` at `Internal.h:49` / `BakeIslandIntermediates.cpp:115`; `kpcIslandIntermediatesDir` at `BakeIslandIntermediates.cpp:229` and throughout `BakeRoute.cpp`); no bake TU references the `ExportIsland` class itself. After the move, `BakeRoute.cpp`/`ProcessBakedRegion.cpp` keep access via `Internal.h:10` → `BakeIslandIntermediates.h`, and `ExportIsland.cpp:3` already includes `BakeIslandIntermediates.h`.
- Literal citations all exact: `BakeRoute.cpp:40-46` (`kpcIntermediateFiles`), :164-167, :321, :339; `ProcessBakedRegion.cpp:139, :154, :239`; `ExportIsland.cpp:156, :211, :318`. JSON keys: writer `ProcessBakedRegion.cpp:254-262`, reader `BakeIslandIntermediates.cpp:236-244` (`ReadBakedDimensions` spans :227-246).
- Version-axis coincidence confirmed: `ExportIsland::Version(28)` (`ExportIsland.h:32`) vs `kiBakeVersion = 28` (`BakeRoute.cpp:65`); `kiSplitVersion = 4` (`BakeRoute.cpp:66`) is the third axis — the independence comments should cover all three.
- Note for execution: `kpcIntermediateFiles` (`BakeRoute.cpp:38-46`) is the route-level *Gaea output* list (includes `Color.png`/`Normals.exr`/`Mesh.gltf`/`Mesh.bin`); the leaf-level literals are the per-chunk derived files. The shared constants should name the three leaf-file strings; folding the route array's entries through them is optional (same strings, different role).
