# `/Engine/Source/File/`

Centralized file I/O, packed asset loading, and state recording/replay.

**Global**: `gpFileManager`

## FileManager

Manages file operations and asset loading with platform directory access (AppData, Temp) via `FileFlags`.

### Packed Asset System

Assets stored in `.pack` files with `.manifest` metadata, using two loading strategies:

- **Eager Loading** (Font, Scene, Model, Raw, Shader): Entire pack files loaded into memory at startup with zero-copy access via pointers into the loaded data.
- **Lazy Loading** (Audio, Islands, Texture): A background thread processes a priority queue using unbuffered disk I/O. All lazy data is pre-allocated in a single `VirtualAlloc` pool. Textures go through a multi-stage state machine coordinating with TextureUploadManager; non-texture chunks become ready immediately after disk load.
- **Priority Loading**: IslandTerrain and TextureManager populate priority CRC vectors at startup, queued at realtime priority before normal requests.

**Device Recreation**: After GPU device loss, `ResetTextureChunkStates()` restores lazy chunks to a re-loadable state based on whether CPU data is still resident.

### Versioned I/O Templates

Type-safe save/load with automatic version validation via `WriteVersionedFile<T>()` / `ReadVersionedFile<T>()` / `ExistsVersionedFile<T>()`. Requires structs to define `static constexpr int64_t kiVersion`.

## DifferenceStream.h

Template-based delta compression for deterministic state recording and replay. Records full state at boundaries with only changed states between frames.

- **DifferenceStreamWriter**: Records state changes during gameplay, capturing CRC checksums every frame but only writing difference records when state actually changes. Saves header plus `.frames`, `.checksums`, and optionally `.fullframes` files.
- **DifferenceStreamReader**: Replays recorded state with CRC validation at each frame. Provides a split API (`LoadDifference()` + `ValidateChecksum()`) for callers that need to inject state between loading and validation, plus a combined `Update()` for simple cases.

### Requirements
- Both template types need stream operators for serialization
- `DIFFERENCE_TYPE` needs equality operator for change detection
- `SAVED_TYPE` must provide `Crc()` method returning `common::crc_t`
