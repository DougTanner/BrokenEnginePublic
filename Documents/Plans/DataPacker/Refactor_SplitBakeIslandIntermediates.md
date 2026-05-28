# Refactor: Split BakeIslandIntermediates.cpp

## Context

`DataPacker/Source/BakeIslandIntermediates.cpp` is currently 1133 lines, past the 1000-line `/reduce-file` soft threshold. The file accreted gradually and a recent long-params refactor (`Refactor_LongParamsAndFlags`) added ~50 net lines (struct definitions + local ref bindings) that pushed it firmly over.

The TU has three natural domains, all currently mingled:

1. **Per-region split logic** — `ProcessBakedRegion` (~230 lines) crops one bake region out of the full Gaea output, downsamples elevation, writes per-leaf files (`Elevation.r32`, `AmbientOcclusion.r16`, `MeshProcessed.bin`, `BakedDimensions.json`), and runs the leaf-acceptance height check.

2. **Per-route Gaea orchestration** — `BakeRoute` (~310 lines) handles the Stage-1/Stage-2 dirty checks, archetype copy + patch, `Gaea.Swarm.exe` invocation, raw-output validation, post-bake elevation conversion, mesh load from `Mesh.gltf`, and the per-region loop dispatching to `ProcessBakedRegion`.

3. **Per-island and orchestration top-level** — `BakeOne`, `BakeIslandIntermediates`, `ReadBakedDimensions`, JSON parsing, route-list resolution, stale-folder pruning, the `IsGaeaRawDirty` / `AreLeavesDirty` dirty-check helpers, and a handful of constants/structs.

The three domains share the new structs (`IslandBakeContext`, `RegionBounds`, `LeafTarget`, `BakeOutput`) but have minimal direct coupling otherwise — each domain only calls into the next via the struct interface.

## Design

Split into three TUs sharing a private header:

- **`BakeIslandIntermediates.cpp`** (top-level only, target ~350 lines) — `BakeIslandIntermediates`, `BakeOne`, `ReadBakedDimensions`, JSON parsing, route resolution, stale-folder pruning. Retains the public header `BakeIslandIntermediates.h`.

- **`BakeRoute.cpp`** (new, target ~350 lines) — `BakeRoute` and its helpers (`PatchArchetype` if local-only, `IsGaeaRawDirty`, `AreLeavesDirty`). Calls `ProcessBakedRegion` via the new shared private header.

- **`ProcessBakedRegion.cpp`** (new, target ~300 lines) — `ProcessBakedRegion` only. Pure split/crop/leaf-write logic.

- **`BakeIslandIntermediatesInternal.h`** (new private header, not in the public API) — holds the shared structs (`IslandBakeContext`, `RegionBounds`, `LeafTarget`, `BakeOutput`), shared constants (`kpcIntermediateFiles`, `kpcBakeVersionFile`, `kpcSplitVersionFile`, `kiBakeVersion`, `kiSplitVersion`, `kfBeachSubdivision*`, `kfMinIslandMaxHeightMeters`, `kfCropEpsilonAboveSeaFloorMeters`, `kpcPatchedArchetypeFile`, `kpcRequiredIslandJsonKeys`), and forward declarations for the cross-TU function calls (`bool ProcessBakedRegion(...)`, `void BakeRoute(...)`).

## Critical files

- `DataPacker/Source/BakeIslandIntermediates.{h,cpp}` — public header unchanged; cpp reduced to orchestration only.
- `DataPacker/Source/BakeRoute.cpp` — new TU for one route's Gaea bake + split orchestration.
- `DataPacker/Source/ProcessBakedRegion.cpp` — new TU for the region split / crop logic.
- `DataPacker/Source/BakeIslandIntermediatesInternal.h` — new private header for shared struct + constant types.
- `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj` and `.filters` — add the two new `.cpp` files and the new header.

## Out of scope

- Renaming `WorldDimensions`, `BakedDimensions`, or `RouteSubdivision`. They stay where they are.
- Touching the public `BakeIslandIntermediates()` entry point's signature or call site in `Main.cpp`.
- Any change to `GaeaArchetype.{h,cpp}` — the Gaea-version-specific boundary stays.
- Any change to `SubdivideBeachBand.{h,cpp}` — already split.
- Any change to the bake protocol (BakeVersion / SplitVersion / file layout). This is a pure code-organization refactor; existing bakes must remain valid post-refactor.

## Acceptance criteria

- All three resulting `.cpp` files compile in DataPacker Release.
- A clean run of `DataPacker.exe` against an existing island re-bake produces byte-identical outputs (or at minimum: identical `BakeVersion.txt` / `SplitVersion.txt`, identical `Elevation.r32` / `AmbientOcclusion.r16` / `Color.png` / `Normals.exr` / `MeshProcessed.bin` / `BakedDimensions.json` for at least one route).
- A clean run against a previously-baked island with all sentinels present completes without re-running Gaea (both stages skipped via dirty checks).
- `BakeIslandIntermediates.cpp` ends up under 500 lines; the two new TUs each stay under 500 lines.

## Notes

- The shared struct definitions (`IslandBakeContext` etc.) need to move to the private header. Today they live in the anonymous namespace of `BakeIslandIntermediates.cpp`; the private header should expose them at TU scope (still hidden from the public header).
- `IsGaeaRawDirty` / `AreLeavesDirty` are currently file-static in the anon namespace; they're only called from `BakeRoute`, so they move with it into `BakeRoute.cpp`.
- The local-ref binding pattern at the top of `BakeRoute` and `ProcessBakedRegion` (introduced by `Refactor_LongParamsAndFlags`) should stay — it preserves the existing function-body readability after the long-params context-struct migration.
