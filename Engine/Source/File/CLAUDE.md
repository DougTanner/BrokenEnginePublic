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

Priority-based loading with four levels (kLow, kNormal, kHigh, kRealtime) processed by background thread. The system supports batch requests, non-blocking status checks, and efficient blocking waits using condition variables.

**Key APIs**:
- `RequestChunkLoad(span<crc>, priority)` - Queue chunks for loading with priority
- `IsChunkReady(crc)` - Non-blocking status check
- `WaitForChunks(span<crc>)` - Efficient blocking wait for multiple chunks
- `ReadChunkData(crc, offset, span)` - Stream data from chunk (supports unloaded chunks)

**Priority Textures and Islands**: During startup, Islands and TextureManager populate CRC vectors for critical assets. FileManager automatically queues these with kRealtime priority. Islands handles its own wait via `WaitAndInitializeHeightmaps()`, while Main.cpp waits for priority textures before rendering begins.

**Threading**: Background thread processes queue sorted by priority, waking on new requests and notifying waiters on completion. Mutex protects queue and chunk state, condition variables avoid busy-waiting.

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

Template-based delta compression system for efficient state recording and replay. Records full state at boundaries with only changed states in between, minimizing storage for deterministic replay.

### Template Types

**DifferenceStreamHeader<SAVED_TYPE, DIFFERENCE_TYPE>**
Contains full state snapshots at stream boundaries (savedStart/savedEnd), initial difference, frame counter, and combined version number from both types.

**DifferenceStreamWriter<SAVED_TYPE, DIFFERENCE_TYPE>**
Records state changes during gameplay. `Update(frame, difference)` only writes when state changes, `Save()` writes header and frame data to separate files.

**DifferenceStreamReader<SAVED_TYPE, DIFFERENCE_TYPE>**
Replays recorded state. `Update(frame, difference, bIterate)` reconstructs state at specific frames, optionally advancing playback. Returns false when reaching savedEnd.

### Architecture

Uses two-file approach: versioned header file and `.frames` data file. Only changed states are recorded with frame numbers, enabling efficient storage for long recordings. Requires `kiVersion` on both template types and `operator==` on DIFFERENCE_TYPE for change detection.

### Use Cases

Designed for deterministic input replay, save states with minimal storage, and network synchronization. GameBase uses separate writer/reader pairs for held and pressed input to enable accurate replay of frame-by-frame input state.
