# `/Engine/Source/File/`

Centralized file I/O, packed asset loading, and state recording/replay.

**Global**: `gpFileManager`

## FileManager

Manages general file operations with platform directory access (AppData, Temp); flags select directory plus read/write/backup. Owns file I/O, versioned/atomic-write templates, directory resolution, and a forward-declared `PackChunks` sub-object reached through FileManager's public chunk API.

FileManager resolves the data root — canonical `LaunchOptions::dataDirectory` when `--data-directory <absolute-path>` is present, otherwise the executable-sibling `Data` root; process working directory never selects asset data — and hands it to `PackChunks`, which discovers and loads the pack/manifest files under it. Client and server worktree launches must receive the same explicit root from `/compile` via `/agent-harness`.

### Versioned I/O

`WriteVersionedFile<T>` / `ReadVersionedFile<T>` prefix version + size. The `has_binary_stream_operators_v` trait routes types that define `operator<<` / `operator>>` through those operators and everything else through raw byte copy; size is written/validated only for trivially-copyable types. Matching version with mismatched size triggers `DEBUG_BREAK` (likely missing sub-version bump). The version+size header itself is single-sourced in `WriteVersionHeader<T>` / `ReadAndValidateVersionHeader<T>`, shared by these functions, DifferenceStream save/load, and the game-layer `GameSaveLoad` grid saves — change the on-disk header in one place.

### Atomic Writes

Writes are atomic by default — staged through a `.tmp` sibling then `std::filesystem::rename`-replaced — so readers never observe a torn file even on crash mid-write. Direct write opens via `OpenFile(kWrite, ...)` must opt out by also setting `kStreaming`; one-shot writers should use `WriteFileAtomically` instead. Backup mode timestamps and copies the existing file before opening for write; status-query or copy failure logs `kError` and continues without the backup (the atomic main-file write is unaffected).

## PackChunks

The packed-asset chunk engine, owned by FileManager via `std::unique_ptr` and reached only through FileManager's forwarding chunk API. Not a `*Manager`: no `gp*` global, not aggregated into `Engine.h`; its header is included only by `PackChunks.cpp` and `FileManager.cpp`. Owns the eager pack buffers, the lazy chunk maps and their atomic `eState` machine, the background loading-thread pool with its sync primitives, and the single-`VirtualAlloc` lazy memory pool.

After validating manifests at startup, it synchronously hashes the ordered Islands manifest table into the connection integrity token. DataPacker's per-entry content CRCs make this payload-sensitive without reading island pack payloads at runtime.

### Eager vs Lazy

`IsEagerChunk(DataTypes)` selects client-only Scene/Model/Shader/Raw packs, read whole at boot with chunk pointers aliasing the pack buffer. Audio/Islands/Texture are lazy. Server skips eager types and opens only lazy types accepted by `IsServerChunk` (currently Islands), allowing DataPacker to rewrite unopened Audio/Texture packs while the server runs. Eager load is asynchronous and format-agnostic; readers acquire its completion flag before accessing the map.

### Lazy Loading

A fixed background pool (currently two threads at `THREAD_PRIORITY_BELOW_NORMAL`) services a shared priority queue; each thread owns its read buffer and decompress scratch, so raising the count multiplies both. `notify_all` engages the pool for bursts. Reads against the shared pack handle are positional, sector-aligned, unbuffered 256KB sub-reads; aligned copies use streaming stores and tails fall back to `memcpy`. Under-lock request gating assigns each chunk to one thread, while the per-chunk release/acquire state publishes its result.

Chunk state is an atomic acquire/release machine; the `eState` release/acquire also covers `pData`/`iDataSize` visibility on the lazy resident-copy path — no separate lock. Textures traverse the full CPU+GPU chain via `TextureUploadManager`, non-texture chunks short-circuit to ready after disk load. Queue insertion wraps `ScopedSuppressAllocationTracking` — items must outlive frame scope. `WaitForChunks` requests at realtime priority and blocks until ready, but a chunk already queued at lower priority is not re-prioritized — priority applies only to not-yet-requested chunks. Free function `RequestTextureChunkLoad(crc)` is forward-declared in `Frame/Collections/Collection.h` so collection templates can request texture loads without including FileManager.h.

Random-access chunk reads copy from the pool when resident, otherwise read the requested span directly from the pack file; audio uses this without resident-loading the whole chunk.

Texture chunks use LZ4; zlib remains a supported decode branch. Compressed bytes use regular `memcpy` into per-thread scratch to stay cache-hot for decompression into the lazy pool. Non-texture and uncompressed chunks retain the cache-bypass copy path.

### Corrupt / Missing Asset Policy

External pack/manifest data is a trust boundary, so loads degrade rather than assert. Two tiers, split by whether a try/catch exists yet:

- **Boot-time required assets** (manifest/pack header, chunk-count range, chunk table, pack open) fail hard: log `kError`, `DEBUG_BREAK`, user-facing MessageBox, then `ExitProcess(0)`. `PackChunks` is constructed (and runs its boot load) inside the FileManager ctor, which runs in `wWinMain` before `MainThread`'s try/catch, so a thrown ASSERT there would `std::terminate` with no crash report.
- **Loading-thread per-chunk corruption** (bad header flags, failed LZ4/zlib decompress, zero-progress/truncated read that would otherwise spin) fails soft: log `kError`, `DEBUG_BREAK`, mark the chunk `kReady` (pool slot stays zero-filled), notify completion, return — the thread survives and `WaitForChunks` waiters unblock.

### Lazy Memory Pool Invariant

Single `VirtualAlloc` (`MEM_RESERVE | MEM_COMMIT`), sized by cumulative `RoundUp` over the full lazy chunk map. Per-chunk `pData` is assigned by walking the same map in the same order. Any reset routine must iterate the entire map (not a subset) to preserve the cumulative offset contract — hashmap iteration order *is* the layout. Compressed chunks contribute their uncompressed size to the cumulative offset; `LazyChunk.iDataSize` is the consumer-visible (post-decompression) byte count, not the on-disk size.

One-shot consumers may reclaim an uncompressed payload sub-range with `DecommitChunkRange` and restore it with `RecommitAndReloadChunkRange`. Only the page-aligned interior is decommitted, preserving boundary pages, pointers, and cumulative offsets. Reload recommits and reads directly from disk rather than the resident-copy path; it returns `false` on recommit, open, or truncated-read failure. Island mesh CPU-slice reclaim uses this main-thread-only with no concurrent reader.

`LazyChunk.iDataSize` is post-decompression size. `EagerChunk.iDataSize` is the raw on-disk extent, including a scene chunk's appended animation section that `ChunkHeader::iSize` excludes.

### Texture Chunk State Reset

Texture reset clears GPU handles: ready chunks return to not-loaded for full reload; upload-in-flight chunks return to disk-loaded for re-upload. Whole-pool device-loss recovery and scoped island LRU eviction share this transition logic. The transfer thread must not upload a chunk being reset. Lock-free audio reads remain safe because restoration rewrites identical pool pointers for chunks not being evicted.

## DifferenceStream

Template delta compression for deterministic recording/replay. Records full boundary states and per-frame deltas; a checksum stream validates playback. `Save` emits a sibling file set (header, `.frames`, `.checksums`, and optional `.fullframes`) and succeeds only when every write succeeds. Failure independently removes every sibling and returns `false`; callers lacking an end frame use the same explicit all-sibling cleanup path. Optional full-frame debug state is gated by `kbReplayFullFrames`.
