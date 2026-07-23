<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T02:29:26.000Z","dependsOn":[]} -->
# Island Heightmap Route Dedup

## Context

Follow-up to the landed **R16 heightmap quantization** (the resident per-template heightmap is now
IEEE half-float in the kIsland chunk payload + lazy pool — `IslandTemplate::mpHeightmapHalf`,
`const uint16_t*` — which halved the dominant resident CPU bucket from ~181.5 MiB to ~90.75 MiB at
70 templates). That was the first half of the former `IslandHeightmapResidency.md` decision (Option
C — quantize/compress). This plan is the **deferred second half of Option C**: content-addressed
dedup of heightmap data shared across routes/leaves that crop the same Gaea source master, so
byte-identical heightmap regions are stored once and aliased by multiple templates instead of
occupying an independent pool region per template.

**Speculative — profile first. Savings scale with cross-route/leaf heightmap redundancy, not with
the active on-screen set. Reading the bake pipeline, that redundancy is expected to be near-zero,
so this plan most likely resolves as "accept + document."** The reasons are structural:

- **Routes are independent bakes, not crops of a shared master.** `BakeRoute` (`BakeRoute.cpp`)
  runs one `Gaea.Swarm.exe` export per route with the route's own `RouteSubdivision::iGaeaChoice`
  patched into the archetype's Route node (`PatchArchetype`). Different routes = different Choice =
  different terrain. Two routes never produce byte-identical heightmaps by design.
- **Leaves within a route are disjoint spatial tiles, not overlapping crops.** `BakeRoute` splits
  the single full-res elevation buffer into up to `iColumns × iRows` regions whose boundaries
  *partition* the full texture (`iStartX = iColumn * iTexturePixels / iColumns`, etc.).
  `ProcessBakedRegion::FindRegionBbox` is confined to each region ("a 2x1 half never pulls land
  across the split seam"), then `CropAndDownsampleElevation` box-filters by `kiElevationDivisor`
  and re-centers. Each leaf's heightmap therefore covers a *different* spatial region of the master
  → not byte-identical to any sibling leaf.
- **The only shared source pixels are the seam alignment strips.** `ComputeCropRect`/`ExpandSpan`
  can borrow up to `kiCropAlignment` neighbour pixels across a seam to reach crop alignment. Even
  those overlap strips are (a) a tiny fraction of each leaf's heightmap and (b) land at different
  crop origins, so their post-box-downsample R16 values are not byte-identical between neighbours.
- **Runtime templates are already 1:1 with distinct chunk CRCs.** `IslandTerrain`'s ctor builds one
  `IslandTemplate` per kIsland chunk keyed by CRC; each leaf's CRC is path-derived
  (`<island>/<route>/<index>`), so two byte-identical payloads would still be two chunks / two
  templates with two independent pool regions — nothing dedups them today, but nothing produces
  byte-identical payloads to dedup either.

Net: the honest expectation is that a redundancy measurement shows ~0% byte-identical heightmap
regions and this plan closes as accept-and-document. It is kept as a live plan only because the
user explicitly deferred it and because a content-pipeline change (e.g. routes deliberately sharing
crops, or repeated placement of one master) could revive the value later. Part of the island
resident-memory scaling series — see `IslandResidentMemoryScaling_Overview.md`.

## Design

**Step 0 — measure (gate the rest of the plan).** Add a DataPacker-side diagnostic pass that hashes
each emitted per-leaf R16 heightmap payload (the exact `heightmapHalf` bytes `ExportIsland::Export`
writes at chunk-payload offset 0) and reports a duplicate histogram: distinct hashes, total leaves,
bytes saved if identical payloads were emitted once. If the duplicate fraction is negligible (the
expected outcome), stop here and record the finding in this plan + the overview doc — do not build
the machinery below.

If (and only if) the measurement shows material byte-identical redundancy:

**DataPacker — content-address and emit once.** In `ExportIsland::Export`
(`ExportJobs/ExportIsland.cpp`) content-address each leaf's R16 heightmap payload by CRC. Emit each
*distinct* heightmap payload once into a shared heightmap chunk (or reuse the first-emitting leaf's
payload), and have every kIsland chunk reference its heightmap by CRC rather than carrying it inline:

- Add `common::crc_t heightmapCrc` to `IslandHeader` (`Common/DataFile.h`) pointing at the shared
  heightmap chunk; the kIsland chunk payload drops the leading heightmap and becomes
  `[float2 mesh positions][uint32 mesh indices][float2 valid-area hull]` only.
- Emitting a distinct payload once requires a stable, content-derived chunk identity so the same
  bytes always dedup to the same CRC across leaves and across bakes (see determinism note below).
- This restructures the `.pack` layout: bump the `IslandHeader` `sizeof` static_assert, bump
  `DataHeader::kiVersion`, and bump `ExportIsland::GetVersion`'s raw version (currently `Version(29)`
  → `Version(30)`), so a stale cache is fully re-exported (the static_assert comment beside
  `IslandHeader` already spells out this triple-bump rule).

**Runtime — alias multiple templates at one pool region.** In `IslandTerrain::WaitForElevationMaps`
(`IslandTerrain.cpp`) resolve `mpHeightmapHalf` from the shared heightmap chunk
(`rChunkMap.at(rIslandHeader.heightmapCrc).pData`) instead of the kIsland chunk's own `pData` at
offset 0. Templates sharing a `heightmapCrc` then alias the same lazy-pool region — one resident
copy for N templates. `RequestChunkLoad`/`WaitForChunks` must include the shared heightmap CRCs. The
kIsland chunk's remaining slices (mesh, hull) keep their existing offset math, now rebased to
payload offset 0. Both builds load the shared heightmap identically (server reads it for NavContour;
client uploads the R16 elevation image at first mint) — no determinism change because the bytes are
byte-identical to today.

**Interaction with existing sub-range reclaim.** The server currently `DecommitChunkRange`s the mesh
CPU slice of each kIsland chunk in `WaitForElevationMaps`, and the client mesh CPU-slice reclaim
does the same client-side — both operate on the *kIsland* chunk, which stays unshared, so they are
unaffected (their offsets rebase to the new payload). The shared *heightmap* chunk, by contrast, is
aliased by multiple templates and must stay fully resident: it must never be decommitted per-template
(a shared region has no single owner). If a future plan ever wants to evict heightmaps, it must
refcount the shared chunk — out of scope here.

## Critical files

- `DataPacker/Source/ExportJobs/ExportIsland.cpp` — `ExportIsland::Export` (heightmap quantize +
  chunk-payload assembly); add the Step-0 hash histogram, and (if pursued) the content-address +
  emit-once logic and the payload restructure.
- `DataPacker/Source/ExportJobs/ExportIsland.h` — `ExportIsland::GetVersion` raw version bump.
- `DataPacker/Source/ExportJobs/Island/BakeRoute.cpp`, `ProcessBakedRegion.cpp`,
  `BakeIslandIntermediates.cpp` — read-only for this plan; they establish that leaves are disjoint
  tiles and routes are independent bakes (the redundancy argument above). No edits expected.
- `Common/DataFile.h` — `IslandHeader` (`heightmapCrc` field, `sizeof` static_assert),
  `DataHeader::kiVersion`.
- `Engine/Source/Frame/IslandTerrain.cpp` — `IslandTerrain` ctor (template build + `RequestChunkLoad`)
  and `WaitForElevationMaps` (`mpHeightmapHalf` aliasing, mesh/hull offset rebase).
- `Engine/Source/Frame/IslandTerrain.h` — `IslandTemplate::mpHeightmapHalf` (unchanged type; new
  source chunk).
- `Engine/Source/File/FileManager.{h,cpp}` — the lazy chunk pool + existing
  `DecommitChunkRange`/`RecommitAndReloadChunkRange` API; read-only reference for how a shared chunk
  aliases into the pool and why it must not be per-template-decommitted.

## Out of scope

- **The R16 quantization itself** — already landed (this plan is the deferred dedup half only).
- **The residency/eviction approaches** — Option A (sim-ring residency/eviction, not taken) and
  Option B (out-of-pool arena, dropped) of the former `IslandHeightmapResidency.md` decision. This
  plan keeps the heightmap permanently resident; it only shares byte-identical copies.
- **The other resident buckets** — mesh CPU slice (`IslandMeshCpuSliceReclaim.md`), GPU mesh arena
  (`IslandMeshArenaResidency.md`), SSBO placement arena (`IslandPlacementSsboResidency.md`), and
  server NavContour (`IslandNavContourResidency.md`) each have their own plan.
- **Any heightmap eviction/refcount lifecycle** for the shared chunk — noted as a future concern
  only; not built here.
- **Texture-channel dedup** (color/normals/AO/masks) — the unique-channel-CRC invariant in
  `IslandTerrain`'s ctor deliberately forbids sharing those; this plan touches only the heightmap.

## Acceptance criteria

- The Step-0 DataPacker histogram exists and reports byte-identical heightmap redundancy across all
  shipping leaves. If negligible, the plan closes with that finding recorded here and in the
  overview doc.
- If pursued: multiple templates whose leaves produced byte-identical heightmaps share one lazy-pool
  region (verified by pointer-aliasing / pool-byte reduction), with no change to the bytes any
  template samples — CRC-identical sim on both builds, and the R16 elevation image / NavContour
  output bit-identical to pre-dedup.

## Notes

- **Independent of the residency-lifecycle idea.** This is orthogonal to Option A's evict/reload
  model — both client and server load the heightmap; dedup just shares byte-identical copies while
  keeping them resident.
- **No determinism change if byte-identical.** The sim-consumed heightmap (`BuildElevationGrid`,
  NavContour) reads exactly the same bytes; dedup changes *where* they live in the pool, not their
  value. CRC is unaffected.
- **Invariant exposure:** touches `.pack` layout, `IslandHeader`, `DataHeader::kiVersion`, and
  `ExportIsland::GetVersion` (a version triple-bump), plus the both-builds heightmap aliasing in
  `WaitForElevationMaps`. It does **not** change any sampled value, so no CRC/replay divergence. The
  emit-once identity must be content-derived and deterministic under the single-canonical-bake-machine
  assumption already documented in `DataPacker/Source/AGENTS.md` (Cross-Machine Reproducibility);
  Gaea's own GPU-dependent output is the upstream variance, unchanged by this plan.
- **Single open decision for `/external-grill-plan` (only if Step 0 shows redundancy):** shared-chunk
  representation — (a) a dedicated new "shared heightmap" chunk type referenced by `heightmapCrc`, vs
  (b) reuse the first-emitting leaf's kIsland chunk payload and point later duplicates' `heightmapCrc`
  at it (no new chunk type, but couples a shared payload's lifetime to one arbitrary leaf's chunk).
