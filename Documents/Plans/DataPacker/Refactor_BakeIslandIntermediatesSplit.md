# Refactor: Split BakeIslandIntermediates.cpp

Source: post-audit follow-up from the beach-band mesh subdivision change.

## Context

`DataPacker/Source/BakeIslandIntermediates.cpp` is over the 1000-line `/reduce-file` threshold (≈ 1266 lines after the constant-beach-height landing). The growth came from layering an adaptive beach-band subdivision pass (~300 lines) on top of an already-large `BakeOne` function that orchestrates the entire per-island bake: archetype patch + Gaea invocation + elevation conversion + auto-crop + AO crop + mesh parse + subdivision + sentinel files.

`BakeOne` is the single biggest cohesion liability in the TU. Each phase is independently testable in concept (elevation pipeline, AO crop, mesh subdivision) but currently shares only a few locals (`dimensions`, `iCropX..iCropHeight`, the mesh buffers). The mesh subdivision pass in particular is self-contained: its only inputs are the two mesh buffers, the band half-width, and the edge/depth caps, and its only side effect is mutating those buffers and incrementing a depth-cap counter.

## Background

- `DataPacker/Source/BakeIslandIntermediates.cpp` — TU at 1221 lines after the beach-band subdivision landed. `BakeOne` (≈ line 321 → ≈ line 1170) holds nearly the whole bake pipeline.
- Beach subdivision block: from the indices-loaded point (≈ line 770) through the final `LOG` (≈ line 1145), enclosed in a single `{}` scope. 12 lambda helpers (`EdgeKey`, `VertexX`, `VertexY`, `VertexZ`, `EdgeAdd`, `EdgeRemove`, `EdgeOther`, `IsBandEligible`, `LongestEdgeXY`, `AddVertex`, `GetOrCreateMidpoint`, `LookupMidpoint`, `AppendTriangle`, `KillTriangle`) all `[&]`-capturing the same shared mutable state (`meshPositions`, `meshIndices`, `triangleAlive`, `triangleDepth`, `edgeMidpoints`, `edgeTriangles`, `iDepthCapHits`). Captured-by-reference lambdas in a single scope are exactly what a struct does for free.
- `DataPacker/Source/CLAUDE.md` Architecture paragraph describes the bake pipeline at a high level; updates here flow naturally if the TU is split.
- File-size precedent: `Frame/Refactor_NavBuildSplit.md` (`Order.md` row 13) splits 1262-line `NavBuild.cpp` into contour / polygon / visibility TUs. Same shape of fix.

## Proposed Approach

1. **Extract the beach subdivision pass to its own TU.** New files:
   - `DataPacker/Source/SubdivideBeachBand.h` — declares one free function:
     ```
     void SubdivideBeachBand(std::vector<float>& rMeshPositions,
                             std::vector<uint32_t>& rMeshIndices,
                             float fBandMinMeters,
                             float fBandMaxMeters,
                             float fMaxEdgeMeters,
                             int32_t iMaxDepth,
                             int64_t& riDepthCapHits);
     ```
   - `DataPacker/Source/SubdivideBeachBand.cpp` — holds the lambda helpers as private member functions of a TU-local `BeachSubdivider` struct (captures-by-reference become member references; `Run()` drives the worklist). The four subdivision-pass constants (`kfBeachSubdivisionMinMeters`, `kfBeachSubdivisionMaxMeters`, `kfBeachSubdivisionMaxEdgeMeters`, `kiBeachSubdivisionMaxDepth`) stay in the caller TU next to `kfBeachHeightMeters`. The band straddles beach Z=0 in absolute engine-meters, independent of `elevationMeters`.
   - `BakeIslandIntermediates.cpp` includes the new header and replaces the inline block with a single call.
2. **Optional follow-on extractions** (defer to a separate plan if scope balloons): the archetype patch+bake+restore RAII (lines ≈ 445–500) and the elevation/AO crop pass (lines ≈ 500–670) are also natural cohesion boundaries. Leaving them alone keeps this plan narrow; revisit if the file still exceeds 1000 lines after the subdivision extraction (it should drop to ≈ 920 lines, which is under threshold).
3. **Build wiring.** Add the two new files to `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj` and `DataPacker.vcxproj.filters` under the `Source` filter. No PCH changes (the new TU uses the same standard-library headers already in `Common/ExternalHeaders.h`).
4. **CLAUDE.md update.** The `DataPacker/Source/CLAUDE.md` paragraph that now mentions "adaptive beach-band densification" need only stay accurate; the implementation location moves but the behavior description stays.

Name interfaces, not just paths: the call site is the inline-`{}` block immediately after the index-loading switch in `BakeOne`. The new free function is `SubdivideBeachBand`. The TU-local helper is `BeachSubdivider`.

## Out of scope

- Splitting the elevation / AO / archetype-lock blocks of `BakeOne` (separate plan if needed; estimate post-subdivision-extraction).
- Promoting subdivision into an `ExportJob` — it is not an asset-type processor, it is a stage of the island bake.
- Adding tests for the subdivision pass — project rule forbids unit tests.
- Adjusting the subdivision algorithm itself (depth cap semantics, T-junction handling) — those are tracked in the file's existing comment block and the warning log added in the parent session.

## Acceptance criteria

- `BakeIslandIntermediates.cpp` drops below 1000 lines.
- `MeshProcessed.bin` is byte-identical before and after the extraction for the standard archetype (`Engine/Data/Islands/02`) at fixed seed — the extraction is pure code motion, no algorithm change.
- `kiBakeVersion` is NOT bumped (no behavior change).
- `DataPacker/Source/CLAUDE.md` still accurately describes the bake pipeline.
- New TU appears in both `.vcxproj` and `.vcxproj.filters` under the same `Source` filter as `BakeIslandIntermediates.cpp`.

## Notes

The captured-by-reference lambda cluster is the strongest signal that this block wants to be a struct: every lambda accesses the same six-field shared state through `[&]`, and the only reason they live as lambdas today is that they were authored inline. Extracting to a struct makes the dependency graph explicit (members vs locals) and frees future evolution (alternative split rules, alternative neighbor traversal, optional reservation hints) from a 300-line single-scope.
