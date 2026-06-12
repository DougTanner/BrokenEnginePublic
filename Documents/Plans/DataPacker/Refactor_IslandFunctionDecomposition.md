# Refactor: Island Bake Function Decomposition

## Context
Source: /external-refactor-clean on `DataPacker/Source/ExportJobs` (recursive). The three largest functions in the island bake path exceed ~100 lines each (largest: 337); each splits along boundaries the existing block comments already name.

## Design

### `BakeRoute` (`Island/BakeRoute.cpp:185-521`, 337 lines)
- Extract anonymous-namespace helpers along the commented stages: `RunGaeaExport` (:213-293), `LoadElevationMeters` (:321-336), `LoadAmbientOcclusion` (:338-350), `LoadMesherMesh` (:361-475), leaving `BakeRoute` as the staged orchestration plus the region-split loop (:485-510) [~1h]
- Drop the 9-member local-ref unpack of `IslandBakeContext` (:187-195) — use `rContext.x` directly once split [~10m]

### `ExportIslandData` (`ExportIsland.cpp:133-333`, 200 lines)
- The four texture-encode blocks (:209-217, :219-230, :232-242, :248-307) share one shape (load → [crop] → `MaskByHeightmap` → `MakeMipmaps` → `Save` → `SaveJpegSidecar`) differing only in source/format/flat-value — extract a per-texture helper (or table-drive it), removing ~60 lines and the repeated 5-argument `MaskByHeightmap(...)` prefix [~45m]
- Extract the hull CCW/convexity verification block (:173-199) as `VerifyHullCcwConvex` [~15m]

### `BeachSubdivider::Run` (`Island/SubdivideBeachBand.cpp:197-380`, 183 lines)
- Extract the in-band 1→4 split (:257-304) and the absorption 1→2/1→3/1→4 switch (:306-364) as member functions [~30m]

## Critical files
- `DataPacker/Source/ExportJobs/Island/BakeRoute.cpp`
- `DataPacker/Source/ExportJobs/ExportIsland.cpp`
- `DataPacker/Source/ExportJobs/Island/SubdivideBeachBand.{h,cpp}`

## Out of scope
- `BakeOne` (`BakeIslandIntermediates.cpp:90-223`, 133 lines) and `MigrateLegacyIntermediate` (114 lines) — borderline; split only if those files are touched anyway.
- The bake TU layout, sentinels, or any on-disk format — pure intra-file extraction; bake output bytes identical.
- Filename-literal constants — `Architecture_IslandConstantsOwnership.md`.

## Notes
- No determinism/pack-byte exposure — mechanical extraction; identical computation order preserved (the encode blocks remain serialized behind `Texture::sEncodeMutex` exactly as today).
- File-group overlap with `Architecture_IslandConstantsOwnership.md` (same TUs) and `Refactor_ExportJobsQuickWins.md` (SubdivideBeachBand ctor) — co-schedule.

## Verification Notes
- All function spans and extraction-seam line citations verified exact: `BakeRoute` :185-521 (337 lines), unpack :187-195 (9 locals), stage blocks :213-293 / :321-336 / :338-350 / :361-475, split loop :485-510; `ExportIslandData` :133-333 (200 lines), encode blocks :209-217 / :219-230 / :232-242 / :248-307, hull verification :173-199; `BeachSubdivider::Run` :197-380 (183 lines), in-band split :257-304, absorption switch :306-364.
- Caveat on the per-texture-helper item: the fourth encode block (masks, :248-307) deviates substantially from the other three — it packs four grayscale PNGs into RGBA (:251-292), runs `Downsize(2)` (:298), and masks with divisor 1 instead of `kiElevationDivisor` (:302). The shared shape covers AO/Color/Normals cleanly; the masks block should keep its custom load/pack body and share only the tail (`MaskByHeightmap → MakeMipmaps → Save → SaveJpegSidecar`). Budget the "~60 lines removed" accordingly.
- The `LoadMesherMesh` extraction (:361-475) includes the `SubdivideBeachBand` invocation and its depth-cap warning — fine to extract together, but it needs `routeDir` for the LOG strings in addition to the mesh buffers and `fBeachOffsetMeters`.
