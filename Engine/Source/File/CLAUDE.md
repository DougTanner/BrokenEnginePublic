# `/Engine/Source/File/`

Centralized file I/O, packed asset loading, and state recording/replay.

**Global**: `gpFileManager`

## FileManager

Manages file operations and asset loading with a dual-loading strategy: eager loading for critical assets at startup, lazy loading with prioritization for large assets on-demand.

### Directory Management

Provides access to platform directories (AppData for saves/config, Temp for scratch files) via `FileFlags`, with optional timestamped backup on write.

### Packed Asset System

Assets stored in `.pack` files with `.manifest` metadata, split into two loading strategies based on data type:

**Eager Loading** (Font, Scene, Model, Raw, Shader): Entire pack files loaded into memory during async initialization with zero-copy access via pointers into the loaded data. Scene chunks with animation data are parsed and registered in `gAnimationDataMap` for zero-copy access to skeletal animation structures.

**Lazy Loading** (Audio, Islands, Texture): A background thread processes a priority queue, reading chunks via unbuffered disk I/O (`FILE_FLAG_NO_BUFFERING`) with non-temporal stores to bypass L3 cache. All lazy data is pre-allocated in a single `VirtualAlloc` pool to eliminate heap lock contention during loading. Textures go through a multi-stage state machine (not loaded -> disk loaded -> GPU uploaded -> ready) coordinating with TextureUploadManager, while non-texture chunks become ready immediately after disk load. State transitions use atomic acquire/release ordering for cross-thread visibility; `kDiskLoaded` serves as the threshold for "data is in memory" checks across multiple APIs.

**Priority Loading**: Islands and TextureManager populate priority CRC vectors at startup; FileManager queues these at realtime priority before processing normal-priority requests.

**Device Recreation**: After GPU device loss, `ResetTextureChunkStates()` restores lazy chunks to a re-loadable state -- chunks that still have CPU data only need GPU re-upload, while chunks whose CPU data was cleared require full disk reload.

**Memory Profiling**: Provides aggregate and per-data-type memory metrics (bytes and allocation counts) for ProfileManager.

### Versioned I/O Templates

Type-safe save/load with automatic version validation via `WriteVersionedFile<T>()` / `ReadVersionedFile<T>()` / `ExistsVersionedFile<T>()`. Requires structs to define `static constexpr int64_t kiVersion`. Automatically detects stream operators via `has_binary_stream_operators_v<T>` type trait, falling back to raw binary copy. Validates `sizeof(T)` for trivially copyable types to detect struct layout changes.

## DifferenceStream.h

Template-based delta compression for deterministic state recording and replay. Records full state at boundaries with only changed states between frames.

**DifferenceStreamWriter** records state changes during gameplay, capturing CRC checksums every frame but only writing difference records when state actually changes. Saves a header file plus `.frames`, `.checksums`, and optionally `.fullframes` for debugging.

**DifferenceStreamReader** replays recorded state with CRC validation at each frame. Provides a split API (`LoadDifference()` + `ValidateChecksum()`) for callers that need to inject state between loading and validation (e.g., transfer spawns during multi-frame replay), plus a combined `Update()` for simple cases. Triggers debug break on CRC mismatch; with `kbEnableReplayFullFrames`, performs detailed field-by-field comparison.

### Requirements
- Both template types need stream operators for serialization
- `DIFFERENCE_TYPE` needs equality operator for change detection
- `SAVED_TYPE` must provide `Crc()` method returning `common::crc_t`

**TransferViaStream<T>**: Helper to create independent copies of non-copyable types via stream round-trip, used during recording initialization.
