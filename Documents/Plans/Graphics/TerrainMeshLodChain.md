# Terrain Mesh LOD Chain (per-template, indirect-selected)

## Context

`kGpuTimerTerrain` (swapchain Image pass) measures **998 µs current / 987 avg / 1027 max** at 120 fps in a Profile build (~6.6 ms total GPU/frame) — the third-biggest GPU cost after Water (2329 µs) and the rest of the Image pass. Caveat: measured on an **idle scene near the spawn islands**; combat scenes shift the mix but the terrain pass cost is camera/scene-static, so this number is representative for terrain.

The dominant cost driver is mesh density with no LOD. Each island template draws its full Gaea2-Mesher bake (`VerticesPerSide` 1024 in the archetype, plus `SubdivideBeachBand` shoreline densification) at **every** zoom level: ~119.5 MiB of GPU mesh across 70 templates (`IslandResidentMemoryScaling_Overview.md` measurement; the top ~3 islands are ~25 MiB ≈ ~1.5M triangles each). At gameplay camera heights (kilometers above the ocean) an island covers a few hundred pixels but submits hundreds of thousands of triangles — sub-pixel triangles whose cost shows up three ways: vertex shading (each `Terrain.vert` invocation does **two** `textureLod` fetches — composite elevation + own-heightmap sink), raster setup, and fragment quad overshading (a triangle covering ~1 pixel still shades a full 2x2 quad, so fragment invocations scale with triangle count, not screen pixels — industry-standard finding, the reason LOD is the canonical fix).

The engine already has the exact mechanism, for water only: `BufferManager::mWaterMeshLods` (`kiVisibleAreaLodCount = 4`) concatenates per-LOD index regions into one index buffer and selects `iIndexOffset`/`iIndexCount` per frame through host-visible indirect commands — "no command-buffer re-record on LOD change" (`BufferManager.h:96-99`, which explicitly notes terrain does *not* do this). The terrain indirect path is equally ready: `Islands::UpdateActiveIslands` already rewrites the per-template `VkDrawIndexedIndirectCommand` every frame (today only `instanceCount`); `indexCount`/`firstIndex` are just two more fields in the same mapped write. The record-once CB is untouched — it binds each template's whole `mMeshBuffer` and issues `vkCmdDrawIndexedIndirect`, exactly as now.

DataPacker already links meshoptimizer (`ProcessBakedRegion.cpp` uses `meshopt_optimizeVertexFetch`); `meshopt_simplify` generates coarser index buffers **over the existing vertex buffer**, which is precisely the concat-index-region layout needed.

## Design

**DataPacker (bake-time):**

- In `ProcessBakedRegion.cpp` (after the existing crop + before `meshopt_optimizeVertexFetch`), generate `kiVisibleAreaLodCount` (4) index sets: LOD0 = the current full index buffer; LOD1..3 via `meshopt_simplify` with target index count ÷4 per level and a per-LOD absolute error bound in meters (protects the `SubdivideBeachBand` shoreline band at near LODs; grill: error-bound values). Concatenate **coarsest-first** (meshoptimizer's recommended assembly — coarse LODs then occupy a small vertex range), then run `meshopt_optimizeVertexFetch` once over the concatenated buffer.
- `IslandHeader` (`Common/DataFile.h`) gains per-LOD index offset/count (e.g. `int32_t piLodIndexCounts[4]` + implied offsets, or offset/count pairs); `iMeshIndexCount` becomes the concat total so `CreateClientMeshBuffers`' size math and the `[indices][positions]` payload layout keep working unchanged. Update the `sizeof(IslandHeader) == 72` static_assert and its message per the established pattern.
- Index memory grows ~+33% of the index region (sum of ÷4 chain), ~+10-15 MiB GPU/CPU total — acceptable against the 119.5 MiB base; note it in `IslandResidentMemoryScaling_Overview.md` numbers when landing.

**Engine (per-frame):**

- `IslandTemplate` (`Engine/Source/Frame/IslandTerrain.h`) carries the per-LOD offsets/counts read from the header in the `IslandTerrain` ctor.
- `Islands::UpdateActiveIslands` (`Engine/Source/Graphics/Islands.cpp`) selects the frame's LOD once from the camera's already-hysteresis-latched zoom bucket — `gpGraphics->mpCamera->miVisibleAreaLod` (`CameraBase.h`), the same latch that drives water mesh LOD and snap coarseness, so terrain/water tessellation move in lockstep and snap-grid stability is inherited (no per-frame `quadSize` interaction; the mesh is world-anchored per island). Clamp with a runtime bias slider (below), then write `indexCount`/`firstIndex` alongside the existing `instanceCount` rewrite. The boot bake in the `Islands` ctor initializes them to LOD0.
- The elevation/shadow prepasses are unaffected (they draw SSBO quads via `QuadsAxisAlignedVisibleArea.vert`, not the mesh), so the composite elevation the water blend / shadows / `Terrain.frag` consume is **identical at every LOD** — `Terrain.vert` re-derives Z from that composite per vertex, which is what keeps LOD switches visually pinned.

**Runtime slider (mandatory tradeoff control):** new `gTerrainMeshLodBias` Wrapper in `TerrainWrappersBase.{h,cpp}` + Terrain Tweaks section (`TweaksSliderMap` pattern), integer-snapped, range −3..+3, default 0; added to `miVisibleAreaLod` and clamped to [0, 3]. −3 forces full density everywhere (exact current behavior = the fallback); +N trades fidelity for µs and doubles as the direct A/B measurement knob against `kGpuTimerTerrain`.

**Visual impact: (b) minor/imperceptible at gameplay camera heights.** Vertex Z always re-derives from the same composite elevation sampler and island UVs derive from vertex XY, so textures, shoreline blend (water reads the same elevation RTT), and shadows do not move; simplification error is XY-tessellation-only and sub-pixel at the heights where coarse LODs engage. LOD transitions ride the existing 5%-hysteresis zoom bucket (no oscillation); a one-frame retriangulation shimmer at the switch is the worst case. At `miVisibleAreaLod = 0` (lowest zoom bucket) LOD0 = today's mesh — zoomed-in fidelity is bit-identical.

**Expected saving:** the vertex/raster/overshading share of the 998 µs — estimated 300-600 µs at typical gameplay heights (triangle count ÷4..÷64 where the pass is geometry-bound), directly measurable via the bias slider before/after. Idle-scene caveat applies; the win grows when zoomed out.

## Critical files

- `DataPacker/Source/ExportJobs/Island/ProcessBakedRegion.cpp` — LOD-chain generation around the existing `meshopt_optimizeVertexFetch` call.
- `DataPacker/Source/ExportJobs/ExportIsland.h` — `GetVersion()` raw bump 29 → 30.
- `Common/DataFile.h` — `IslandHeader` per-LOD fields + `DataHeader::kiVersion` bump + static_assert update.
- `Engine/Source/Frame/IslandTerrain.h` / `IslandTerrain.cpp` — per-LOD offsets/counts on `IslandTemplate`, populated in the ctor manifest read.
- `Engine/Source/Frame/IslandTerrainResidency.cpp` — `CreateClientMeshBuffers` (index-region size math already keyed off `miMeshIndexCount`; verify the `[indices][positions]` offset derivation and the decommit byte counts).
- `Engine/Source/Graphics/Islands.cpp` / `Islands.h` — boot bake of the indirect commands; per-frame `indexCount`/`firstIndex` selection in `UpdateActiveIslands`.
- `Engine/Source/Ui/TerrainWrappersBase.{h,cpp}` + `Engine/Source/Ui/Screens/TweaksScreen/` Terrain section — `gTerrainMeshLodBias`.
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — no code change (comment refresh only: `indexCount` no longer boot-baked).

## Out of scope

- Per-instance LOD selection (top-down camera = uniform view distance; one global LOD per frame is correct by construction).
- Water mesh LOD (already exists) and any change to the elevation/shadow prepasses.
- GPU mesh residency/eviction — `IslandMeshArenaResidency.md` owns that; see Notes for sequencing.
- Terrain collision / `Frame/IslandTerrain` sim queries (heightmap-based, mesh-free) — untouched.
- Fragment-shader sample reduction (`TerrainDetailSampleTrim.md`) and instance culling (`TerrainInstanceAreaCull.md`).

## Acceptance criteria

- `kGpuTimerTerrain` drops measurably at gameplay heights with bias 0; bias −3 reproduces today's cost and visuals exactly.
- No shoreline/texture/shadow movement at LOD switches (Z from composite elevation verified in shader unchanged).

## Coordination

- `Documents/Plans/Graphics/IslandMeshArenaResidency.md`: never interleave indirect mesh-layout bookkeeping; this plan's structured dependency requires the resolved arena design first.

## Notes

- **Invariant exposure:** `.pack` layout change → `DataHeader::kiVersion` bump + `ExportIsland::GetVersion` raw bump (shared with any concurrently-landing `kiVersion` bump). **No CRC/wire/determinism exposure**: the mesh is client-render-only (the server never reads it — `IslandTerrainResidency.cpp` documents the server decommits the mesh slice unread); heightmap, placements, and collision are untouched. Client/graphics + DataPacker only.
- **Sequencing:** `IslandMeshArenaResidency.md` option A rewrites the same indirect `firstIndex`/`vertexOffset` bookkeeping and option B replaces the mesh layout entirely — resolve that decision plan first or land this first and refresh it; never interleave. Shares `IslandTerrainResidency.cpp` with the island residency series File Group and `File/MeshRecommitFailureObservability.md` — co-schedule or refresh citations. Shares `Islands.cpp`/`Islands.h` with `TerrainInstanceAreaCull.md` — co-schedule (the two per-frame writes compose trivially).
- **Grill decisions:** per-LOD `meshopt_simplify` error bounds (meters) and whether LOD3 is ÷64 or capped shallower; whether the bias slider ships range −3..+3 or 0..+3.
