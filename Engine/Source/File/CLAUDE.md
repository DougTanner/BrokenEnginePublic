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

**Eager Loading** (startup): Font, Gltf, Model, Raw, Shader
- Entire pack files loaded into memory during async initialization
- Zero-copy access via pointers into memory-mapped data
- Accessed via `GetEagerChunkMap()` returning `EagerChunk` structs
- For glTF chunks with `bHasAnimation`, extracts animation data (skeleton with all nodes, skin joint mapping, inverse bind matrices, and animation clips) into `gAnimationDataMap` global registry. Animation data offset calculated as aligned ChunkHeader + aligned material data size (16-byte alignment matches export)
- Initializes glTF comparison logging via `engine::InitComparisonLog()` for debugging animation processing (see Graphics/GltfComparisonLog.h)

**Lazy Loading** (on-demand): Audio, Islands, Texture
- Background thread processes priority queue (kLow -> kNormal -> kHigh -> kRealtime)
- Memory-efficient for large assets
- Accessed via `GetLazyChunkMap()` returning `LazyChunk` structs
- Texture chunks are uploaded to the GPU on the background thread via a dedicated transfer queue when a separate transfer queue family is available; the resulting VkImage is stored in the `LazyChunk` for adoption by TextureManager on the main thread

**Key APIs**:
- `RequestChunkLoad(span<crc>, priority)` - Queue chunks for background loading
- `IsChunkReady(crc)` - Non-blocking status check
- `WaitForChunks(span<crc>)` - Blocking wait using condition variables
- `ReadChunkData(crc, offset, span)` - Stream data from chunk (loaded or direct file read)

**Priority Loading**: During startup, Islands and TextureManager populate CRC vectors for critical assets. FileManager queues these with kRealtime priority during `LoadPackFiles()`.

**Transfer Queue GPU Uploads**: `InitTransferResources()` / `DestroyTransferResources()` manage a dedicated Vulkan command pool and fence for the transfer queue family. After loading a texture chunk from disk, `UploadTextureToGpu()` creates a VkImage via VMA, stages data into a host-visible buffer, records buffer-to-image copies with layout transitions, and submits on the transfer queue. When the transfer and graphics queues are on separate families: if QFOT is optional (VK_KHR_maintenance9), the image is transitioned directly to SHADER_READ_ONLY_OPTIMAL without queue family ownership transfer; otherwise, a queue family release barrier is recorded and the corresponding acquire barrier is performed by `Texture::AdoptTransferredImage()` on the graphics queue. If the transfer queue is the same as the graphics queue, background GPU uploads are skipped to avoid concurrent vkQueueSubmit from different threads. `ClearTransferredImage()` nulls out the VkImage/VmaAllocation handles after TextureManager adopts ownership.

**Threading**: Background `LoadingThread()` processes queue sorted by priority, waking via condition variable and notifying completions. Eager loading runs in separate async task that completes before background thread starts.

**Memory Profiling**: Provides aggregate and per-data-type memory metrics for ProfileManager. Aggregate methods return total bytes and allocation counts for eager vs lazy loading. `GetMemoryStats(DataTypes)` returns per-data-type statistics. `IsEagerChunk()` helper function classifies data types by loading strategy.

**Internal Helpers**: `GetDataFilePath()` constructs pack/manifest file paths from data type and extension.

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
