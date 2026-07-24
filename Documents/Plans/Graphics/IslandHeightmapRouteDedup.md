<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T02:29:26.000Z","dependsOn":[]} -->
# Island Heightmap Route Dedup

## Context

Follow-up to the landed **R16 heightmap quantization**: the resident per-template heightmap is IEEE
half-float at kIsland chunk-payload offset 0 in the lazy chunk pool (`IslandTemplate::mpHeightmapHalf`,
`const uint16_t*`), which halved the dominant resident CPU bucket from ~181.5 MiB to ~90.75 MiB at
70 templates. That was the first half of the former `IslandHeightmapResidency.md` decision (Option C
— quantize/compress). This plan is the **deferred second half of Option C**: content-addressed dedup
of heightmap data shared across routes/leaves, so byte-identical heightmap payloads are stored once
and aliased by multiple templates instead of occupying an independent pool region per template.

**Speculative — measure first (Step 0 gates everything else).** Savings scale with cross-route/leaf
heightmap redundancy, not with the active on-screen set. Reading the bake pipeline, that redundancy
is expected to be near-zero, so this plan most likely resolves as "accept + document." The reasons
are structural:

- **Routes are independent bakes, not crops of a shared master.** `BakeRoute`
  (`DataPacker/Source/ExportJobs/Island/BakeRoute.cpp`) runs one `Gaea.Swarm.exe` export per route
  with the route's own `RouteSubdivision::iGaeaChoice` patched into the archetype's Route node
  (`PatchArchetype`, `GaeaArchetype.cpp`). Different routes = different Choice = different terrain.
  Two routes never produce byte-identical heightmaps by design.
- **Leaves within a route are disjoint spatial tiles, not overlapping crops.** `BakeRoute` splits
  the single full-res elevation buffer into up to `iColumns × iRows` regions whose boundaries
  *partition* the full texture (`iStartX = iColumn * iTexturePixels / iColumns`, etc.).
  `ProcessBakedRegion.cpp`'s `FindRegionBbox` is confined to each region (a 2x1 half never pulls
  land across the split seam), then `CropAndDownsampleElevation` box-filters by `kiElevationDivisor`
  and re-centers. Each leaf's heightmap therefore covers a *different* spatial region of the master
  — not byte-identical to any sibling leaf.
- **The only shared source pixels are the seam alignment strips.** `ComputeCropRect`/`ExpandSpan`
  can borrow up to `kiCropAlignment` neighbour pixels across a seam to reach crop alignment. Even
  those overlap strips are (a) a tiny fraction of each leaf's heightmap and (b) land at different
  crop origins, so their post-box-downsample R16 values are not byte-identical between neighbours.
- **Runtime templates are already 1:1 with distinct chunk CRCs.** `IslandTerrain`'s ctor
  (`Engine/Source/Frame/IslandTerrain.cpp`) builds one `IslandTemplate` per kIsland chunk keyed by
  CRC; each leaf's CRC is path-derived (`<island>/<route>/<index>`), so two byte-identical payloads
  would still be two chunks / two templates with two independent pool regions — nothing dedups them
  today, but nothing produces byte-identical payloads to dedup either.

Net: the honest expectation is that a redundancy measurement shows ~0% byte-identical heightmap
payloads and this plan closes as accept-and-document. It is kept as a live plan only because the
user explicitly deferred it and because a content-pipeline change (e.g. routes deliberately sharing
crops, or repeated placement of one master) could revive the value later. Part of the island
resident-memory scaling series — see `IslandResidentMemoryScaling_Overview.md`.

## Design

### Step 0 — measure (gates the rest of the plan)

Add a DataPacker-side diagnostic that hashes each emitted per-leaf R16 heightmap payload — the exact
`heightmapHalf` bytes `ExportIsland::Export` (`DataPacker/Source/ExportJobs/ExportIsland.cpp`)
memcpys to chunk-payload offset 0 — and reports a duplicate histogram: distinct hashes, total
leaves, bytes saved if identical payloads were emitted once. If the duplicate fraction is negligible
(the expected outcome), stop here and record the finding in this plan plus
`IslandResidentMemoryScaling_Overview.md` — do not build the machinery below.

### Conditional — only if Step 0 shows material byte-identical redundancy

**DataPacker — content-address and emit once.** In `ExportIsland::Export`, content-address each
leaf's R16 heightmap payload by CRC. Emit each *distinct* heightmap payload once into a shared
heightmap chunk (or reuse the first-emitting leaf's payload — see Open decision), and have every
kIsland chunk reference its heightmap by CRC rather than carrying it inline:

- Add `common::crc_t heightmapCrc` to `IslandHeader` (`Common/DataFile.h`) pointing at the shared
  heightmap chunk; the kIsland chunk payload drops the leading heightmap and becomes
  `[float2 mesh positions][uint32 mesh indices][float2 valid-area hull]` only.
- Emitting a distinct payload once requires a stable, content-derived chunk identity so the same
  bytes always dedup to the same CRC across leaves and across bakes (see Invariants below).
- This restructures the `.pack` layout: update the `sizeof(IslandHeader) == 72` static_assert, bump
  `DataHeader::kiVersion`'s manual constant (currently `50 + sizeof(ChunkHeader)`), and bump
  `ExportIsland::GetVersion`'s raw version (`Version(29)` → `Version(30)`,
  `DataPacker/Source/ExportJobs/ExportIsland.h`), so a stale cache is fully re-exported (the
  static_assert message beside `IslandHeader` spells out this triple-bump rule).

**Runtime — alias multiple templates at one pool region.** In `IslandTerrain::WaitForElevationMaps`
(`Engine/Source/Frame/IslandTerrain.cpp`) resolve `mpHeightmapHalf` from the shared heightmap chunk
(`rChunkMap.at(...heightmapCrc).pData`) instead of the kIsland chunk's own `pData` at offset 0.
Templates sharing a `heightmapCrc` then alias the same lazy-pool region — one resident copy for N
templates. The ctor's `RequestChunkLoad(mIslandCrcsSorted, ...)` and `WaitForElevationMaps`'s
`WaitForChunks(mIslandCrcsSorted)` must additionally cover the shared heightmap CRCs. The kIsland
chunk's remaining slices (mesh, hull) keep their existing offset math, rebased so the mesh
positions start at payload offset 0. Both builds load the shared heightmap identically (server
reads it for NavContour; client uploads the R16 elevation image at first mint via
`CreateElevationTextureFromHeightmap`) — no determinism change because the bytes are byte-identical
to today.

**Interaction with the landed mesh CPU-slice reclaim.** The server `DecommitChunkRange`s the
`[positions][indices]` sub-range of each kIsland chunk in `WaitForElevationMaps`, and the client
does the same in `IslandTerrain::CreateClientMeshBuffers`
(`Engine/Source/Frame/IslandTerrainResidency.cpp`), recommitting via `RecommitAndReloadChunkRange`
on device-loss recovery. Both operate on the *kIsland* chunk, which stays unshared, so they are
unaffected — but their range arguments currently offset past `iHeightmapBytes`, and must rebase to
offset 0 when the heightmap leaves the kIsland payload. The shared *heightmap* chunk, by contrast,
is aliased by multiple templates and must stay fully resident: never decommit it per-template (a
shared region has no single owner). If a future plan ever wants to evict heightmaps, it must
refcount the shared chunk — out of scope here.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the
acceptance criteria, and add no abstractions, configuration, refactors, or fixes to adjacent code
encountered along the way. Naming a file grants no permission to touch anything in it beyond the
named regions plus the mechanical necessities (includes, forward declarations) the named change
requires.

**In scope — Step 0 (unconditional):**

- `DataPacker/Source/ExportJobs/ExportIsland.cpp` — `ExportIsland::Export`: hash the
  `heightmapHalf` payload bytes and feed the duplicate histogram; the aggregation/report mechanism
  is local detail.
- This plan file and `Documents/Plans/Graphics/IslandResidentMemoryScaling_Overview.md` — record
  the measured redundancy result.

**In scope — conditional (only if Step 0 shows material redundancy):**

- `DataPacker/Source/ExportJobs/ExportIsland.cpp` — `ExportIsland::Export`: content-address,
  emit-once, payload restructure (drop leading heightmap; keep
  `[positions][indices][hull]` assembly), set `islandHeader.heightmapCrc`.
- `DataPacker/Source/ExportJobs/ExportIsland.h` — `GetVersion`: `Version(29)` → `Version(30)`.
- `Common/DataFile.h` — `IslandHeader`: add `heightmapCrc`, update the payload-layout comment and
  the `sizeof(IslandHeader) == 72` static_assert; `DataHeader::kiVersion`: bump the manual
  constant.
- `Engine/Source/Frame/IslandTerrain.cpp` — ctor: extend the `RequestChunkLoad` call to cover
  shared heightmap CRCs; `WaitForElevationMaps`: extend `WaitForChunks`, re-source
  `mpHeightmapHalf` from the shared chunk, rebase the mesh/hull offset math and the
  `DecommitChunkRange` range to the heightmap-less payload.
- `Engine/Source/Frame/IslandTerrain.h` — `IslandTemplate`: only what re-sourcing requires (e.g. a
  stored heightmap chunk CRC); `mpHeightmapHalf` keeps its type.
- `Engine/Source/Frame/IslandTerrainResidency.cpp` — `CreateClientMeshBuffers`: rebase the
  `RecommitAndReloadChunkRange`/`DecommitChunkRange` offsets to the heightmap-less kIsland payload.

**Read-only references (no edits):**

- `DataPacker/Source/ExportJobs/Island/BakeRoute.cpp`, `ProcessBakedRegion.cpp`,
  `BakeIslandIntermediates.cpp`, `GaeaArchetype.cpp` — establish the redundancy argument above.
- `Engine/Source/File/FileManager.{h,cpp}` — lazy chunk pool and the
  `DecommitChunkRange`/`RecommitAndReloadChunkRange` API; reference for why a shared chunk must not
  be per-template-decommitted.

**Out of scope:**

- **The R16 quantization itself** — already landed (this plan is the deferred dedup half only).
- **The residency/eviction approaches** — Option A (sim-ring residency/eviction, not taken) and
  Option B (out-of-pool arena, dropped) of the former `IslandHeightmapResidency.md` decision. This
  plan keeps the CPU heightmap permanently resident; it only shares byte-identical copies.
- **The other resident buckets** — the mesh CPU-slice reclaim (landed; see
  `IslandTerrain.cpp`/`IslandTerrainResidency.cpp` above), GPU mesh arena
  (`IslandMeshArenaResidency.md`), SSBO placement arena (`IslandPlacementSsboResidency.md`), and
  server NavContour residency are each their own work.
- **Any heightmap eviction/refcount lifecycle** for the shared chunk — noted as a future concern
  only; not built here. The existing client texture-slot eviction (`EvictTemplate`,
  `IslandTerrainResidency.cpp`) is GPU-side and untouched.
- **Texture-channel dedup** (color/normals/AO/masks) — the unique-channel-CRC invariant asserted in
  `IslandTerrain`'s ctor deliberately forbids sharing those; this plan touches only the heightmap.

## Risk tier and invariants

**Tier 3** (conditional half): touches `.pack` layout, `IslandHeader`, `DataHeader::kiVersion`, and
`ExportIsland::GetVersion` (the version triple-bump), plus both-builds heightmap aliasing in
`WaitForElevationMaps`. Step 0 alone is Tier 2 (DataPacker diagnostic only, no emitted-byte change).

- **No determinism change if byte-identical.** The sim-consumed heightmap (elevation-grid splat,
  NavContour) reads exactly the same bytes; dedup changes *where* they live in the pool, not their
  value. CRC/replay is unaffected.
- **Emit-once identity must be content-derived and deterministic** under the
  single-canonical-bake-machine assumption documented in `DataPacker/Source/AGENTS.md`
  (Reproducibility); Gaea's own GPU-dependent output is the upstream variance, unchanged here.
- **The shared heightmap chunk is never decommitted per-template** (no single owner, no refcount).

## Acceptance criteria

- The Step-0 DataPacker histogram exists and reports byte-identical heightmap redundancy across all
  shipping leaves. If negligible, the plan closes with that finding recorded here and in
  `IslandResidentMemoryScaling_Overview.md`.
- If pursued: multiple templates whose leaves produced byte-identical heightmaps share one lazy-pool
  region (verified by pointer-aliasing / pool-byte reduction), with no change to the bytes any
  template samples — CRC-identical sim on both builds, and the R16 elevation image / NavContour
  output bit-identical to pre-dedup.

## Open decision (route through `/external-grill-plan`, only if Step 0 shows redundancy)

Shared-chunk representation — (a) a dedicated new "shared heightmap" chunk type referenced by
`heightmapCrc`, vs (b) reuse the first-emitting leaf's kIsland chunk payload and point later
duplicates' `heightmapCrc` at it (no new chunk type, but couples a shared payload's lifetime to one
arbitrary leaf's chunk). Do not implement the conditional half before this decision is made.
