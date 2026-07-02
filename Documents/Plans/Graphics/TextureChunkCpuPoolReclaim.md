# Reclaim Texture Lazy-Chunk CPU Pool Bytes

Acknowledged known gap in `Graphics/IslandResidentMemoryScaling_Overview.md` ("the texture LRU frees only VRAM + resets chunk state for reload… never decommits the pool").

**Decision plan (present options).**

## Context

Every `kTexture` `LazyChunk`'s bytes in the monolithic lazy pool (`FileManager.cpp:367`, `mpLazyPool`) are read exactly once — the staging copy into the GPU image (`TextureUploadManager::RecordStagingCopies`). Immediately after adoption the main thread nulls the pointer/size (`Graphics/Managers/TextureManager.cpp:591-592`, in the adopt helper reached from `ProcessPendingTextures`), so the bytes are never read again — **but only the pointer is abandoned; the physical pool pages stay committed for the process lifetime.** Unlike the island mesh slice (a boot one-shot), texture chunks are lazy, on-demand, and **LRU-cycled at steady state**: the load→adopt→evict→re-acquire→reload cycle turns continuously as the camera pans (`IslandTerrainResidency.cpp` `FirstMintTextureSlot`/`AcquireTextureSlot`/`EvictTemplate` → `FileManager::ResetTextureChunkStates`). This is why the mesh plan's extension-review gate rejected folding it in: a decommit here runs inside the main-loop steady state, not at boot.

## Design

Reuse the existing `FileManager::DecommitChunkRange`/`RecommitAndReloadChunkRange` lazy-pool API (in `FileManager.{h,cpp}`). Add `DecommitChunkRange` at the post-adoption null point (`TextureManager.cpp:591-592`, where the transfer thread has already released ownership — no race), and thread a `MEM_COMMIT` recommit into the existing reload path (`FileManager::ResetTextureChunkStates` → `LoadChunk`, which today writes straight into `rLazyChunk.pData` with **no `MEM_COMMIT`** — a reload of a decommitted range would fault).

The open question is the churn tradeoff, so this is a decision plan:
- **A — accept + document** (likely for rapidly cycling sets): the menu island browser cycles islands repeatedly (`IslandTerrainResidency.cpp` anticipates this); per-cycle `MEM_DECOMMIT`/`MEM_COMMIT` syscalls + page-faults + zero-fill may cost more than the resident-set win.
- **B — decommit-on-adopt with a grace/threshold**: only decommit chunks that stay adopted past a churn threshold, so stably-visible islands reclaim while cycling ones don't thrash.
- **C — decommit unconditionally on adopt**: simplest; measure whether the churn cost is acceptable.

Profile the resident texture-CPU-pool bytes and the cycling churn cost before committing.

## Critical files
- `Engine/Source/Graphics/Managers/TextureManager.cpp` — adopt helper null point (`:591-592`); `ProcessPendingTextures`.
- `Engine/Source/File/FileManager.{h,cpp}` — the shared decommit/recommit API (from the mesh plan); `ResetTextureChunkStates` + `LoadChunk` recommit (`MEM_COMMIT` before disk re-read).
- `Engine/Source/Graphics/Managers/CLAUDE.md` — eviction-symmetry / drained descriptor-patch-window invariants (must not be disturbed).

## Out of scope
- The island mesh CPU slice (`IslandMeshCpuSliceReclaim.md`) and the other island residency buckets.
- Eager-pack (`mPackFileData`) reclaim — different storage mechanism (`File/EagerPackBufferReclaim.md`).

## Notes
- Client/graphics-only. **No determinism/CRC/`.pack`/`kiVersion` exposure** — texture CPU bytes never feed the sim.
- **Invariant exposure to declare at grill:** this adds a **steady-state main-loop** decommit/recommit path (allocation-tracker interaction inside `ProcessPendingTextures`) and touches the texture-LRU eviction-symmetry lifecycle — the reason it is a separate plan rather than a fold.
- Grill: option A/B/C churn tradeoff; confirm the `LoadChunk` recommit and that `ReadChunkData` sources from disk.
