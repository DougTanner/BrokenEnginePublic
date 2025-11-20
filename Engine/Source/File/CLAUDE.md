# `/Engine/Source/File/`

Centralized file I/O, packed asset loading, and state recording/replay.

**Global**: `gpFileManager`

## FileManager

Manages all file operations and asset loading with a dual-loading strategy: eager loading for critical assets at startup, and lazy loading with prioritization for large assets on-demand.

### Directory Management

Provides access to platform directories for saves, config, and temporary files:
- AppData directory for user saves and configuration (auto-created per game)
- Temp directory for temporary files
- Data directory for packed assets

All file operations route through directory-aware APIs that handle path construction and automatic backup creation when requested.

### Packed Asset System

Assets are stored in `.pack` files with `.manifest` metadata for efficient loading. Two loading strategies optimize memory and startup time:

**Eager Loading** (startup): Font, Gltf, Model, Shader
- Entire pack files loaded into memory during initialization
- Zero-copy access via pointers into memory-mapped data
- Accessed via `GetEagerChunkMap()` returning `EagerChunk` structs

**Lazy Loading** (on-demand): Audio, Islands, Texture
- Loaded by background thread with priority queue
- Memory-efficient for large assets
- Accessed via `GetLazyChunkMap()` returning `LazyChunk` structs

### Lazy Loading System

Priority-based loading with four levels (kLow, kNormal, kHigh, kRealtime) processed by background thread using priority queue. The system supports batch requests, non-blocking status checks, and efficient blocking waits using condition variables.

**Key APIs**:
- `RequestChunkLoad(span<crc>, priority)` - Queue chunks for loading with priority (skips already loaded/requested chunks)
- `IsChunkReady(crc)` - Non-blocking status check
- `WaitForChunks(span<crc>)` - Efficient blocking wait for multiple chunks with condition variable
- `ReadChunkData(crc, offset, span)` - Stream data from chunk (supports both loaded and unloaded chunks via direct file read)

**Priority Textures and Islands**: During startup, Islands and TextureManager populate CRC vectors for critical assets. FileManager automatically queues these with kRealtime priority during `LoadPackFiles()`. Islands handles its own wait via `WaitAndInitializeHeightmaps()`, while Main.cpp waits for priority textures before rendering begins.

**Threading**: Background thread (`LoadingThread()`) processes queue sorted by priority, waking on new requests via `mWakeCondition` and notifying waiters via `mCompletionCondition` on completion. Mutex protects queue and chunk state, condition variables avoid busy-waiting. Eager loading happens in separate async task that completes before background thread starts.

### File Operations

Standard file operations with directory flag support:
- `OpenFile()` - Opens files with optional automatic timestamped backup
- `Exists()`, `GetFileSize()`, `RemoveFile()` - Basic file operations
- All operations support `kAppDataDirectory`, `kTempDirectory`, and `kBackup` flags

### Versioned I/O Templates

Type-safe save/load with automatic version validation:
- `WriteVersionedFile<T>()` - Serializes struct with version header
- `ReadVersionedFile<T>()` - Deserializes with version check
- `ExistsVersionedFile<T>()` - Checks file exists with correct version

Requires structs to define `static constexpr int64_t kiVersion` for versioning.

**Smart Serialization**: Automatically detects and uses binary stream operators when available. Type traits check for `operator<<` and `operator>>` at compile time, excluding built-in types, pointers, and strings to avoid false positives. When custom stream operators are present, uses them for serialization; otherwise falls back to raw binary copy. This allows types to implement explicit serialization logic for future-proofing while maintaining backward compatibility with raw struct copies.

## DifferenceStream.h

Template-based delta compression system for efficient state recording and replay with determinism validation. Records full state at boundaries with only changed states in between, minimizing storage while ensuring replay accuracy.

### Helper Utilities

**TransferViaStream<T>**: Utility function that transfers data between objects using stream operators. Supports non-copyable types by serializing through an intermediate stringstream buffer. Used internally for creating independent copies of saved state during recording initialization.

### Template Types

**DifferenceStreamWriter<SAVED_TYPE, DIFFERENCE_TYPE>**
Records state changes during gameplay. `Update(frame, difference, savedCurrent)` captures CRCs at every frame and writes difference records only when state changes. `Save()` writes three files: header with start/end states, `.frames` with difference data, and `.crcs` with validation data. When `ENABLE_REPLAY_FULL_FRAMES` is defined, also writes `.fullframes` file containing complete state snapshots for every frame.

**DifferenceStreamReader<SAVED_TYPE, DIFFERENCE_TYPE>**
Replays recorded state with validation. `Update(frame, difference, savedCurrent)` reconstructs state at specific frames and validates CRCs against recorded values. Triggers debug break on CRC mismatch to detect non-determinism. When `ENABLE_REPLAY_FULL_FRAMES` is defined and CRC mismatch occurs, performs detailed comparison against full frame snapshot to identify exact differences. Returns false when reaching end of recording.

### Architecture

Uses multi-file approach: header file with start/end states and metadata, `.frames` file with frame-indexed difference records, and `.crcs` file with per-frame CRC validation data. Only frames where state changes are recorded, enabling efficient storage for long recordings with sparse input.

**Debug Builds with ENABLE_REPLAY_FULL_FRAMES**: When preprocessor directive is defined, system additionally stores complete state snapshots in `.fullframes` file for every frame. During replay, if CRC validation fails, compares current state against full snapshot using `common::BreakOnNotEqual()` to provide detailed diagnostics of state divergence. Enables pinpointing exact fields/objects that deviate during non-determinism debugging.

**Requirements**:
- Both template types need stream operators for serialization
- DIFFERENCE_TYPE needs equality operator for change detection
- SAVED_TYPE must provide `Crc()` method returning `common::crc_t`
- When using full frame debugging, SAVED_TYPE must support comparison in `common::BreakOnNotEqual()`

### Use Cases

Designed for deterministic input replay with validation. GameBase uses this for replay system, capturing input changes and validating state consistency. CRC validation enables immediate detection of non-determinism during development and debugging. Full frame storage provides detailed debugging when determinism issues occur.
