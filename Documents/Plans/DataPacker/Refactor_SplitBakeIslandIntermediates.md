# Split BakeIslandIntermediates.cpp and remove dead GetIslandDimensions

## Context

`DataPacker/Source/BakeIslandIntermediates.cpp` crossed ~1150 lines after the multi-route island
work added `ProcessBakedRegion`, `BakeRoute`, `LookupRouteSubdivision`, `PatchArchetypeRoute`, and
the `RouteSubdivision` table — past the `/reduce-file` ~1000-line threshold. Much of the length is
heavy explanatory comment blocks rather than code, so confirm the *code-line* count before
committing to a split; if code lines are comfortably under threshold, this plan may be a no-op.

Separately, `GetIslandDimensions` (declared `BakeIslandIntermediates.h`, defined in the `.cpp`) has
**no callers** anywhere in the repo and was already dead before this session — incidental dead code
to remove while the file is open.

## Design

If the split is warranted, separate along the existing seam the code already falls into:

1. **Gaea-process / archetype-patch helpers** → a new TU (e.g. `BakeArchetypePatch.{h,cpp}` or
   `GaeaArchetype.{h,cpp}`): `ResolveGaeaExecutable`, `ResolveTerrain`, `WriteFileBytes`,
   `PatchArchetypeSeeds`, `PatchArchetypeMesherResolution`, `PatchArchetypeRoute`,
   `ReadArchetypeSeaLevel`, `PatchArchetype`.
2. **Bake / region pipeline** stays in `BakeIslandIntermediates.cpp`: the `RouteSubdivision` table +
   `LookupRouteSubdivision`, `IsRouteBakeDirty`, `ProcessBakedRegion`, `BakeRoute`, `BakeOne`,
   `BakeIslandIntermediates`, `ReadBakedDimensions`.

Shared file-scope constants (`kpcIntermediateFiles`, `kiBakeVersion`, `kfGaeaSeaLevelDefault`,
beach-subdivision constants, `kiCropAlignment`, `WorldDimensions`) move to whichever side uses them,
or a small shared header if both need them.

Remove `GetIslandDimensions` (decl + def) regardless of whether the split lands.

## Critical files

- `DataPacker/Source/BakeIslandIntermediates.cpp` / `.h` — the split source + dead-function removal.
- New `.cpp`/`.h` pair (if split) — add to `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj`
  and `.vcxproj.filters`.
- Confirm no other TU includes anything that moves (`ExportJobs/ExportIsland.cpp` includes
  `BakeIslandIntermediates.h` for `ReadBakedDimensions`/`BakedDimensions`/`WorldDimensions` — keep
  those reachable from that header).

## Out of scope

- Any behavioral change to the bake, split, crop, or export logic — pure file reorganization + dead
  code removal.
- The multi-route feature itself (already landed).
- Trimming the explanatory comment blocks (they are load-bearing Gaea-2-migration context).

## Acceptance criteria

- `BakeIslandIntermediates.cpp` (and any new TU) each under the `/reduce-file` threshold, OR a noted
  decision that code-line count is already under and no split is needed.
- `GetIslandDimensions` gone; no dangling references.
- DataPacker (Release) builds clean; a re-bake produces byte-identical island chunks vs. pre-split.

## Notes

Found by the step-9 audit / code-review of the multi-route island change. Mechanical, compile-checked
refactor; the only risk is `.vcxproj` wiring for the new TU. Verify the actual code-line count first —
the file is comment-dense and may not truly need splitting.
