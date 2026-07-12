# `/Engine/Source/File/`

Centralized file I/O, packed asset loading, and state recording/replay.

**Global**: `gpFileManager`

## FileManager

Manages general file operations with platform directory access (AppData, Temp); flags select directory plus read/write/backup. Owns file I/O, the versioned/atomic-write templates, and directory resolution, and holds a `PackChunks` sub-object (`std::unique_ptr`, forward-declared so the chunk engine's headers stay out of FileManager.h's ~20 PCH consumers) that owns the packed-asset chunk engine. FileManager's public chunk methods forward to it; `gpFileManager` and the call sites are unchanged.

FileManager resolves the data root — canonical `LaunchOptions::dataDirectory` when `--data-directory <absolute-path>` is present, otherwise the executable-sibling `Data` root; process working directory never selects asset data — and hands it to `PackChunks`, which discovers and loads the pack/manifest files under it. Client and server worktree launches must receive the same explicit root from `/compile` via `/agent-harness`.

### Versioned I/O

`WriteVersionedFile<T>` / `ReadVersionedFile<T>` prefix version + size. The `has_binary_stream_operators_v` trait routes types that define `operator<<` / `operator>>` through those operators and everything else through raw byte copy; size is written/validated only for trivially-copyable types. Matching version with mismatched size triggers `DEBUG_BREAK` (likely missing sub-version bump). The version+size header itself is single-sourced in `WriteVersionHeader<T>` / `ReadAndValidateVersionHeader<T>`, shared by these functions, DifferenceStream save/load, and the game-layer `GameSaveLoad` grid saves — change the on-disk header in one place.

### Atomic Writes

Writes are atomic by default — staged through a `.tmp` sibling then `std::filesystem::rename`-replaced — so readers never observe a torn file even on crash mid-write. Direct write opens via `OpenFile(kWrite, ...)` must opt out by also setting `kStreaming`; one-shot writers should use `WriteFileAtomically` instead. Backup mode timestamps and copies the existing file before opening for write; copy failure logs `kError` and continues without the backup (the atomic main-file write is unaffected).

## PackChunks

The packed-asset chunk engine, owned by FileManager via `std::unique_ptr` and reached only through FileManager's forwarding chunk API. Not a `*Manager`: no `gp*` global, not aggregated into `Engine.h`; its header is included only by `PackChunks.cpp` and `FileManager.cpp`. Owns the eager pack buffers, the lazy chunk maps and their atomic `eState` machine, the background loading-thread pool with its sync primitives, and the single-`VirtualAlloc` lazy memory pool.

### Eager vs Lazy

Split determined by `IsEagerChunk(DataTypes)`: Font/Scene/Model/Shader/Raw are eager (client-only, each pack read whole into memory at boot; chunk pointers alias that buffer, no per-chunk copies); Audio/Islands/Texture are lazy. Server skips eager types entirely and additionally restricts lazy opens to types matching `IsServerChunk(DataTypes)` (currently `kDataTypeIslands` only) — Audio/Texture packs are never opened server-side, so DataPacker can rewrite them while the server runs (an open `FILE_SHARE_READ` handle would block rewrites). Eager load runs async and is format-agnostic (populates the chunk map only — consumers parse their own formats); readers acquire an atomic completion flag (set release by the async task) before touching the eager map — gates the boot window only.

### Lazy Loading

A small fixed pool of background threads (`kiLoadingThreadCount`, currently 2) services a shared priority queue, parallelizing decompression (the streaming-latency bottleneck — chunks are independent). Each thread runs at `THREAD_PRIORITY_BELOW_NORMAL` — not `THREAD_MODE_BACKGROUND_BEGIN`, whose `IoPriorityVeryLow` stalls large reads behind foreground I/O (Defender, indexing, OneDrive) for seconds during startup contention — and owns a private read buffer *and* decompress scratch (indexed by thread index), so raising the count multiplies both buffers' memory. The shared queue's `mWakeCondition` uses `notify_all` (both the request-enqueue burst and shutdown) so a multi-chunk burst engages every thread rather than draining serially on one. The pack handle is shared, so sub-reads are positional (offset supplied in an `OVERLAPPED` — no `FILE_FLAG_OVERLAPPED` needed; a synchronous handle still completes synchronously) instead of `SetFilePointerEx` + `ReadFile`, which would race the handle's shared file position across threads. Unbuffered disk I/O (`FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN`); sector size queried via `GetDiskFreeSpaceW` on the data drive root, offsets kept sector-aligned. Reads split into 256KB sub-chunks. Aligned 16B copy path uses `_mm_stream_si128` + `_mm_sfence` to bypass L3; tail/unaligned falls back to `memcpy`. The per-chunk `eState` release/acquire machine tolerates any thread performing the store; `RequestChunkLoad`'s under-lock state gating ensures no chunk is ever handed to two threads.

Chunk state is an atomic acquire/release machine; the `eState` release/acquire also covers `pData`/`iDataSize` visibility on the lazy resident-copy path — no separate lock. Textures traverse the full CPU+GPU chain via `TextureUploadManager`, non-texture chunks short-circuit to ready after disk load. Queue insertion wraps `ScopedSuppressAllocationTracking` — items must outlive frame scope. `WaitForChunks` requests at realtime priority and blocks until ready, but a chunk already queued at lower priority is not re-prioritized — priority applies only to not-yet-requested chunks. Free function `RequestTextureChunkLoad(crc)` is forward-declared in `Frame/Collections/Collection.h` so collection templates can request texture loads without including FileManager.h.

A random-access read API serves an arbitrary offset+span within a chunk: it copies from the pool when the chunk is already loaded, otherwise reads directly from the pack file via a transient buffered stream (used to stream audio without resident-loading the whole chunk).

Texture chunks are LZ4-compressed (`kLz4Compressed`; DataPacker switched them from zlib for ~5-10x faster decode with no adler32 pass — `kZlibCompressed` remains a legal decode branch that the loader still handles). A compressed chunk (either codec, tested via `common::IsCompressed`) reads into the loading thread's own decompress scratch (one per loading thread, each sized at boot to the largest compressed chunk on disk) using regular `memcpy` instead of the streaming-store path — keeps bytes hot for the decompress (`LZ4_decompress_safe` / `uncompress`), which writes into the lazy-pool slot. Non-texture and uncompressed chunks retain the cache-bypass fast path.

### Corrupt / Missing Asset Policy

External pack/manifest data is a trust boundary, so loads degrade rather than assert. Two tiers, split by whether a try/catch exists yet:

- **Boot-time required assets** (manifest/pack header, chunk-count range, chunk table, pack open) fail hard: log `kError`, `DEBUG_BREAK`, user-facing MessageBox, then `ExitProcess(0)`. `PackChunks` is constructed (and runs its boot load) inside the FileManager ctor, which runs in `wWinMain` before `MainThread`'s try/catch, so a thrown ASSERT there would `std::terminate` with no crash report.
- **Loading-thread per-chunk corruption** (bad header flags, failed LZ4/zlib decompress, zero-progress/truncated read that would otherwise spin) fails soft: log `kError`, `DEBUG_BREAK`, mark the chunk `kReady` (pool slot stays zero-filled), notify completion, return — the thread survives and `WaitForChunks` waiters unblock.

### Lazy Memory Pool Invariant

Single `VirtualAlloc` (`MEM_RESERVE | MEM_COMMIT`), sized by cumulative `RoundUp` over the full lazy chunk map. Per-chunk `pData` is assigned by walking the same map in the same order. Any reset routine must iterate the entire map (not a subset) to preserve the cumulative offset contract — hashmap iteration order *is* the layout. Compressed chunks contribute their uncompressed size to the cumulative offset; `LazyChunk.iDataSize` is the consumer-visible (post-decompression) byte count, not the on-disk size.

A chunk consumer that reads a payload sub-range exactly once may reclaim it with `DecommitChunkRange(crc, offset, length)` / `RecommitAndReloadChunkRange(crc, offset, length)`: the former `MEM_DECOMMIT`s only the page-aligned interior of the range (boundary partial-pages, which may share bytes with the neighbouring payload, and every other chunk stay committed — the `pData` pointer and cumulative offsets are unchanged); the latter `MEM_COMMIT`s that interior and re-reads the range straight from the pack file on disk (not the decommitted resident copy, so it does **not** go through `ReadChunkData`, which copies from the pool for a loaded chunk) and returns `bool` — `false` on any soft-fail (recommit, pack open, or truncated read) so the caller can fail loud instead of reading stale pages. Uncompressed chunks only (on-disk payload == pool layout). Used by the island mesh CPU-slice reclaim (`IslandTerrain{,Residency}.cpp`); main-thread only, with no concurrent reader of the range.

Note: `EagerChunk` also has an `iDataSize` field but with different semantics — it is the raw on-disk data extent (`ChunkLocation::uiSize - kiChunkDataOffset`), which for scene chunks includes the appended animation section that `ChunkHeader::iSize` excludes. `LazyChunk.iDataSize` is post-decompression size; `EagerChunk.iDataSize` is raw on-disk size.

### Texture Chunk State Reset

Resetting texture chunks clears GPU handles and transitions based on CPU residency: ready chunks drop to not-loaded (full reload); upload-in-flight chunks drop to disk-loaded (re-upload only). Two callers: a whole-pool variant for device-loss recovery, and a scoped variant taking a span of island CRCs for per-island LRU eviction. Both share the same per-chunk transition logic — keep them in sync if state machine changes. The thread-safety precondition is documented, not asserted: the transfer thread must not be concurrently uploading any chunk being reset (device-loss caller runs after it joins; the scoped caller runs inside the drained descriptor-patch window). The audio fill worker reads chunk `pData`/`iDataSize` lock-free via `ReadChunkData`, but the pool-pointer restoration rewrites identical values for any chunk not being evicted, so a racing audio read is benign.

## DifferenceStream

Template delta compression for deterministic state recording/replay. Records full state at boundaries and only changed states between frames; per-frame CRC stream enables validation. `Save` emits a sibling file set (header, `.frames`, `.checksums`, and `.fullframes` when enabled); any in-process write failure removes the whole set so a partial recording is never left to load. Optional full-frame debug stream gated by `if constexpr (kbReplayFullFrames)`; the stream/index members are present in all builds (avoids clang's parse-time non-dependent type checks inside `if constexpr` discarded branches) but stay empty when disabled. Template parameters must supply stream operators and a `Crc()` method returning `common::crc_t`.
