# `/Engine/Source/File/`

Centralized file I/O, packed asset loading, and state recording/replay.

**Global**: `gpFileManager`

## FileManager

Manages file operations and asset loading with platform directory access (AppData, Temp). Flags select directory plus read/write/backup; backup mode timestamps and copies the existing file before a write open.

### Eager vs Lazy

Split determined by `IsEagerChunk(DataTypes)`: Font/Scene/Model/Shader/Raw are eager (client-only, entire pack mmap'd at boot for zero-copy access); Audio/Islands/Texture are lazy. Server skips eager types entirely and additionally restricts lazy opens to types matching `IsServerChunk(DataTypes)` (currently `kDataTypeIslands` only) — Audio/Texture packs are never opened server-side, so DataPacker can rewrite them while the server runs (the prior `FILE_SHARE_READ` handle blocked rewrites). Eager parse runs async; first consumer blocks on the future.

### Lazy Loading

Background thread services a priority queue. Thread runs at `THREAD_PRIORITY_BELOW_NORMAL` — NOT `THREAD_MODE_BACKGROUND_BEGIN`, whose `IoPriorityVeryLow` stalls large reads behind foreground I/O (Defender, indexing, OneDrive) for seconds during startup contention. Unbuffered disk I/O (`FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN`) into a pre-faulted sector-aligned read buffer; sector size queried via `GetDiskFreeSpaceW` on the data drive root. Reads split into 256KB sub-chunks. Aligned 16B copy path uses `_mm_stream_si128` + `_mm_sfence` to bypass L3; tail/unaligned falls back to `memcpy`.

Chunk state is an atomic acquire/release machine; textures traverse the full CPU+GPU chain via `TextureUploadManager`, non-texture chunks short-circuit to ready after disk load. Queue insertion wraps `ScopedSuppressAllocationTracking` — items must outlive frame scope. `WaitForChunks` auto-promotes to realtime priority.

Texture chunks flagged zlib-compressed read into a dedicated decompress scratch (sized at boot to the largest compressed chunk on disk) using regular `memcpy` instead of the streaming-store path — keeps bytes hot for `uncompress`, which writes into the lazy-pool slot. Non-texture and uncompressed chunks retain the cache-bypass fast path.

### Lazy Memory Pool Invariant

Single `VirtualAlloc` (`MEM_RESERVE | MEM_COMMIT`), sized by cumulative `RoundUp` over the full lazy chunk map. Per-chunk `pData` is assigned by walking the same map in the same order. **Any reset routine must iterate the entire map** (not a subset) to preserve the cumulative offset contract — hashmap iteration order *is* the layout. Compressed chunks contribute their **uncompressed** size to the cumulative offset; `LazyChunk.iDataSize` is the consumer-visible (post-decompression) byte count, not the on-disk size.

### Texture Chunk State Reset

Resetting texture chunks clears GPU handles and transitions based on CPU residency: ready chunks drop to not-loaded (full reload); upload-in-flight chunks drop to disk-loaded (re-upload only). Two callers: a whole-pool variant for device-loss recovery, and a scoped variant taking a span of island CRCs for per-island LRU eviction. Both share the same per-chunk transition logic — keep them in sync if state machine changes.

### Versioned I/O

`WriteVersionedFile<T>` / `ReadVersionedFile<T>` prefix version + size. The `has_binary_stream_operators_v` trait routes trivially-copyable types through byte copy and non-trivial types through `operator<<` / `operator>>`. Matching version with mismatched size triggers `DEBUG_BREAK` (likely missing sub-version bump).

### Atomic Writes

Writes are atomic by default — staged through a temp sibling and `MoveFileExW`-replaced — so readers never observe a torn file even on crash mid-write. Direct write opens via `OpenFile(kWrite, ...)` must opt out by also setting `kStreaming`; one-shot writers should use `WriteFileAtomically` instead. Backup mode timestamps and copies the existing file before opening for write, and asserts on copy failure.

## DifferenceStream

Template delta compression for deterministic state recording/replay. Records full state at boundaries and only changed states between frames; per-frame CRC stream enables validation. Optional full-frame debug stream gated by `if constexpr (kbReplayFullFrames)`; the stream/index members are present in all builds (avoids clang's parse-time non-dependent type checks inside `if constexpr` discarded branches) but stay empty when disabled. Template parameters must supply stream operators and a `Crc()` method returning `common::crc_t`.
