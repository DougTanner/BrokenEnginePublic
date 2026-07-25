<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-03T02:45:42.000Z","dependsOn":[]} -->
# Terrain Mesh LOD Chain (per-template, indirect-selected)

## Context

`kGpuTimerTerrain` (swapchain Image pass) measures **998 µs current / 987 avg / 1027 max** at 120 fps in a Profile build (~6.6 ms total GPU/frame) — the third-biggest GPU cost after Water (2329 µs) and the rest of the Image pass. Caveat: measured on an **idle scene near the spawn islands**; combat scenes shift the mix but the terrain pass cost is camera/scene-static, so this number is representative for terrain.

The dominant cost driver is mesh density with no LOD. Each island template draws its full Gaea2-Mesher bake (`VerticesPerSide` 1024 in the archetype, plus `SubdivideBeachBand` shoreline densification) at **every** zoom level: ~119.5 MiB of GPU mesh across 70 templates (`IslandResidentMemoryScaling_Overview.md` measurement; the top ~3 islands are ~25 MiB ≈ ~1.5M triangles each). At gameplay camera heights (kilometers above the ocean) an island covers a few hundred pixels but submits hundreds of thousands of triangles — sub-pixel triangles whose cost shows up three ways: vertex shading (each `Terrain.vert` invocation does **two** `textureLod` fetches — composite elevation at `Terrain.vert:114` + own-heightmap sink at `Terrain.vert:120`), raster setup, and fragment quad overshading (a triangle covering ~1 pixel still shades a full 2x2 quad, so fragment invocations scale with triangle count, not screen pixels — industry-standard finding, the reason LOD is the canonical fix).

The engine already has the exact mechanism, for water only: `BufferManager::mWaterMeshLods` (`kiVisibleAreaLodCount = 4`, `BufferManager.h`) concatenates per-LOD index regions into one index buffer and selects `iIndexOffset`/`iIndexCount` per frame through host-visible indirect commands — "no command-buffer re-record on LOD change" per the comment block above `kiVisibleAreaLodCount`, which explicitly notes terrain does *not* do this. The terrain indirect path is equally ready: `Islands::UpdateActiveIslands` (`Islands.cpp`) already rewrites the per-template `VkDrawIndexedIndirectCommand` every frame (today only `instanceCount`); `indexCount`/`firstIndex` are just two more fields in the same mapped write. The record-once CB is untouched — `CommandBufferRecordMain.cpp` binds each template's whole `mMeshBuffer` and issues one `vkCmdDrawIndexedIndirect` per template, exactly as now.

DataPacker already links meshoptimizer (`ProcessBakedRegion.cpp`'s `CropAndRepackMesh` uses `meshopt_optimizeVertexFetch`); `meshopt_simplify` generates coarser index buffers **over the existing vertex buffer**, which is precisely the concat-index-region layout needed.

## Design

**DataPacker (bake-time):**

- In `ProcessBakedRegion.cpp`, inside `CropAndRepackMesh` (after the triangle-discard crop compaction, before the existing `meshopt_optimizeVertexFetch` call), generate `kiVisibleAreaLodCount` (4) index sets: LOD0 = the current full post-crop index buffer; LOD1..3 via `meshopt_simplify` with target index count ÷4 per level and a per-LOD absolute error bound in meters (protects the `SubdivideBeachBand` shoreline band at near LODs; error-bound values are an unresolved decision — see Notes). Concatenate **coarsest-first** (meshoptimizer's recommended assembly — coarse LODs then occupy a small vertex range), then run `meshopt_optimizeVertexFetch` once over the concatenated index buffer. Positions here are still bare float XYZ triples (12-byte stride); XY extraction happens downstream, unchanged.
- `WriteMeshProcessed` (same file) and its reader in `ExportIsland.cpp` (the `MeshProcessed.bin` load that fills `iMeshIndexCount` and `cpuMeshIndices`) carry the per-LOD index counts through the intermediate; `ExportIsland.cpp`'s header fill (`pHeader->islandHeader.iMeshIndexCount = ...`) writes the new per-LOD fields.
- `IslandHeader` (`Common/DataFile.h`) gains per-LOD index counts (`int32_t piLodIndexCounts[4]`, offsets implied by coarsest-first prefix sums); `iMeshIndexCount` becomes the concat total so the chunk-payload layout documented on `IslandHeader` (`[positions][indices][hull verts]`) and all size math keyed off it keep working unchanged. Update the `sizeof(IslandHeader) == 72` static_assert value and keep its message per the established pattern.
- Index memory grows ~+33% of the index region (sum of ÷4 chain), ~+10-15 MiB GPU/CPU total — acceptable against the 119.5 MiB base; update the numbers in `IslandResidentMemoryScaling_Overview.md` when landing.

**Engine (per-frame):**

- `IslandTemplate` (`Engine/Source/Frame/IslandTerrain.h`) carries the per-LOD counts read from the header, populated in the `IslandTerrain` ctor next to the existing `rTemplate.miMeshIndexCount = rLazyChunk.header.islandHeader.iMeshIndexCount` manifest read (`IslandTerrain.cpp`).
- `Islands::UpdateActiveIslands` (`Engine/Source/Graphics/Islands.cpp`) selects the frame's LOD once from the camera's already-hysteresis-latched zoom bucket — `game::gpCamera->miVisibleAreaLod` (`CameraBase.h`; hysteresis in `CameraBase.cpp`), the same latch that drives water mesh LOD and snap coarseness, so terrain/water tessellation move in lockstep and snap-grid stability is inherited (no per-frame `quadSize` interaction; the mesh is world-anchored per island). Add the runtime bias (below), clamp to [0, 3], then write `indexCount`/`firstIndex` alongside the existing per-template `instanceCount` rewrite. The boot bake of the indirect commands in the `Islands` ctor initializes them to LOD0 (today it bakes `indexCount = rTemplate.miMeshIndexCount`, `firstIndex = 0`).
- The elevation/shadow prepasses are unaffected (they draw SSBO quads via `QuadsAxisAlignedVisibleArea.vert`, not the mesh), so the composite elevation the water blend / shadows / `Terrain.frag` consume is **identical at every LOD** — `Terrain.vert` re-derives Z from that composite per vertex, which is what keeps LOD switches visually pinned.

**Runtime slider (mandatory tradeoff control):** new `gTerrainMeshLodBias` `Wrapper` in `TerrainWrappersBase.{h,cpp}` — `Wrapper(0.0f, -3.0f, 3.0f, 1.0f)` (step 1.0 integer-snaps; default 0) — registered in `TweaksScreenTerrain.cpp`'s `TweaksSliderMapRegistrar` table plus a `WrapperSlider` row in its Terrain section. Added to `miVisibleAreaLod` and clamped to [0, 3]. −3 forces full density everywhere (exact current behavior = the fallback); +N trades fidelity for µs and doubles as the direct A/B measurement knob against `kGpuTimerTerrain`.

**Visual impact: minor/imperceptible at gameplay camera heights.** Vertex Z always re-derives from the same composite elevation sampler and island UVs derive from vertex XY, so textures, shoreline blend (water reads the same elevation RTT), and shadows do not move; simplification error is XY-tessellation-only and sub-pixel at the heights where coarse LODs engage. LOD transitions ride the existing 5%-hysteresis zoom bucket (no oscillation); a one-frame retriangulation shimmer at the switch is the worst case. At `miVisibleAreaLod = 0` (lowest zoom bucket) LOD0 = today's mesh — zoomed-in fidelity is bit-identical.

**Expected saving:** the vertex/raster/overshading share of the 998 µs — estimated 300-600 µs at typical gameplay heights (triangle count ÷4..÷64 where the pass is geometry-bound), directly measurable via the bias slider before/after. Idle-scene caveat applies; the win grows when zoomed out.

## Scope contract

The list below is both target and ceiling: implement the named regions with the smallest complete change, plus only the mechanical necessities they require (includes, declarations, the static_assert value the layout change forces). Naming a file grants no permission to touch anything else in it. No new abstractions, configuration, refactors, or fixes to adjacent code encountered along the way.

### In scope (file → regions)

- `DataPacker/Source/ExportJobs/Island/ProcessBakedRegion.cpp` — `CropAndRepackMesh` (LOD-chain generation around the existing `meshopt_optimizeVertexFetch` call) and `WriteMeshProcessed` (per-LOD counts in the intermediate format).
- `DataPacker/Source/ExportJobs/ExportIsland.h` — `GetVersion()` raw bump `Version(29)` → `Version(30)`.
- `DataPacker/Source/ExportJobs/ExportIsland.cpp` — the `MeshProcessed.bin` reader (per-LOD counts into the exported-mesh struct) and the `islandHeader` field fill in `Export()`.
- `Common/DataFile.h` — `IslandHeader` per-LOD count fields + comment, `DataHeader::kiVersion` bump, `sizeof(IslandHeader)` static_assert value.
- `Engine/Source/Frame/IslandTerrain.h` — `IslandTemplate`: per-LOD count members beside `miMeshVertexCount`/`miMeshIndexCount`.
- `Engine/Source/Frame/IslandTerrain.cpp` — `IslandTerrain` ctor manifest-header read: populate the new members.
- `Engine/Source/Frame/IslandTerrainResidency.cpp` — `IslandTerrain::CreateClientMeshBuffers` only: verify `iMeshIndexBytes`/`iMeshPositionBytes` size math against the concat-total `miMeshIndexCount`, the `[uint32 indices][float2 positions]` GPU buffer layout, and the recommit/decommit byte counts (`iMeshBytes`) — expected to need no change beyond comments once `miMeshIndexCount` is the concat total.
- `Engine/Source/Graphics/Islands.cpp` — ctor boot bake of `VkDrawIndexedIndirectCommand` (LOD0 init) and `UpdateActiveIslands` (LOD select + `indexCount`/`firstIndex` write beside the `instanceCount` rewrite).
- `Engine/Source/Graphics/Islands.h` — only declarations the `Islands.cpp` changes require (likely none).
- `Engine/Source/Ui/TerrainWrappersBase.h` / `.cpp` — `gTerrainMeshLodBias` extern + definition.
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenTerrain.cpp` — registrar entry + `WrapperSlider` row.
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — comment refresh only in the terrain-mesh record block (`indexCount` no longer boot-only); no code change.
- `Engine/Source/Graphics/Managers/BufferManager.h` — comment refresh only above `kiVisibleAreaLodCount` (the "Water only — terrain draws … instead" parenthetical becomes stale); no code change.
- `Documents/Plans/Graphics/IslandResidentMemoryScaling_Overview.md` — memory-number refresh at landing.

### Out of scope

- Per-instance LOD selection (top-down camera = uniform view distance; one global LOD per frame is correct by construction).
- Water mesh LOD (already exists) and any change to the elevation/shadow prepasses.
- GPU mesh residency/eviction — `IslandMeshArenaResidency.md` owns that; see Notes for sequencing.
- Terrain collision / `Frame/IslandTerrain` sim queries (heightmap-based, mesh-free) — untouched.
- Height-faded rock/beach detail-normal sampling in `Terrain.frag`, and `Islands.cpp`'s existing packing contract: a mesh-visible indirect-count prefix followed by the full subscribed offscreen remainder for fixed-count prepasses — preserve, do not modify.
- Everything else in the in-scope files: the crop/re-center math in `CropAndRepackMesh`, LRU/refcount/SSBO logic in `UpdateActiveIslands`, device-loss recommit flow in `CreateClientMeshBuffers`, all other wrappers and slider rows.

## Risk tier and invariants

**Tier 3** — `.pack` chunk-layout change (data-layout surface): `DataHeader::kiVersion` bump + `ExportIsland::GetVersion` raw bump (shared with any concurrently-landing `kiVersion` bump). **No CRC/wire/determinism exposure**: the mesh is client-render-only (the server never reads it — `IslandTerrainResidency.cpp` documents the server decommits the mesh slice unread); heightmap, placements, and collision are untouched. Client/graphics + DataPacker only. Preserve the record-once command-buffer invariant (no CB re-record on LOD change) and the host/GPU indirect-write framebuffer-index discipline already documented at the top of `UpdateActiveIslands`.

## Acceptance criteria

- `kGpuTimerTerrain` drops measurably at gameplay heights with bias 0; bias −3 reproduces today's cost and visuals exactly.
- No shoreline/texture/shadow movement at LOD switches (Z from composite elevation verified in shader unchanged).

## Coordination

- `Documents/Plans/Graphics/IslandMeshArenaResidency.md`: never interleave indirect mesh-layout bookkeeping; this plan's structured dependency requires the resolved arena design first.

## Notes

- **Sequencing:** `IslandMeshArenaResidency.md` option A rewrites the same indirect `firstIndex`/`vertexOffset` bookkeeping and option B replaces the mesh layout entirely — resolve that decision plan first or land this first and refresh it; never interleave. Shares `IslandTerrainResidency.cpp` with the island residency series File Group — co-schedule or refresh citations. Its per-frame indirect writes must preserve `Islands.cpp`/`Islands.h`'s mesh-visible prefix and offscreen-remainder count split.
- **Unresolved decisions (resolve with the user before implementation; do not pick silently):** per-LOD `meshopt_simplify` error bounds (meters) and whether LOD3 is ÷64 or capped shallower.
