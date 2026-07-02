# Island Resident-Memory Scaling — Overview

**Reference / Index document — not an executable plan.** Anchors the series of plans that bound per-template island resident memory. Records the boot measurement and the architecture findings that shaped the decomposition, so each sub-plan doesn't re-derive them.

## The problem

Island 01's multi-island route table raised the per-template count ~9× to **70 templates** (`kiMaxIslands` bumped 64→128 as a stopgap). Several per-template allocations stay **permanently resident, scaling with total template count** rather than with concurrent on-screen/simulated residency, and escape the texture LRU (which manages only the GPU G-buffer / elevation images).

## Measurement (`[DEBUG-resmem]` boot capture, 70 templates)

| Bucket | Size | Residency |
|---|---|---|
| heightmap (`mpHeightmapHalf`, R16 half-float) | ~90.75 MiB | CPU, lazy pool |
| mesh CPU (`mpfMeshPositions`/`mpuiMeshIndices`) | 119.5 MiB | CPU, lazy pool |
| mesh GPU (`mMeshBuffer`) | 119.5 MiB | VRAM |
| SSBO placement arena (`mIslandsStorageBuffers`) | 16.4 MiB | VRAM (host-visible) |
| hull (`mpf2ValidAreaVertices`) | 22 KiB | CPU, lazy pool |
| **Total** | **~346 MiB** | — |

The heightmap bucket is stored as R16 IEEE half-float (2 bytes/texel, dequantized to float on CPU read via `XMConvertHalfToFloat`), so it sits at ~half its former R32 figure (~181.5 MiB → ~90.75 MiB). Headroom to the `kiMaxIslands=128` cap: ~1.8× → ~630 MiB if the route table grows toward it. The distribution is heavily skewed — the top ~3 islands carry ~25 MiB each; dozens of small islands are <1 MiB — so a concurrent-residency bound pays off disproportionately.

## Architecture findings (why the naive strategies don't work)

Verified against source; these drive the decomposition:

1. **The GPU mesh is bound in the record-once command buffer.** `Managers/CommandBufferRecordMain.cpp:261-263` binds every template's `mMeshBuffer` at record time (`Objects/Buffer.cpp:285,288`); CB re-record is banned (`Graphics/CLAUDE.md`). Freeing it the way the texture LRU frees textures (bindless descriptor repatch) is impossible — a directly-bound vertex/index buffer has no indirection to patch. → `IslandMeshArenaResidency.md`.
2. **The lazy CPU payload is one monolithic committed `VirtualAlloc(MEM_RESERVE|MEM_COMMIT)` pool** (`FileManager.cpp:367`), freed wholesale only in `~FileManager` (`:99`). There is **no per-chunk release API**; the texture LRU frees only VRAM + resets chunk state for reload (`ResetTextureChunkStates`, skips non-texture chunks), never decommits the pool. Reclaiming CPU payload needs new decommit infrastructure.
3. **The heightmap and hull are deterministic sim dependencies on BOTH builds** — `mpHeightmapHalf` is read per-tick by `BuildElevationGrid` (`IslandTerrain.cpp:355`, dequantizing each R16 texel via `XMConvertHalfToFloat`) on client and server, plus render (`GlobalElevation` `:267`) and the server NavContour build (`:199`); the hull feeds deterministic placement (`IslandChainPlacement.cpp:170`). Because both builds consume it every tick, an evict/reload lifecycle would have needed a sim-coordinated, both-builds, byte-identical reload-on-access model — **not taken**. Instead the heightmap is **compressed in place**: quantized to R16 half-float (landed), permanently resident at half the former size. Any further reduction is content-addressed dedup of byte-identical heightmap regions — still permanently resident and byte-identical, no eviction. → `IslandHeightmapRouteDedup.md`.
4. **Only the mesh CPU slice is dead after boot** — read once in `CreateClientMeshBuffers` to upload the GPU buffer, then never again, and never read at all on the server (no sim exposure). But `CreateClientMeshBuffers` **re-runs on device-loss recovery** (`Islands` ctor, `Islands.cpp:21`), so any reclaim must reload it there. → `IslandMeshCpuSliceReclaim.md`.

## The series

| Plan | Bucket | Tier | One-liner |
|---|---|---|---|
| `IslandMeshCpuSliceReclaim.md` | mesh CPU 119.5 MiB | Medium | The clean, determinism-free win — decommit the dead mesh slice; reload on device-loss recovery. |
| `IslandMeshArenaResidency.md` | mesh GPU 119.5 MiB | Large | Stable-handle arena or bindless vertex-pull (record-once-CB constraint). |
| `IslandHeightmapRouteDedup.md` | heightmap ~90.75 MiB (R16) | Large | Deferred dedup half of the R16 plan: content-address byte-identical heightmap regions shared across routes/leaves and alias one pool region from many templates. Speculative — profile first; likely closes accept-and-document (leaves are disjoint tiles, routes are independent bakes). |
| `IslandPlacementSsboResidency.md` | SSBO 16.4 MiB | Medium | Compact/share the per-template placement arena; smallest bucket. |
| `IslandNavContourResidency.md` | NavContour (server) | Medium | Server-side per-template `mNavContour`; resolves on its own measurement — there is no heightmap eviction lifecycle to attach to (the heightmap is compressed in place, not evicted). |

Recommended sequence: mesh-CPU reclaim first (clean, best value/effort), then GPU arena and SSBO independently; the heightmap is already R16-compressed in place (biggest single win, landed), with route-dedup as a speculative profile-first follow-up, and NavContour resolving on its own server-side measurement.

## Instrumentation

The `[DEBUG-resmem]` LOG block (`IslandTerrainResidency.cpp:47-67`, `kError`-tagged) drove the measurement above. `IslandMeshCpuSliceReclaim.md` removes it on landing; re-add if a later plan needs a fresh capture.
