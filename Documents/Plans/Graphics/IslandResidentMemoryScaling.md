# Island Resident-Memory Scaling

## Context

Every `kIsland` template carries **permanently-resident** CPU + GPU data that scales linearly with the
total number of `kIsland` chunks, independent of the `kiMaxIslands` ceiling and of the texture LRU:

- The per-template Gaea2 terrain **mesh** (vertex + index buffers, beach-band densified) is uploaded
  once in `IslandTerrain::CreateClientMeshBuffers` (called from the `Islands` ctor) and held until the
  `kSurface` destroy tier via `ReleaseGpuResources`. The boot-time-then-frozen mesh policy is what
  permits the record-once command-buffer invariant (`Islands` bakes one indirect draw per template at
  boot), so meshes deliberately cannot be created lazily on first visit.
- The per-template **heightmap** (R32 meters) lives in the `kIsland` chunk payload, which `FileManager`
  loads eagerly at boot and keeps resident; the GPU elevation image is re-created from it on demand.

Only the GPU `color` / `normals` / `AO` / `masks` / `elevation` *textures* are LRU-managed
(`TextureManager` + `IslandTerrain` eviction/restoration). The mesh GPU buffers and the in-memory
chunk (mesh + heightmap) are **not** evictable today.

This was negligible at the ~7-chunk ship count (routes `1x1` + `2x1` + `2x2`). The multi-island route
work raised island `01` alone to **65** `kIsland` chunks (routes `1x1`..`4x4`), so the permanently-
resident footprint grew ~9x. `kiMaxIslands` was bumped 64 -> 128 as a stopgap to let the chunks load;
this plan addresses the underlying memory scaling, which the cap bump does not.

## Design

Measure first, then decide. Steps:

1. **Quantify.** Instrument boot to log, per template and in aggregate: resident mesh bytes
   (vertex+index, CPU + GPU) and resident heightmap bytes (the `kIsland` chunk payload). Confirm the
   real per-template footprint and the 65-template total before choosing a strategy — the beach
   densification makes mesh size highly variable, so the dominant term is unknown without numbers.
2. **Pick a strategy** (candidates, not yet decided — this is the design call the measurement informs):
   - **(a) Make per-template meshes + heightmaps LRU-evictable** like the textures. Highest payoff but
     conflicts head-on with the record-once CB invariant (the indirect draws bind meshes at boot). Would
     require either re-recording on residency change (currently banned outside swapchain/settings/
     device-loss) or an indirection that lets an evicted template draw nothing without a re-record.
   - **(b) Lazy-load the `kIsland` chunk** (mesh + heightmap) on first subscription instead of eagerly
     at boot, mirroring the texture lazy-load path, and release on eviction. Keeps the resident set
     proportional to *visited* templates rather than *all* templates.
   - **(c) Cap / tier the total template count** per island (e.g. only bake/ship a subset of routes, or
     gate route export behind a build profile) so the resident set stays bounded by construction.
   - **(d) Compress / share** resident data (e.g. quantize mesh positions, share identical heightmaps
     across routes that crop the same source) — incremental, lower risk, smaller payoff.
3. **Re-evaluate the `kiMaxIslands` value** once residency is bounded: if a strategy makes the resident
   set proportional to concurrent residency rather than total templates, the cap can be reasoned about
   as a residency budget (see `IslandBindlessSlotDescriptorRobustness.md` Issue 2, which already notes
   slot 0 is reserved so usable slots are `1..kiMaxIslands-1`).

## Critical files

- `Engine/Source/Frame/IslandTerrain.cpp` / `.h` — `CreateClientMeshBuffers`, `ReleaseGpuResources`,
  per-template mesh/heightmap residency; `EvictionSweep` / `RestorationSweep` (the texture LRU that
  would need a mesh/heightmap analogue for strategy (a)/(b)).
- `Engine/Source/Graphics/Islands.cpp` — boot-fixed `miTemplateCount`, per-template indirect-draw bake,
  the record-once CB invariant that strategy (a) must respect.
- `Engine/Source/File/FileManager.cpp` — eager `kIsland` chunk load at boot (strategy (b) lazy-load
  seam).
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — `kiMaxIslands` (stopgap 128; revisit per step 3).
- `DataPacker/Source/BakeIslandIntermediates.cpp` + `Engine/Data/Islands/01/Island.json` — the route
  table / route list that determines how many templates an island emits (strategy (c)).

## Out of scope

- The `kiMaxIslands` cap bump 64 -> 128 itself — already landed as the stopgap that motivated this plan.
- The two latent bindless-slot bugs in `IslandBindlessSlotDescriptorRobustness.md` (verifier
  `iArrayIndex` walk; slot-exhaustion off-by-one) — that plan owns them. This plan does not change the
  slot mint/recycle logic.
- The DataPacker bake / split pipeline correctness (route table, `ExpandSpan` borrow-to-alignment) —
  landed with the multi-island route work.
- Adding new routes or islands; changing what ships.

## Acceptance criteria

- Boot logs the per-template and aggregate resident mesh + heightmap footprint, so the scaling is
  measured rather than estimated.
- A chosen strategy bounds the permanently-resident island footprint by *concurrent residency* (or an
  explicit cap) rather than *total template count*, OR the measurement shows the 65-template footprint
  is acceptable and the plan is closed with that justification recorded.
- The record-once command-buffer invariant is preserved (no per-frame / per-residency CB re-record
  introduced), or any deviation is explicitly designed and documented.

## Notes

- Frame/CLAUDE.md is the authoritative description of the current per-template residency model; keep it
  in sync if strategy (a)/(b) changes the mesh/heightmap lifetime.
- The texture LRU already solves the equivalent problem for `color`/`normals`/`AO`/`masks`/`elevation`
  GPU images — strategies (a)/(b) are essentially "extend that residency management to the mesh buffers
  and the in-memory chunk," with the CB-record invariant as the hard constraint the texture path did
  not face.
