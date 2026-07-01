# Island GPU Mesh Residency (Stable-Handle Arena)

**Decision plan (present options).** Follow-up split out of `IslandResidentMemoryScalingStrategy.md`. When the island resident-memory buckets were triaged, the CPU payload went to mesh-CPU-slice reclaim (`IslandResidentMemoryScalingStrategy.md`) + heightmap residency (`IslandHeightmapResidency.md`); this plan owns the **GPU** mesh side that strategy (a2) targeted.

## Context

The `[DEBUG-resmem]` boot capture (70 templates) measured **~119.5 MiB** of permanently-resident per-template GPU mesh (`IslandTemplate::mMeshBuffer`, `IslandTerrain.h:97`), scaling with total template count (not concurrent residency), with ~1.8× headroom to the `kiMaxIslands=128` cap.

The naive "free `mMeshBuffer` in `EvictionSweep`" approach that strategy (a2) proposed is **infeasible as written**: the per-template mesh is **bound directly into the record-once terrain command buffer** — `Managers/CommandBufferRecordMain.cpp:261-263` asserts `mMeshBuffer.mDeviceLocalVkBuffer != VK_NULL_HANDLE`, then `RecordBindVertexBuffer` (`Objects/Buffer.cpp:285,288` → `vkCmdBindIndexBuffer` + `vkCmdBindVertexBuffers`), then `vkCmdDrawIndexedIndirect`. `IslandTerrainResidency.cpp:198-200` documents the invariant verbatim. Destroying the buffer while the resubmitted-every-frame CB still references it is a Vulkan lifetime violation (VUID-vkDestroyBuffer-buffer-00922). The texture LRU's eviction works only because textures are **bindless** (a patchable descriptor slot, no direct CB reference); the mesh has no such indirection. CB re-record on eviction churn is banned (`Graphics/CLAUDE.md`).

## Design

Two viable approaches to make per-template GPU mesh evictable without breaking the record-once CB — present both, pick one at grill:

- **A — Persistent mesh arena with a stable `VkBuffer` handle + sub-allocation.** One (or a small pool of) long-lived device-local buffer(s) whose handle never changes, so the recorded `vkCmdBindVertexBuffers` stays valid. Per-template mesh data occupies a sub-range; eviction frees the *sub-range* (memory reclaimed via a suballocator) and degenerates the draw (`indexCount = 0` in the per-template indirect entry, `Islands.cpp`); restoration re-uploads into a sub-range and rewrites the indirect entry. The terrain draw binds the arena buffer once (per template, same handle) and uses `firstIndex`/`vertexOffset` to address the sub-range. Frees VRAM without touching the CB. Main complexity: a device-local mesh suballocator with fragmentation handling, and per-template `firstIndex`/`vertexOffset` bookkeeping.
- **B — Bindless vertex-pull.** Move island vertex/index data into a persistent SSBO read by `Terrain.vert` via a patchable per-template slot (mirroring the bindless texture arrays). Eviction patches the slot to a placeholder; the CB binds nothing per-template. Larger shader + pipeline + DataPacker-mesh-layout change, but conceptually uniform with the existing texture LRU.

Both bound GPU mesh residency by *concurrent residency* (the same set the texture LRU and the (b) chunk lifecycle track), not total template count.

## Critical files

- `Engine/Source/Frame/IslandTerrainResidency.cpp` — `CreateClientMeshBuffers` (`:10`), `EvictionSweep` (`:278`), `RestorationSweep` (`:378`), `ReleaseGpuResources` (`:432`).
- `Engine/Source/Frame/IslandTerrain.h` — `IslandTemplate::mMeshBuffer` (`:97`), mesh CPU pointers (`:95-96`).
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — terrain per-template bind + draw (`:261-263`).
- `Engine/Source/Graphics/Objects/Buffer.cpp` — `RecordBindVertexBuffer` (`:285,288`).
- `Engine/Source/Graphics/Islands.cpp` / `.h` — per-template indirect entries (`indexCount`/`firstIndex`/`vertexOffset`, baked `:62-69`).
- `Engine/Data/Shaders/Terrain/Terrain.vert` — only under approach **B** (vertex-pull rework).
- `Engine/Source/Frame/CLAUDE.md` / `Engine/Source/Graphics/CLAUDE.md` — residency + record-once-CB documentation.

## Out of scope

- CPU chunk payload (heightmap + mesh CPU slice + hull) — owned by the (b) lazy-chunk plan (`IslandResidentMemoryScalingStrategy.md`).
- The `kiMaxIslands` cap value.
- NavContour residency (`IslandNavContourResidency.md`).
- The per-template SSBO `mIslandsStorageBuffers` (~16 MiB placement arena) — a separate, smaller concern.

## Acceptance criteria

- Per-template GPU mesh VRAM is bounded by concurrent residency, not template count.
- The record-once CB invariant is preserved (no per-eviction re-record; bound handles stay valid).
- `Frame/CLAUDE.md` / `Graphics/CLAUDE.md` updated to reflect the chosen mesh-residency mechanism.

## Notes

- Client/graphics-only. No `kiVersion`/`.pack`/CRC/wire/determinism exposure (approach B changes the DataPacker island mesh *layout* but not its CRC contract — confirm at grill).
- **Decision plan**: approach A (arena) vs B (bindless vertex-pull) is the single open decision for `/external-grill-plan`.
- Coordinate with the (b) lazy-chunk plan's eviction/restoration timing (both hook `EvictionSweep`/`RestorationSweep`) — sequence, don't interleave.
