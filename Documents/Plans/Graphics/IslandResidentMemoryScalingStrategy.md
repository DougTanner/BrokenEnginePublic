# Island Resident-Memory Scaling — Strategy Pick

## Context

Predecessor `IslandResidentMemoryScaling.md` landed step 1 only: boot-time `kDebug` LOG
instrumentation at the end of `engine::IslandTerrain::CreateClientMeshBuffers` that reports
per-template and aggregate bytes for mesh CPU, mesh GPU, heightmap, and valid-area hull. The
`[DEBUG-resmem]` tag is grep-removable when this strategy plan lands.

Premise unchanged: with island 01's multi-island route table (`1x1`..`4x4`), template count
rose ~9x to ~65, and per-template mesh + heightmap escape the texture LRU. `kiMaxIslands` was
bumped 64 -> 128 as a stopgap. The texture LRU manages `color`/`normals`/`AO`/`masks`/`elevation`
GPU images; mesh GPU buffers and the in-memory chunk payload (heightmap + hull, sliced from the
`kIsland` chunk loaded eagerly by `FileManager`) stay permanently resident.

This plan picks and implements a strategy once boot-log measurements from a real run are in hand.

## Design

Read the per-template + aggregate numbers from a Debug client boot log (the `[DEBUG-resmem]` lines
emitted by `CreateClientMeshBuffers`). Then pick:

- **(a2) Extend the texture LRU to per-template meshes.** Free `mMeshBuffer` in `EvictionSweep`
  alongside the existing texture eviction; zero the corresponding
  `Islands::mIndirectBuffers[i]` entry (`instanceCount = 0` / `indexCount = 0`) so the per-template
  draw becomes a no-op without touching the record-once CB. `RestorationSweep` re-uploads the mesh
  from the still-resident chunk-payload pointers (`mpfMeshPositions` / `mpuiMeshIndices`) and
  rewrites the indirect entry. Mirrors how `mElevationTexture` already re-creates from the resident
  heightmap on first-mint after eviction.
- **(b) Lazy-load the `kIsland` chunk.** Switch `FileManager` to load the chunk on first
  subscription (mirroring the texture-chunk pipeline) and release on eviction. This complements (a2)
  by also evicting the heightmap + hull + CPU mesh data, not just GPU. Higher payoff, larger blast
  radius (changes `FileManager` lifecycle).
- **(c) Cap / tier the total template count** by trimming the route table in
  `Engine/Data/Islands/01/Island.json` and the bake pipeline in
  `DataPacker/Source/BakeIslandIntermediates.cpp`. Lowest risk; doesn't fix scaling, only delays it.
- **(d) Compress / share** resident data: quantize mesh positions to int16, pack indices to uint16
  where vertex count permits, share heightmaps across routes that crop the same source. Incremental,
  lower payoff.
- **(e) Close the plan.** If the measurements show the 65-template footprint is acceptable for the
  working memory budget, document the rationale and stop — no code change beyond removing the
  `[DEBUG-resmem]` LOG block.

Likely order of preference: (e) if numbers are small → (a2) if mesh GPU dominates → (a2)+(b) if
chunk payload also dominates → (c) as a stopgap if (a2)/(b) effort is too large for the residency
win.

## Critical files

- `Engine/Source/Frame/IslandTerrain.cpp` / `.h` — `IslandTemplate`, `CreateClientMeshBuffers`,
  `ReleaseGpuResources`, `EvictionSweep`, `RestorationSweep`. Remove the `[DEBUG-resmem]` LOG block
  in `CreateClientMeshBuffers` once the strategy lands.
- `Engine/Source/Graphics/Islands.cpp` / `.h` — `miTemplateCount`, `mIndirectBuffers`,
  `mIslandsStorageBuffers`. Strategy (a2) mutates indirect-cmd entries to make evicted draws no-ops.
- `Engine/Source/File/FileManager.cpp` — eager `kIsland` chunk load (line 197 maps `kIsland` ->
  `kDataTypeIslands`). Strategy (b) seam.
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — `kiMaxIslands` (currently 128). Re-evaluate as a
  residency budget after strategy lands.
- `Engine/Data/Islands/01/Island.json` + `DataPacker/Source/BakeIslandIntermediates.cpp` — route
  table (strategy (c) seam).
- `Engine/Source/Frame/CLAUDE.md` — module-level residency-model paragraph; update if strategy
  (a2) or (b) changes mesh/heightmap lifetime.

## Out of scope

- The `kiMaxIslands` cap value itself (handled as a follow-up at step 3 of the original plan).
- The NavContour per-template residency — owned by sibling plan
  `IslandNavContourResidency.md`. Coordinate strategy if (a2)/(b) lands so NavContour can adopt the
  same lifecycle.
- Any DataPacker bake correctness work; only the route-table seam under strategy (c) is in scope.
- Adding new routes or islands.

## Acceptance criteria

- A strategy is picked and recorded in this plan (or the plan is closed via (e) with the
  measurement values cited).
- If a code-change strategy lands: the resident set is bounded by *concurrent residency* rather
  than *total template count*, AND the record-once CB invariant is preserved (or any deviation is
  explicitly designed and documented in `Engine/Source/Frame/CLAUDE.md`).
- The `[DEBUG-resmem]` LOG block in `CreateClientMeshBuffers` is removed (its data has been
  captured and used to drive the decision).
- `Engine/Source/Frame/CLAUDE.md` is updated if mesh / heightmap lifetime changes.

## Notes

- This plan is gated on real boot-log data. Do not implement before the `[DEBUG-resmem]` lines
  have been captured from a Debug client run with the current route table.
- File-group co-location: this plan touches `Engine/Source/Frame/IslandTerrain.cpp` alongside
  several other plans listed in `Order.md`'s File Groups section. Coordinate sessions per that
  section if more than one of them lands at once.
- Strategy (a2) requires the per-template indirect-cmd entries to be writable in `Islands`; verify
  `mIndirectBuffers` is mapped (not device-only) before committing to that approach.
