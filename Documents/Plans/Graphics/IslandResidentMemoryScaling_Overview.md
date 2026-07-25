# Island Resident-Memory Scaling — Overview

**Reference / index document — not an executable plan.** Deliberately carries no byte-zero `broken-engine-plan/v1` metadata, so WorktreeCli never schedules it. It anchors the series of plans that bound per-template island resident memory: it records the shared boot measurement, the closed decisions, and the architecture findings that shaped the decomposition, so each sibling plan does not re-derive them.

## Role and scope contract

This document authorizes no implementation work. Implementation scope, in-file boundaries, risk tiers, and acceptance criteria live exclusively in the sibling plans it indexes; nothing in this overview widens or overrides them. The only in-scope changes to this file are reference updates — series status, measurements, findings — when a sibling plan lands, closes, or is re-measured. That maintenance scope is both target and ceiling: do not merge sibling plan content into this file, add executable metadata, or derive new work items from it.

## The problem

Island 01's multi-island route table raised the per-template count ~9× to **70 templates** (`kiMaxIslands` bumped 64→128 as a stopgap). Several per-template allocations stay **permanently resident, scaling with total template count** rather than with concurrent on-screen/simulated residency, and escape the texture LRU (which manages only the GPU G-buffer / elevation images).

## Measurement (`[DEBUG-resmem]` boot capture, 70 templates)

Historical capture — taken before the mesh-CPU reclaim landed; see the series table for which buckets have since been addressed.

| Bucket | Size | Residency at capture |
|---|---|---|
| heightmap (`mpHeightmapHalf`, R16 half-float) | ~90.75 MiB | CPU, lazy pool |
| mesh CPU (`mpfMeshPositions`/`mpuiMeshIndices`) | 119.5 MiB | CPU, lazy pool |
| mesh GPU (`mMeshBuffer`) | 119.5 MiB | VRAM |
| SSBO placement arena (`mIslandsStorageBuffers`) | 16.4 MiB | VRAM (host-visible) |
| hull (`mpf2ValidAreaVertices`) | 22 KiB | CPU, lazy pool |
| **Total** | **~346 MiB** | — |

The heightmap bucket is stored as R16 IEEE half-float (2 bytes/texel, dequantized to float on CPU read via `XMConvertHalfToFloat`), so it sits at ~half its former R32 figure (~181.5 MiB → ~90.75 MiB). Headroom to the `kiMaxIslands=128` cap: ~1.8× → ~630 MiB if the route table grows toward it. The distribution is heavily skewed — the top ~3 islands carry ~25 MiB each; dozens of small islands are <1 MiB — so a concurrent-residency bound pays off disproportionately.

## Server NavContour measurement and decision (`[DEBUG-resmem]`, 70 templates)

Server `NavContour` storage measured **57,264 logical-attributable bytes (55.92 KiB)** and **67,992 capacity-attributable bytes (66.40 KiB)**, with a maximum per-template capacity of **2,544 bytes (2.48 KiB)**. The method counts `sizeof(NavContour)` plus logical size or capacity storage for `vertices`, `polygonOffsets`, `visEdgeA`, and `visEdgeB`; allocator metadata/padding and process heap-arena commitment are excluded.

Against the ~90.77 MiB server island CPU base (~90.75 MiB heightmaps plus 22 KiB hull), capacity-attributable NavContour storage is ~0.071%, well below the 5% budget (~4.54 MiB). At 128 templates, the observed average projects to ~121.4 KiB; applying the observed maximum to every template projects to 318 KiB. Neither projection bounds unknown future topology.

**Decision (closed):** eager server NavContour residency stays. Its capacity-attributable footprint is negligible, so lifecycle or compression complexity is unjustified. No plan file exists for this bucket; this paragraph is the record of the closure.

## Texture CPU pool reclaim measurement

The five-cycle exact control cohort matched all 20/20 CRC/path/size checks and covered **766,933,000 decompressed bytes (731.4043 MiB)**, with **766,769,160 bytes (731.2481 MiB)** in conservative page interiors. With unconditional reclaim, working-set median moved **766,332,928 → 780,390,400 bytes (+13.406 MiB)**, **748.711 MiB less growth than the baseline control**; private median moved **5,296,390,144 → 4,497,997,824 bytes (−761.406 MiB)**, finishing **820.840 MiB below baseline cycle 5**. A forced revisit reloaded the same four CRCs after a 1,839-tick drive-away and 66-key bounded wrap with no errors or corruption; screenshots were populated.

## Architecture findings (why the naive strategies don't work)

Verified against source; these drive the decomposition:

1. **The GPU mesh is bound in the record-once command buffer.** `Managers/CommandBufferRecordMain.cpp:405-406` asserts each template's `mMeshBuffer.mDeviceLocalVkBuffer` is live and binds it at record time via `Buffer::RecordBindVertexBuffer` (`Objects/Buffer.cpp:275`); CB re-record is banned (`Graphics/AGENTS.md`). Freeing it the way the texture LRU frees textures (bindless descriptor repatch) is impossible — a directly-bound vertex/index buffer has no indirection to patch. → `IslandMeshArenaResidency.md`.
2. **The lazy CPU payload uses one monolithic reserved pool with selectively committed pages — texture reclaim landed.** Option C unconditionally decommits each adopted texture chunk's page interior in `TextureManager::AdoptUploadedChunk` after transfer completion, when no concurrent reader remains. `PackChunks::LoadChunk` recommits the chunk's full pool allocation before any raw-read or decompression write on a later reload; pool layout and boundary pages remain stable. → `TextureChunkCpuPoolReclaim.md` (removed — landed).
3. **The heightmap and hull are deterministic sim dependencies on BOTH builds** — `mpHeightmapHalf` is read per-tick by `BuildElevationGrid` (`IslandTerrain.cpp:416`, dequantizing each R16 texel via `XMConvertHalfToFloat` at `:407`) on client and server, plus render (`GlobalElevation`, `:310`) and the server NavContour build (`:198-210`); the hull feeds deterministic placement (`IslandChainPlacement.cpp:170`). Because both builds consume it every tick, an evict/reload lifecycle would have needed a sim-coordinated, both-builds, byte-identical reload-on-access model — **not taken**. Instead the heightmap is **compressed in place**: quantized to R16 half-float (landed), permanently resident at half the former size. Any further reduction is content-addressed dedup of byte-identical heightmap regions — still permanently resident and byte-identical, no eviction. → `IslandHeightmapRouteDedup.md`.
4. **Only the mesh CPU slice was dead after boot — reclaimed (landed).** Read once by `CreateClientMeshBuffers` to upload the GPU buffer, then never again, and never read at all on the server. The client now decommits the `[positions][indices]` sub-range immediately after the upload (`IslandTerrainResidency.cpp:67`) and recommits+reloads it from disk when `CreateClientMeshBuffers` re-runs on device-loss recovery (`Islands` ctor, `Graphics/Islands.cpp:20`; gate at `IslandTerrainResidency.cpp:36-47`); the server decommits it immediately after chunk load (`IslandTerrain.cpp:193`). The plan that delivered this (`IslandMeshCpuSliceReclaim.md`) completed and its file was removed.

## The series

| Plan | Bucket | Effort | Status | One-liner |
|---|---|---|---|---|
| `IslandMeshCpuSliceReclaim.md` (removed — landed) | mesh CPU 119.5 MiB | Medium | **Landed** | The clean, determinism-free win — decommit the dead mesh slice; reload on device-loss recovery. Delivered the shared `DecommitChunkRange`/`RecommitAndReloadChunkRange` infrastructure. |
| `IslandMeshArenaResidency.md` | mesh GPU 119.5 MiB | Large | Open | Stable-handle mesh arena (VMA virtual-block sub-allocation) under the record-once-CB constraint; option decided in-plan. |
| `IslandHeightmapRouteDedup.md` | heightmap ~90.75 MiB (R16) | Large | Open, speculative | Deferred dedup half of the R16 plan: content-address byte-identical heightmap regions shared across routes/leaves and alias one pool region from many templates. Profile first; likely closes accept-and-document (leaves are disjoint tiles, routes are independent bakes). |
| `IslandPlacementSsboResidency.md` (removed — landed) | SSBO 16.4 MiB | Medium | **Landed** | Option C: retain the fixed arena; correct the four-framebuffer 16.40625 MiB accounting and accept the smallest residency bucket. |
| `TextureChunkCpuPoolReclaim.md` (removed — landed) | texture lazy-chunk CPU pool (not in the island table above) | Medium | **Landed** | Option C: unconditionally decommit after transfer-complete adoption; recommit the full allocation before `LoadChunk` writes on revisit. |
| *(no file)* NavContour | NavContour (server), 66.40 KiB capacity-attributable at 70 templates | — | **Closed by measurement** | Eager server `mNavContour` residency stays; lifecycle/compression complexity is unjustified. |

Recommended sequence for the remaining open plans: GPU mesh arena remains independent; heightmap route-dedup is a speculative profile-first decision plan. The heightmap's biggest single win (R16 compression in place), mesh-CPU reclaim, texture-pool reclaim, and placement-SSBO residency decision have landed; NavContour is closed by measurement.

## Instrumentation

Temporary `[DEBUG-resmem]` boot instrumentation drove the measurements above and was removed after capture. Re-add it only when a later decision needs a fresh measurement.
