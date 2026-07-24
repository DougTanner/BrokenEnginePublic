<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T02:29:26.000Z","dependsOn":[]} -->
# Reclaim Texture Lazy-Chunk CPU Pool Bytes

Acknowledged known gap in `Graphics/IslandResidentMemoryScaling_Overview.md` ("the texture LRU frees only VRAM + resets chunk state for reload… never decommits the pool").

**Decision plan.** One design decision (the churn policy, options A/B/C below) is deliberately unresolved and must be settled at `/external-grill-plan` before implementation. Everything else in this plan is final.

## Context

Every `kTexture` `LazyChunk`'s bytes in the monolithic lazy pool (`PackChunks::mpLazyPool`, committed whole at `PackChunks.cpp` `Initialize`'s `VirtualAlloc(MEM_RESERVE | MEM_COMMIT)`) are read exactly once — the staging copy into the GPU image (`TextureUploadManager::RecordStagingCopies`). Immediately after adoption the main thread nulls `pData`/`iDataSize` in `TextureManager::AdoptUploadedChunk` (reached from `ProcessPendingTextures`; the "Race-free null of worker-thread-shared CPU-pool state" block), so the bytes are never read again — **but only the pointer is abandoned; the physical pool pages stay committed for the process lifetime.**

Unlike the island mesh slice (a boot one-shot, since implemented — `IslandTerrain.cpp` already calls `DecommitChunkRange` for the mesh sub-range), texture chunks are lazy, on-demand, and **LRU-cycled at steady state**: the load→adopt→evict→re-acquire→reload cycle turns continuously as the camera pans (`IslandTerrainResidency.cpp` `IslandTerrain::FirstMintTextureSlot`/`AcquireTextureSlot`/`EvictTemplate` → `FileManager::ResetTextureChunkStates(evictCrcs)` → `PackChunks::LoadChunk` disk re-read). That is why the mesh plan's extension-review gate rejected folding this in: a decommit here runs inside the main-loop steady state, not at boot.

## Design

Reuse the existing lazy-pool decommit/recommit machinery: the `FileManager` facade (`FileManager.h` `DecommitChunkRange`/`RecommitAndReloadChunkRange`) forwarding to `PackChunks::DecommitChunkRange`/`RecommitAndReloadChunkRange` (`PackChunks.cpp`). Two mechanical pieces, common to both implementing options (B and C; option A implements neither and instead documents the accepted gap in `IslandResidentMemoryScaling_Overview.md`):

1. **Decommit at the adopt null point.** In `TextureManager::AdoptUploadedChunk`, at the existing `rLazyChunk.pData = nullptr; rLazyChunk.iDataSize = 0;` statements, capture the chunk's data size before nulling and call `gpFileManager->DecommitChunkRange(crc, 0, <captured size>)` (condition per the chosen option). No race: reaching `kGpuUploadComplete` means the transfer thread released ownership — the same ordering invariant the existing null-point comment documents. `DecommitChunkRange` already decommits only the page-aligned interior, leaving boundary pages and the pool reservation intact — its semantics are not changed.
2. **Recommit before reload.** The texture reload path (`ResetTextureChunkStates` sets `kNotLoaded` → loader thread `PackChunks::LoadChunk` writes straight into `rLazyChunk.pData`) performs **no `MEM_COMMIT`** — a reload of a decommitted range would fault on the loader thread. Thread a `MEM_COMMIT` of the chunk's page-aligned interior (`PAGE_READWRITE`, same alignment math and soft-fail-on-null pattern as `RecommitAndReloadChunkRange`) into that reload path before `LoadChunk`'s first write into `pData`. Do **not** route texture reloads through `RecommitAndReloadChunkRange` itself — its direct uncompressed disk re-read duplicates and conflicts with `LoadChunk`'s owned re-read (including decompression); only its `MEM_COMMIT` step is needed, extracted into a small `PackChunks` helper if that avoids duplication. Exact call site (`LoadChunk` entry for `kTexture` chunks vs. `ResetTextureChunkStates`) is confirmed at grill.

### Open decision — churn policy (resolve at grill; do not implement before it is chosen)

The open question is the churn tradeoff, so this is a decision plan:
- **A — accept + document** (likely for rapidly cycling sets): the menu island browser cycles islands repeatedly (`IslandTerrainResidency.cpp` anticipates this); per-cycle `MEM_DECOMMIT`/`MEM_COMMIT` syscalls + page-faults + zero-fill may cost more than the resident-set win.
- **B — decommit-on-adopt with a grace/threshold**: only decommit chunks that stay adopted past a churn threshold, so stably-visible islands reclaim while cycling ones don't thrash. If chosen, the threshold state and its owner must be fully specified in the grill refinement before implementation.
- **C — decommit unconditionally on adopt**: simplest; measure whether the churn cost is acceptable.

Profile the resident texture-CPU-pool bytes and the cycling churn cost before committing to an option. Additional grill confirmations: the recommit call site (above), and that `ReadChunkData` serves an adopted/decommitted texture chunk from disk (its resident-pool branch must not dereference a nulled or decommitted `pData`).

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change implementing the grill-chosen option, and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission beyond the named regions plus the mechanical necessities (includes, declarations) they require.

**In scope:**
- `Engine/Source/Graphics/Managers/TextureManager.cpp` — `TextureManager::AdoptUploadedChunk` only: the decommit call at the existing `pData`/`iDataSize` null point, plus whatever per-chunk condition the chosen option requires (option B's threshold bookkeeping only as specified by the grill refinement).
- `Engine/Source/File/PackChunks.h` / `PackChunks.cpp` — the recommit `MEM_COMMIT` in the texture reload path (`LoadChunk` and/or `ResetTextureChunkStates` per grill confirmation), optionally as one new private page-interior-commit helper shared with `RecommitAndReloadChunkRange`'s existing commit step.
- `Engine/Source/File/FileManager.h` / `FileManager.cpp` — only a mechanical facade forward if the recommit needs one; no other changes.

**Out of scope:**
- The island mesh CPU slice (already implemented; `IslandTerrain.cpp`/`IslandTerrainResidency.cpp` mesh `DecommitChunkRange`/`RecommitAndReloadChunkRange` callers unchanged) and the other island residency buckets.
- Eager-pack (`mPackFileData`) reclaim — different storage mechanism.
- The `ProcessPendingTextures` same-queue-family fallback adopt branch (`kDiskLoaded` path): it does not null `pData` today and stays untouched.
- `DecommitChunkRange`/`RecommitAndReloadChunkRange` semantics, audio streaming reads, allocation-tracking policy, and the descriptor/eviction lifecycle in `Graphics/Managers`.

## Critical files
- `Engine/Source/Graphics/Managers/TextureManager.cpp` — `AdoptUploadedChunk` null point; `ProcessPendingTextures`.
- `Engine/Source/File/PackChunks.h` / `PackChunks.cpp` — `mpLazyPool`, `DecommitChunkRange`, `RecommitAndReloadChunkRange`, `ResetTextureChunkStates`, `LoadChunk` (recommit before disk re-read).
- `Engine/Source/File/FileManager.h` / `FileManager.cpp` — facade forwards.
- `Engine/Source/Frame/IslandTerrainResidency.cpp` — LRU cycle context (read-only for this plan).
- `Engine/Source/Graphics/Managers/AGENTS.md` — eviction-symmetry / drained descriptor-patch-window invariants (must not be disturbed).

## Risk tier and invariants

Tier 3 trigger: steady-state **main-loop** VM decommit/recommit interacting with the texture-LRU eviction lifecycle and cross-thread loader writes — declare at grill (the reason this is a separate plan rather than a fold). Invariants to preserve:
- Lazy-pool layout (`File/AGENTS.md`): cumulative offsets and chunk pointers never move; only page-aligned interiors decommit; boundary pages stay committed; no concurrent reader of a decommitted range.
- Loader threads write only committed pages: every path that re-reads a decommitted texture chunk recommits first.
- Eviction symmetry and the drained descriptor-patch window (`Graphics/Managers/AGENTS.md`) are untouched.
- Allocation tracking: `AdoptUploadedChunk` runs inside the tracked main loop — `VirtualFree(MEM_DECOMMIT)` is not a heap allocation, but any option-B bookkeeping that allocates needs the established suppression pattern.
- Client/graphics-only. **No determinism/CRC/`.pack`/`kiVersion` exposure** — texture CPU bytes never feed the sim.

## Acceptance criteria
- Measured resident-set (working-set) reduction covering adopted texture chunks under the chosen option, from the pre-decision profiling.
- Harness scenario: pan the camera to drive island evict→re-acquire cycles; textures reload without faults or corruption, and logs show the evict/reset/reload cycle completing (`kLoading`/`kGraphics` verbose logs already present in `EvictTemplate`).
- No behavior change on the untouched fallback adopt path or mesh residency paths (diff-decisive).
