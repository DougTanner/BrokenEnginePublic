# Island GPU Mesh Residency (Stable-Handle Arena)

Part of the island resident-memory scaling series — see `IslandResidentMemoryScaling_Overview.md`. This plan owns the **GPU** mesh side (the ~119.5 MiB per-template `mMeshBuffer` the texture-LRU evict path can't touch — record-once-CB binding).

**Decision (2026-07-03): chose A (persistent mesh arena, stable `VkBuffer` handle + VMA virtual-block sub-allocation) over B (bindless vertex-pull SSBO).** Rationale: (1) A's per-template addressing channel already exists and is the sanctioned pattern — the per-template `VkDrawIndexedIndirectCommand`s live in a host-visible mapped buffer (`Islands.cpp:55-73`) whose `instanceCount` is already rewritten every frame, and `Graphics/AGENTS.md` mandates exactly this channel for per-frame variation; `firstIndex`/`vertexOffset` are two more fields in the same mapped write. (2) A's headline cost — "write a device-local suballocator with fragmentation handling" — is off the shelf: the VMA the engine builds against (SDK 1.4.341.1, `vma/vk_mem_alloc.h`, included via `Common/ExternalHeaders.h:238`) ships the virtual-allocation API (`vmaCreateVirtualBlock`/`vmaVirtualAllocate`/`vmaVirtualFree`), purpose-built for sub-allocating one large buffer. (3) B's blast radius is far larger — `Terrain.vert` rework, pipeline vertex-input mode change (terrain is the reflection-driven per-draw vertex-buffer path, `Objects/AGENTS.md`), DataPacker mesh-layout change with a `.pack`/`kiVersion` bump — and it invalidates `TerrainMeshLodChain.md`, which is designed around `meshopt_simplify` index chains over the *existing* vertex-buffer layout with indirect `indexCount`/`firstIndex` selection; A composes with that plan directly (their `firstIndex` gains an arena base term). (4) B's "uniform with the texture LRU" appeal is aesthetic: the LRU *policy* (refcount, grace clock, `EvictionSweep`/`RestorationSweep` in the drained window) is reused identically under A — only the reclamation mechanism differs, as it must for a non-descriptor resource.

## Context

The `[DEBUG-resmem]` boot capture (70 templates) measured **~119.5 MiB** of permanently-resident per-template GPU mesh (`IslandTemplate::mMeshBuffer`, `IslandTerrain.h:99`), scaling with total template count (not concurrent residency), with ~1.8× headroom to the `kiMaxIslands=128` cap.

Naively freeing `mMeshBuffer` in `EvictionSweep` is infeasible: the per-template mesh is **bound directly into the record-once terrain command buffer** — `Managers/CommandBufferRecordMain.cpp:252-257` asserts `mMeshBuffer.mDeviceLocalVkBuffer != VK_NULL_HANDLE`, then `RecordBindVertexBuffer` (`Objects/Buffer.cpp:285,288` → `vkCmdBindIndexBuffer` + `vkCmdBindVertexBuffers`), then `vkCmdDrawIndexedIndirect`. Destroying the buffer while the resubmitted-every-frame CB still references it is a Vulkan lifetime violation (VUID-vkDestroyBuffer-buffer-00922). The texture LRU's eviction works only because textures are **bindless** (a patchable descriptor slot, no direct CB reference); the mesh has no such indirection. CB re-record on eviction churn is banned (`Graphics/AGENTS.md`). The fix is to make the bound handle *stable* (one arena buffer) and move the per-template variation into the already-host-visible indirect commands.

## Design

**Arena.** One long-lived device-local `VkBuffer` ("island mesh arena", `kIndexVertex` usage), created empty at `Islands` ctor time (before CB record), capacity a named constant (`kiIslandMeshArenaBytes`, default 64 MiB — tunable; must exceed the largest single template mesh, ~25 MiB). Sub-allocation via one `VmaVirtualBlock` over the buffer's size (virtual blocks are standalone bookkeeping — no interaction with `gpDeviceManager->mpAllocator`). Each resident template holds two sub-ranges from the same block: its index slice (alignment 4) and its vertex slice (alignment 8). The recorded CB binds the arena **once** before the per-template draw loop — `vkCmdBindIndexBuffer(arena, 0, VK_INDEX_TYPE_UINT32)` + `vkCmdBindVertexBuffers(arena, offset 0)` — replacing the per-template `RecordBindVertexBuffer` call and the per-template `ASSERT` (`CommandBufferRecordMain.cpp:252-257`); the one-indirect-draw-per-template loop is unchanged. Record-once is preserved: the handle never changes for the life of the `Graphics` instance.

**Addressing.** Per-template mesh location lives in the indirect commands: `firstIndex = indexSliceByteOffset / 4`, `vertexOffset = vertexSliceByteOffset / 8` (stride `2 * sizeof(float)`), `indexCount = miMeshIndexCount` when resident, `0` when not. The boot bake in the `Islands` ctor (`Islands.cpp:62-73`) initializes `indexCount = 0` for every template (nothing is mesh-resident at boot). Residency transitions rewrite these fields in **all `kiMaxFramebuffers` instances** of the mapped indirect buffer — safe because both sweeps run inside `RenderGlobal`'s post-fence-wait drained window on churn frames (the same window that makes the descriptor rewrites safe). `UpdateActiveIslands` continues rewriting only `instanceCount` per frame.

**Template state.** `IslandTemplate` (`IslandTerrain.h`) drops `mMeshBuffer` and gains: the two `VmaVirtualAllocation` handles + byte offsets, and a small mesh-residency state (non-resident → CPU-load-pending → resident). Mesh residency rides the existing texture lifecycle: requested at the same point the template's texture chunks are requested, torn down in `EvictTemplate`.

**Restoration (upload path).** The mesh CPU slice in the kIsland chunk pool is decommitted after upload (existing `DecommitChunkRange` infra, `IslandTerrainResidency.cpp:59`), so each restoration must re-source it from disk **asynchronously** — never a synchronous multi-MiB read on the render thread. Extend `FileManager` with an async variant of `RecommitAndReloadChunkRange` (queued to the existing background chunk-load thread; completion flag polled by `RestorationSweep`, mirroring how texture chunks reach `kReady`). `RestorationSweep` (`IslandTerrainResidency.cpp:386`) then, inside the drained window: `vmaVirtualAllocate` the two slices (see exhaustion policy below), staging-copy the `[indices]` and `[positions]` payloads into the arena at their offsets via a `OneShotCommandBuffer` copy (add a small `Buffer`/arena helper for copy-at-offset; reuse the existing staging pattern), re-decommit the CPU slice, write `firstIndex`/`vertexOffset`/`indexCount` into all indirect instances, mark resident. Until then the template draws nothing (`indexCount = 0`) — the mesh analog of the texture placeholder window; frame 0 already renders no islands.

**Eviction.** `EvictTemplate` (`IslandTerrainResidency.cpp:226`), when the template is mesh-resident: write `indexCount = 0` into all indirect instances, `vmaVirtualFree` both slices, clear mesh state. No buffer destroy, no CB touch — the arena handle stays bound and valid; the freed region is inert because nothing addresses it.

**Arena exhaustion.** If `vmaVirtualAllocate` fails at restoration: force-evict the least-recently-used mesh-resident template with `miRefCount == 0` (reuse the existing LRU bookkeeping — `muiLastUsedRenderFrame`) and retry; if none is evictable, skip this template's restore this sweep and retry next churn frame. Log `kWarning` on the skip path (sustained skipping means `kiIslandMeshArenaBytes` is undersized for the scene).

**Boot & device loss.** `CreateClientMeshBuffers` (`IslandTerrainResidency.cpp:10`) is retired as the eager all-templates upload; its recommit/reload + decommit mechanics move into the restoration path. `ReleaseGpuResources` (`IslandTerrainResidency.cpp:440`) destroys the arena buffer + virtual block instead of the per-template `mMeshBuffer` loop and clears every template's mesh state; `Graphics` recreation recreates an empty arena and restoration repopulates on demand. The boot-order invariant "elevation maps complete before the `Graphics` ctor because the record-once CB needs the CPU mesh pointers" relaxes to needing only the arena buffer — update the `Engine/Source/AGENTS.md` boot-order bullet and the `Frame/AGENTS.md` "mesh stays permanently resident" paragraph accordingly.

## Critical files

- `Engine/Source/Frame/IslandTerrainResidency.cpp` — `CreateClientMeshBuffers` (`:10`, retired/repurposed), `EvictTemplate` (`:226`), `EvictionSweep` (`:364`), `RestorationSweep` (`:386`), `ReleaseGpuResources` (`:440`).
- `Engine/Source/Frame/IslandTerrain.h` — `IslandTemplate::mMeshBuffer` (`:99`) replaced by arena slice state; mesh CPU pointers (`:97-98`), `mbMeshCpuDecommitted` (`:102`).
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — per-template bind + `ASSERT` (`:252-257`) → single arena bind before the loop.
- `Engine/Source/Graphics/Objects/Buffer.cpp` / `.h` — `RecordBindVertexBuffer` (`:281-289`, terrain call site removed; other callers unchanged); new copy-at-offset arena upload helper.
- `Engine/Source/Graphics/Islands.cpp` / `.h` — arena + virtual block ownership; indirect bake `indexCount = 0` (`:62-73`); residency-transition indirect rewrites.
- `Engine/Source/File/FileManager.cpp` / `.h` — async `RecommitAndReloadChunkRange` variant on the background load thread.
- `Engine/Source/AGENTS.md` / `Engine/Source/Frame/AGENTS.md` / `Engine/Source/Graphics/AGENTS.md` — boot-order, mesh-residency, and record-once-CB documentation updates.

## Out of scope

- The mesh **CPU** slice reclaim itself — already landed (`DecommitChunkRange`/`RecommitAndReloadChunkRange`, `IslandTerrainResidency.cpp:36-60`); this plan only moves when those calls run.
- Heightmap memory (compressed in place to R16; dedup follow-up is `IslandHeightmapRouteDedup.md`).
- The `kiMaxIslands` cap value.
- NavContour residency (`IslandNavContourResidency.md`).
- The per-template SSBO `mIslandsStorageBuffers` (~16 MiB placement arena) — `IslandPlacementSsboResidency.md`.
- Terrain mesh LOD (`TerrainMeshLodChain.md`) — composes with this plan (see Notes) but lands separately.

## Acceptance criteria

- Per-template GPU mesh VRAM is bounded by `kiIslandMeshArenaBytes` (concurrent residency), not template count.
- The record-once CB invariant is preserved: one boot-time record, arena handle stable, no per-eviction re-record.
- No synchronous disk read on the render thread in the restoration path.
- `Engine/Source/AGENTS.md` / `Frame/AGENTS.md` / `Graphics/AGENTS.md` updated to reflect arena mesh residency and the relaxed boot-order invariant.

## Notes

- Client/graphics-only. **No `kiVersion`/`.pack`/CRC/wire/determinism exposure** — the mesh payload layout on disk is untouched; the server never reads the mesh.
- **Sequencing with `TerrainMeshLodChain.md`:** both rewrite indirect `indexCount`/`firstIndex` bookkeeping — resolve ordering at scheduling time, never interleave. Composition is direct: under both, `firstIndex = arenaIndexSliceOffset/4 + lodIndexOffset` and the per-frame LOD `indexCount` write reads the template's resident arena base (0-count when non-resident).
- All indirect-command residency rewrites touch every framebuffer instance and are therefore restricted to the `RenderGlobal` drained window (churn-frame fence drain) — same discipline as the descriptor patches; do not move them to per-frame `UpdateActiveIslands`.
- Suggested defaults for grill confirmation: `kiIslandMeshArenaBytes` = 64 MiB; single shared arena (indices + vertices in one virtual block) rather than split index/vertex arenas — avoids guessing a split ratio; alignments 4 (index) / 8 (vertex).
