# `/Engine/Source/File/`

Centralized file I/O, packed asset loading, and state recording/replay.

**Global**: `gpFileManager`

## FileManager

Manages file operations and asset loading with dual-loading strategy: eager loading for critical assets at startup, lazy loading with prioritization for large assets on-demand.

### Directory Management

Provides access to platform directories via `FileFlags`:
- `kAppDataDirectory` - User saves and configuration (auto-created per game)
- `kTempDirectory` - Temporary files
- `kBackup` - Creates timestamped backup before writing

### Packed Asset System

Assets stored in `.pack` files with `.manifest` metadata. Two loading strategies:

**Eager Loading** (startup): Font, Scene, Model, Raw, Shader
- Entire pack files loaded into memory during async initialization
- Zero-copy access via pointers into memory-mapped data
- Accessed via `GetEagerChunkMap()` returning `EagerChunk` structs
- For scene chunks with `bHasAnimation`, passes a pointer to the animation data stream within the chunk to `AnimationData::Load()`, which sets up zero-copy const pointers into pack memory for nodes, skin joint mapping, inverse bind matrices, animation clips, material infos, channels, and keyframes. Results stored in `gAnimationDataMap` global registry keyed by scene CRC

**Lazy Loading** (on-demand): Audio, Islands, Texture
- Background thread processes priority queue (kLow -> kNormal -> kHigh -> kRealtime)
- Memory-efficient for large assets
- Accessed via `GetLazyChunkMap()` returning `LazyChunk` structs
- `LazyChunk` stores chunk location, atomic state, header, a pre-allocated data pointer into a `VirtualAlloc` memory pool, data size, and GPU upload results (VkImage, VmaAllocation, VkDeviceMemory) written by the upload thread and read by the main thread

**Key APIs**:
- `RequestChunkLoad(span<crc>, priority)` - Queue chunks for background loading
- `IsChunkReady(crc)` - Non-blocking status check
- `WaitForChunks(span<crc>)` - Blocking wait using condition variables
- `ReadChunkData(crc, offset, span)` - Stream data from chunk (loaded or direct file read)

**Priority Loading**: During startup, Islands populates CRC vectors for critical assets. FileManager queues island priority loads with kRealtime priority during `LoadPackFiles()`. TextureManager requests its own priority and remaining texture loads after construction.

**Chunk State Machine**: `LazyChunk` uses `ChunkState` enum with atomic ordering for thread-safe state progression. Textures follow the full pipeline: `kNotLoaded` -> `kLoadRequested` -> `kUploading` -> `kGpuUploadComplete` -> `kReady`. Non-texture chunks (audio, islands) skip GPU upload and transition directly from `kLoadRequested` to `kReady` after disk load. The `kDiskLoaded` enum value (between `kLoadRequested` and `kUploading`) serves as a threshold for "data is in memory" checks -- `RequestChunkLoad`, `ReadChunkData`, and memory profiling all compare `>= kDiskLoaded` to determine if chunk data is available. The `kReady` state is the terminal state; `IsChunkReady()` and `WaitForChunks()` both check for `>= kReady`. State checks use `std::memory_order_acquire`/`release` for cross-thread visibility. `MovableAtomicChunkState` wrapper enables `LazyChunk` to be stored in containers by providing copy/move constructors that atomically load and initialize.

**Memory Pool**: All lazy chunk data is pre-allocated in a single `VirtualAlloc` pool during `LoadPackFiles()`. Each `LazyChunk` receives a pointer into this pool at its aligned offset, eliminating heap lock contention during background loading. The pool is freed with `VirtualFree` in the destructor.

**Device Recreation**: `ResetTextureChunkStates()` restores all lazy chunks to a loadable state after GPU device destruction. Restores pool data pointers for all chunks (since `ProcessPendingTextures` clears `pData`/`iDataSize` for adopted textures), nulls GPU handles (`vkImage`, `vmaAllocation`, `vkDeviceMemory`), and resets texture chunk states: `kReady` chunks (CPU data cleared) go back to `kNotLoaded` for full disk reload, while `kGpuUploadComplete`/`kUploading` chunks (CPU data still valid) go back to `kDiskLoaded` for re-upload only. Called by `Graphics::Destroy()` during full surface-level destruction, after TextureUploadManager transfer resources are destroyed and before DeviceManager destruction.

**Unbuffered Disk I/O**: Lazy chunk loading uses Win32 `FILE_FLAG_NO_BUFFERING` for direct disk access, bypassing the OS file cache. During `LoadPackFiles()`, persistent file handles are opened for each lazy pack file type, and a sector-aligned read buffer is pre-allocated (256KB sub-read size + sector padding). `LoadChunk()` reads data in 256KB sub-chunks (`kiSubReadSize`), aligning offsets and sizes to disk sector boundaries. Each sub-chunk is read into the shared buffer via `ReadFile()`, then copied to the destination using non-temporal stores (`_mm_stream_si128`) to bypass L3 cache, with `_mm_sfence` after each batch. Handles and the read buffer are cleaned up in the destructor.

**Threading**: Background `LoadingThread()` runs at background thread priority (`THREAD_MODE_BACKGROUND_BEGIN`) and processes queue sorted by priority, waking via condition variable. For non-texture chunks, sets state directly to `kReady` and calls `NotifyChunkCompletion()` to wake waiters. Eager loading runs in separate async task that completes before background thread starts.

**Memory Profiling**: Provides aggregate and per-data-type memory metrics for ProfileManager. Aggregate methods return total bytes and allocation counts for eager vs lazy loading. `GetMemoryStats(DataTypes)` returns per-data-type statistics using `DataTypeFromFlags()` to classify lazy chunks.

**Internal Helpers**: `GetDataFilePath()` constructs pack/manifest file paths from data type and extension. `DataTypeFromFlags()` derives the `DataTypes` enum from a chunk's `ChunkFlags` (used to look up pack file handles and classify chunks by type). `IsEagerChunk()` classifies data types by loading strategy. `RequestTextureChunkLoad(crc)` is a free function (declared in Collection.h, implemented in FileManager.cpp) that bridges collection type registration to lazy chunk loading -- called automatically by `TypeRegistry::RegisterType` for types with texture CRCs. All chunk data offset calculations use the centralized `common::kiChunkDataOffset` constant from DataFile.h.

### Versioned I/O Templates

Type-safe save/load with automatic version validation:
- `WriteVersionedFile<T>()` / `ReadVersionedFile<T>()` - Serialize/deserialize with version header
- `ExistsVersionedFile<T>()` - Check file exists with correct version

Requires structs to define `static constexpr int64_t kiVersion`. Automatically detects and uses stream operators when available via `has_binary_stream_operators_v<T>` type trait; otherwise falls back to raw binary copy.

**Size Validation**: For trivially copyable types, validates `sizeof(T)` matches stored size to detect struct layout changes. For types with custom stream operators, skips size validation since serialized size may differ from struct size.

## DifferenceStream.h

Template-based delta compression for deterministic state recording and replay. Records full state at boundaries with only changed states between frames.

### Template Classes

**DifferenceStreamWriter<SAVED_TYPE, DIFFERENCE_TYPE>**
Records state changes during gameplay. `Update()` captures CRCs every frame but only writes difference records when state changes. `Save()` writes header file with version info and start/end states, plus `.frames` (difference records), `.checksums` (validation data), and optionally `.fullframes` (complete snapshots when `kbEnableReplayFullFrames` defined).

**DifferenceStreamReader<SAVED_TYPE, DIFFERENCE_TYPE>**
Replays recorded state with validation. Verifies version and conditionally validates struct size (only for trivially copyable types). `Update()` reconstructs state and validates CRCs against recorded values. Triggers debug break on CRC mismatch to detect non-determinism. With `kbEnableReplayFullFrames`, performs detailed field comparison via `common::BreakOnNotEqual()` on mismatch.

### Requirements

- Both template types need stream operators for serialization
- `DIFFERENCE_TYPE` needs equality operator for change detection
- `SAVED_TYPE` must provide `Crc()` method returning `common::crc_t`

### Helper Utility

**TransferViaStream<T>**: Transfers data between objects using stream operators. Used for creating independent copies of non-copyable types during recording initialization.
