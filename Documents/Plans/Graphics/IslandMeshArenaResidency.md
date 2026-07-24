<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-01T01:46:38.000Z","dependsOn":[]} -->
# Island GPU Mesh Residency (Stable-Handle Arena)

Part of the island resident-memory scaling series — see `IslandResidentMemoryScaling_Overview.md`. This plan owns the **GPU** mesh side: the ~119.5 MiB of permanently-resident per-template `mMeshBuffer` memory that the texture-LRU evict path cannot touch because the buffers are bound into the record-once command buffer.

## Context

The `[DEBUG-resmem]` boot capture (70 templates) measured **~119.5 MiB** of permanently-resident per-template GPU mesh (`IslandTemplate::mMeshBuffer`, `Engine/Source/Frame/IslandTerrain.h:99`), scaling with total template count (not concurrent residency), with ~1.8× headroom to the `kiMaxIslands=128` cap.

Naively freeing `mMeshBuffer` in `EvictionSweep` is infeasible: the per-template mesh is **bound directly into the record-once terrain command buffer** — `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp:405-407` asserts `mMeshBuffer.mDeviceLocalVkBuffer != VK_NULL_HANDLE`, then calls `RecordBindVertexBuffer` (`Engine/Source/Graphics/Objects/Buffer.cpp:275-282` → `vkCmdBindIndexBuffer` + `vkCmdBindVertexBuffers`), then `vkCmdDrawIndexedIndirect`. Destroying the buffer while the resubmitted-every-frame CB still references it is a Vulkan lifetime violation (VUID-vkDestroyBuffer-buffer-00922). The texture LRU's eviction works only because textures are **bindless** (a patchable descriptor slot, no direct CB reference); the mesh has no such indirection. CB re-record on eviction churn is banned (`Engine/Source/Graphics/AGENTS.md`). The fix is to make the bound handle *stable* (one arena buffer) and move the per-template variation into the already-host-visible indirect commands.

### Decision record

**Decision (2026-07-03): chose A (persistent mesh arena, stable `VkBuffer` handle + VMA virtual-block sub-allocation) over B (bindless vertex-pull SSBO).** Rationale: (1) A's per-template addressing channel already exists and is the sanctioned pattern — the per-template `VkDrawIndexedIndirectCommand`s live in a host-visible mapped buffer (`Engine/Source/Graphics/Islands.cpp:54-77`) whose `instanceCount` is already rewritten every frame, and `Graphics/AGENTS.md` mandates exactly this channel for per-frame variation; `firstIndex`/`vertexOffset` are two more fields in the same mapped write. (2) A's headline cost — "write a device-local suballocator with fragmentation handling" — is off the shelf: the VMA the engine builds against (SDK 1.4.341.1, `vma/vk_mem_alloc.h`, included via `Common/ExternalHeaders.h:262`) ships the virtual-allocation API (`vmaCreateVirtualBlock`/`vmaVirtualAllocate`/`vmaVirtualFree`), purpose-built for sub-allocating one large buffer. (3) B's blast radius is far larger — `Terrain.vert` rework, pipeline vertex-input mode change (terrain is the reflection-driven per-draw vertex-buffer path, `Objects/AGENTS.md`), DataPacker mesh-layout change with a `.pack`/`kiVersion` bump — and it invalidates `TerrainMeshLodChain.md`, which is designed around `meshopt_simplify` index chains over the *existing* vertex-buffer layout with indirect `indexCount`/`firstIndex` selection; A composes with that plan directly (their `firstIndex` gains an arena base term). (4) B's "uniform with the texture LRU" appeal is aesthetic: the LRU *policy* (refcount, grace clock, `EvictionSweep`/`RestorationSweep` in the drained window) is reused identically under A — only the reclamation mechanism differs, as it must for a non-descriptor resource.

Decided parameters (no implementer choice remains): `kiIslandMeshArenaBytes` = 64 MiB; one shared arena — indices and vertices sub-allocated from one `VmaVirtualBlock` (no split index/vertex arenas, no split ratio); sub-allocation alignments 4 (index slices) and 8 (vertex slices).

## Design

**Arena.** One long-lived device-local `VkBuffer` ("island mesh arena", `kIndexVertex` usage), created empty at `Islands` ctor time (before CB record), capacity the named constant `kiIslandMeshArenaBytes` (64 MiB; must exceed the largest single template mesh, ~25 MiB). Sub-allocation via one `VmaVirtualBlock` over the buffer's size (virtual blocks are standalone bookkeeping — no interaction with `gpDeviceManager->mpAllocator`). Each resident template holds two sub-ranges from the same block: its index slice (alignment 4) and its vertex slice (alignment 8). The recorded CB binds the arena **once** before the per-template draw loop — `vkCmdBindIndexBuffer(arena, 0, VK_INDEX_TYPE_UINT32)` + `vkCmdBindVertexBuffers(arena, offset 0)` — replacing the per-template `RecordBindVertexBuffer` call and the per-template `ASSERT` (`CommandBufferRecordMain.cpp:405-406`); the one-indirect-draw-per-template loop (`CommandBufferRecordMain.cpp:407`) is unchanged. Record-once is preserved: the handle never changes for the life of the `Graphics` instance.

**Addressing.** Per-template mesh location lives in the indirect commands: `firstIndex = indexSliceByteOffset / 4`, `vertexOffset = vertexSliceByteOffset / 8` (vertex stride `2 * sizeof(float)` — interleaved XY pairs, `IslandTerrain.h:97`), `indexCount = miMeshIndexCount` when resident, `0` when not. The boot bake in the `Islands` ctor (`Islands.cpp:54-77`) changes to initialize `indexCount = 0` for every template (nothing is mesh-resident at boot) instead of baking `miMeshIndexCount`. Residency transitions rewrite these fields in **all `kiMaxFramebuffers` instances** of the mapped indirect buffer — safe because both sweeps run inside `RenderGlobal`'s post-fence-wait drained window on churn frames (the same window that makes the descriptor rewrites safe). `UpdateActiveIslands` continues rewriting only `instanceCount` per frame.

**Template state.** `IslandTemplate` (`IslandTerrain.h`) drops `mMeshBuffer` (`:99`) and gains: the two `VmaVirtualAllocation` handles + byte offsets, and a small mesh-residency state (non-resident → CPU-load-pending → resident). Mesh residency rides the existing texture lifecycle: requested at the same point the template's texture chunks are requested, torn down in `EvictTemplate`.

**Restoration (upload path).** The mesh CPU slice in the kIsland chunk pool is decommitted after upload (existing `DecommitChunkRange` infra, `Engine/Source/Frame/IslandTerrainResidency.cpp:67`), so each restoration must re-source it from disk **asynchronously** — never a synchronous multi-MiB read on the render thread. Extend `FileManager` with an async variant of `RecommitAndReloadChunkRange` (`Engine/Source/File/FileManager.cpp:252`; queued to the existing background chunk-load thread; completion flag polled by `RestorationSweep`, mirroring how texture chunks reach `kReady`). `RestorationSweep` (`IslandTerrainResidency.cpp:302`) then, inside the drained window: `vmaVirtualAllocate` the two slices (see exhaustion policy below), staging-copy the `[indices]` and `[positions]` payloads into the arena at their offsets via a `OneShotCommandBuffer` copy (add a small `Buffer`/arena helper for copy-at-offset; reuse the existing staging pattern), re-decommit the CPU slice, write `firstIndex`/`vertexOffset`/`indexCount` into all indirect instances, mark resident. Until then the template draws nothing (`indexCount = 0`) — the mesh analog of the texture placeholder window; frame 0 already renders no islands.

**Eviction.** `EvictTemplate` (`IslandTerrainResidency.cpp:241`), when the template is mesh-resident: write `indexCount = 0` into all indirect instances, `vmaVirtualFree` both slices, clear mesh state. No buffer destroy, no CB touch — the arena handle stays bound and valid; the freed region is inert because nothing addresses it.

**Arena exhaustion.** If `vmaVirtualAllocate` fails at restoration: force-evict the least-recently-used mesh-resident template with `miRefCount == 0` (reuse the existing LRU bookkeeping — `muiLastUsedRenderFrame`, `IslandTerrain.h:87`) and retry; if none is evictable, skip this template's restore this sweep and retry next churn frame. Log `kWarning` on the skip path (sustained skipping means `kiIslandMeshArenaBytes` is undersized for the scene).

**Boot & device loss.** `CreateClientMeshBuffers` (`IslandTerrainResidency.cpp:10`) is retired as the eager all-templates upload; its recommit/reload + decommit mechanics move into the restoration path. `ReleaseGpuResources` (`IslandTerrainResidency.cpp:328`) destroys the arena buffer + virtual block instead of the per-template `mMeshBuffer` loop and clears every template's mesh state; `Graphics` recreation recreates an empty arena and restoration repopulates on demand. The boot-order invariant "elevation maps complete before the `Graphics` ctor because the record-once CB needs the CPU mesh pointers" relaxes to needing only the arena buffer — update the `Engine/Source/AGENTS.md` boot-order bullet and the `Engine/Source/Frame/AGENTS.md` "mesh stays permanently resident" paragraph accordingly.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the acceptance criteria, and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants permission to touch only the named functions/members/regions plus the mechanical necessities (includes, forward declarations, member declarations) those named changes require.

### In scope (exact regions)

- `Engine/Source/Frame/IslandTerrainResidency.cpp`
  - `CreateClientMeshBuffers` (`:10`) — retired as the eager all-templates upload; recommit/decommit mechanics relocate into the restoration path.
  - `EvictTemplate` (`:241`) — add the mesh-resident teardown branch (indirect zero, `vmaVirtualFree`, state clear).
  - `EvictionSweep` (`:289`) — only as needed to route mesh teardown through `EvictTemplate`.
  - `RestorationSweep` (`:302`) — add the async-load poll + arena allocate + staging upload + indirect rewrite sequence.
  - `ReleaseGpuResources` (`:328`) — replace the per-template `mMeshBuffer` destroy loop with arena + virtual-block destroy and mesh-state clear.
- `Engine/Source/Frame/IslandTerrain.h` — `IslandTemplate`: remove `mMeshBuffer` (`:99`); add the two `VmaVirtualAllocation` handles + byte offsets and the mesh-residency state enum/member. `mpfMeshPositions`/`mpuiMeshIndices` (`:97-98`), `miMeshIndexCount` (`:70`), and `mbMeshCpuDecommitted` (`:102`) are read/retained, not redesigned.
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — terrain draw loop only (`:395-407`): delete the per-template `ASSERT` + `RecordBindVertexBuffer` (`:405-406`), add the single arena bind before the loop; the `vkCmdDrawIndexedIndirect` call (`:407`) is unchanged.
- `Engine/Source/Graphics/Objects/Buffer.cpp` / `Buffer.h` — remove the terrain call site's use of `RecordBindVertexBuffer` (`Buffer.cpp:275-282` itself stays; other callers unchanged); add one copy-at-offset arena upload helper following the existing staging pattern.
- `Engine/Source/Graphics/Islands.cpp` / `Islands.h` — ctor: arena buffer + `VmaVirtualBlock` creation and ownership, indirect bake `indexCount = 0` (`Islands.cpp:54-77`); add the residency-transition indirect rewrite entry point used by the sweeps. `UpdateActiveIslands` (`Islands.cpp:95`) is untouched.
- `Engine/Source/File/FileManager.cpp` / `FileManager.h` — add the async variant of `RecommitAndReloadChunkRange` (`FileManager.cpp:252`, `FileManager.h:153`) on the existing background chunk-load thread, plus whatever `Engine/Source/File/PackChunks.h`/`.cpp` plumbing that variant mechanically requires (`PackChunks.cpp:849`).
- `Engine/Source/AGENTS.md`, `Engine/Source/Frame/AGENTS.md`, `Engine/Source/Graphics/AGENTS.md` — boot-order, mesh-residency, and record-once-CB documentation updates only.

### Out of scope

- The mesh **CPU** slice reclaim itself — already landed (`DecommitChunkRange`/`RecommitAndReloadChunkRange` usage, `IslandTerrainResidency.cpp:36-67`); this plan only moves when those calls run.
- Heightmap memory (compressed in place to R16; dedup follow-up is `IslandHeightmapRouteDedup.md`).
- The `kiMaxIslands` cap value.
- NavContour residency.
- The per-template SSBO `mIslandsStorageBuffers` (~16 MiB placement arena) — `IslandPlacementSsboResidency.md`.
- Terrain mesh LOD (`TerrainMeshLodChain.md`) — composes with this plan (see Notes) but lands separately.
- Any shader, pipeline vertex-input, DataPacker, or `.pack` layout change (rejected option B).

## Risk tier

**Tier 3** — spans independently owned subsystems (Frame residency, Graphics command recording, File async loading) and adds background-thread work. Invariants the change must preserve:

- Record-once CB: one boot-time record; the bound arena handle never changes for the life of the `Graphics` instance; no re-record on eviction churn.
- All multi-framebuffer indirect-command rewrites happen only inside `RenderGlobal`'s post-fence-wait drained window (churn frames) — same discipline as descriptor patches.
- No synchronous disk read on the render thread.
- No `kiVersion`/`.pack`/CRC/wire/determinism exposure — the on-disk mesh payload layout is untouched and the server never reads the mesh. Client/graphics-only.

## Acceptance criteria

- Per-template GPU mesh VRAM is bounded by `kiIslandMeshArenaBytes` (concurrent residency), not template count.
- The record-once CB invariant is preserved: one boot-time record, arena handle stable, no per-eviction re-record.
- No synchronous disk read on the render thread in the restoration path.
- Evicted-then-revisited templates re-restore and draw correctly; non-resident templates draw nothing (`indexCount = 0`), matching the texture placeholder window behavior.
- Arena exhaustion follows the stated policy: LRU force-evict of a `miRefCount == 0` mesh-resident template, else skip-and-retry with a `kWarning` log.
- `Engine/Source/AGENTS.md` / `Frame/AGENTS.md` / `Graphics/AGENTS.md` updated to reflect arena mesh residency and the relaxed boot-order invariant.

## Coordination

- `Documents/Plans/Graphics/TerrainMeshLodChain.md`: never interleave indirect mesh-layout bookkeeping; TerrainMeshLodChain's structured dependency requires this resolved arena design first.

## Notes

- **Sequencing with `TerrainMeshLodChain.md`:** both rewrite indirect `indexCount`/`firstIndex` bookkeeping — resolve ordering at scheduling time, never interleave. Composition is direct: under both, `firstIndex = arenaIndexSliceOffset/4 + lodIndexOffset` and the per-frame LOD `indexCount` write reads the template's resident arena base (0-count when non-resident).
- All indirect-command residency rewrites touch every framebuffer instance and are therefore restricted to the `RenderGlobal` drained window (churn-frame fence drain) — same discipline as the descriptor patches; do not move them to per-frame `UpdateActiveIslands`.
