# `/Engine/Source/File/`

Centralized file I/O, packed asset loading, and state recording/replay.

**Global**: `gpFileManager`

## FileManager

Manages file operations and asset loading with platform directory access (AppData, Temp). Flags select directory plus read/write/backup.

### Eager vs Lazy

Split determined by `IsEagerChunk(DataTypes)`: Font/Scene/Model/Shader/Raw are eager (client-only, each pack read whole into memory at boot; chunk pointers alias that buffer, no per-chunk copies); Audio/Islands/Texture are lazy. Server skips eager types entirely and additionally restricts lazy opens to types matching `IsServerChunk(DataTypes)` (currently `kDataTypeIslands` only) — Audio/Texture packs are never opened server-side, so DataPacker can rewrite them while the server runs (an open `FILE_SHARE_READ` handle would block rewrites). Eager load runs async and is format-agnostic (populates the chunk map only — consumers parse their own formats); first consumer blocks on the future.

### Lazy Loading

Background thread services a priority queue. Thread runs at `THREAD_PRIORITY_BELOW_NORMAL` — not `THREAD_MODE_BACKGROUND_BEGIN`, whose `IoPriorityVeryLow` stalls large reads behind foreground I/O (Defender, indexing, OneDrive) for seconds during startup contention. Unbuffered disk I/O (`FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN`) into a pre-allocated sector-aligned read buffer; sector size queried via `GetDiskFreeSpaceW` on the data drive root. Reads split into 256KB sub-chunks. Aligned 16B copy path uses `_mm_stream_si128` + `_mm_sfence` to bypass L3; tail/unaligned falls back to `memcpy`.

Chunk state is an atomic acquire/release machine; textures traverse the full CPU+GPU chain via `TextureUploadManager`, non-texture chunks short-circuit to ready after disk load. Queue insertion wraps `ScopedSuppressAllocationTracking` — items must outlive frame scope. `WaitForChunks` requests at realtime priority and blocks until ready, but a chunk already queued at lower priority is not re-prioritized — priority applies only to not-yet-requested chunks. Free function `RequestTextureChunkLoad(crc)` is forward-declared in `Frame/Collections/Collection.h` so collection templates can request texture loads without including FileManager.h.

A random-access read API serves an arbitrary offset+span within a chunk: it copies from the pool when the chunk is already loaded, otherwise reads directly from the pack file via a transient buffered stream (used to stream audio without resident-loading the whole chunk).

Texture chunks flagged zlib-compressed read into a dedicated decompress scratch (sized at boot to the largest compressed chunk on disk) using regular `memcpy` instead of the streaming-store path — keeps bytes hot for `uncompress`, which writes into the lazy-pool slot. Non-texture and uncompressed chunks retain the cache-bypass fast path.

### Corrupt / Missing Asset Policy

External pack/manifest data is a trust boundary, so loads degrade rather than assert. Two tiers, split by whether a try/catch exists yet:

- **Boot-time required assets** (manifest/pack header, chunk-count range, chunk table, pack open) fail hard: log `kError`, `DEBUG_BREAK`, user-facing MessageBox, then `ExitProcess(0)`. The FileManager ctor runs in `wWinMain` before `MainThread`'s try/catch, so a thrown ASSERT there would `std::terminate` with no crash report.
- **Loading-thread per-chunk corruption** (bad header flags, failed zlib decompress, zero-progress/truncated read that would otherwise spin) fails soft: log `kError`, `DEBUG_BREAK`, mark the chunk `kReady` (pool slot stays zero-filled), notify completion, return — the thread survives and `WaitForChunks` waiters unblock.

### Lazy Memory Pool Invariant

Single `VirtualAlloc` (`MEM_RESERVE | MEM_COMMIT`), sized by cumulative `RoundUp` over the full lazy chunk map. Per-chunk `pData` is assigned by walking the same map in the same order. Any reset routine must iterate the entire map (not a subset) to preserve the cumulative offset contract — hashmap iteration order *is* the layout. Compressed chunks contribute their uncompressed size to the cumulative offset; `LazyChunk.iDataSize` is the consumer-visible (post-decompression) byte count, not the on-disk size.

Note: `EagerChunk` also has an `iDataSize` field but with different semantics — it is the raw on-disk data extent (`ChunkLocation::uiSize - kiChunkDataOffset`), which for scene chunks includes the appended animation section that `ChunkHeader::iSize` excludes. `LazyChunk.iDataSize` is post-decompression size; `EagerChunk.iDataSize` is raw on-disk size.

### Texture Chunk State Reset

Resetting texture chunks clears GPU handles and transitions based on CPU residency: ready chunks drop to not-loaded (full reload); upload-in-flight chunks drop to disk-loaded (re-upload only). Two callers: a whole-pool variant for device-loss recovery, and a scoped variant taking a span of island CRCs for per-island LRU eviction. Both share the same per-chunk transition logic — keep them in sync if state machine changes. The thread-safety precondition is documented, not asserted: the transfer thread must not be concurrently uploading any chunk being reset (device-loss caller runs after the transfer thread joins; the scoped caller runs inside the drained descriptor-patch window).

### Versioned I/O

`WriteVersionedFile<T>` / `ReadVersionedFile<T>` prefix version + size. The `has_binary_stream_operators_v` trait routes types that define `operator<<` / `operator>>` through those operators and everything else through raw byte copy; size is written/validated only for trivially-copyable types. Matching version with mismatched size triggers `DEBUG_BREAK` (likely missing sub-version bump).

### Atomic Writes

Writes are atomic by default — staged through a `.tmp` sibling then `std::filesystem::rename`-replaced — so readers never observe a torn file even on crash mid-write. Direct write opens via `OpenFile(kWrite, ...)` must opt out by also setting `kStreaming`; one-shot writers should use `WriteFileAtomically` instead. Backup mode timestamps and copies the existing file before opening for write; copy failure logs `kError` and continues without the backup (the atomic main-file write is unaffected).

## DifferenceStream

Template delta compression for deterministic state recording/replay. Records full state at boundaries and only changed states between frames; per-frame CRC stream enables validation. Optional full-frame debug stream gated by `if constexpr (kbReplayFullFrames)`; the stream/index members are present in all builds (avoids clang's parse-time non-dependent type checks inside `if constexpr` discarded branches) but stay empty when disabled. Template parameters must supply stream operators and a `Crc()` method returning `common::crc_t`.
