# `/Engine/Source/File/`

Centralized file I/O, packed asset loading, and state recording/replay.

**Global**: `gpFileManager`

## FileManager

### Directory Management
- **kAppDataDirectory** - User AppData for saves/config  
- **kTempDirectory** - System temp folder
- Auto-creates game-specific subdirectories
- Redirects logs to AppData (Debug.txt/Profile.txt/Release.txt)

### Packed Asset System
| Type | Loading | Storage | Access Method |
|------|---------|---------|---------------|
| Font, Gltf, Model, Shader | Eager (startup) | `mEagerChunkMap` | `GetEagerChunkMap()` |
| Audio, Islands, Texture | Lazy (on-demand, priority) | `mLazyChunkMap` | `GetLazyChunkMap()` |

**Priority Loading**:
- Islands and priority textures are lazy-loaded with kRealtime priority during startup
- `Islands::smIslandCrcs` contains island CRCs collected during Islands construction
- `TextureManager::smPriorityTextures` contains critical texture CRCs
- Islands class handles its own loading via `WaitAndInitializeHeightmaps()`
- Main.cpp separately waits for `TextureManager::smPriorityTextures` before rendering

#### Lazy Loading APIs
- `RequestChunkLoad(std::span<const common::crc_t> crcs, priority)` - Queue lazy loading with priority (kLow, kNormal, kHigh, kRealtime)
  - Takes span of CRCs for batch loading efficiency
  - Locks mutex once for entire batch
  - Skips already loaded or requested chunks
  - Wakes background thread only if any chunks were queued
- `IsChunkReady(crc)` - Check load status (non-blocking)
- `WaitForChunks(std::span<const common::crc_t>)` - Efficiently blocks until all chunks loaded
  - Requests all lazy chunks with kRealtime priority via RequestChunkLoad()
  - Silently skips eager chunks (already loaded)
  - Uses condition variable for efficient waiting (no busy-wait)
  - Only wakes when all requested chunks complete
- `ReadChunkData(crc, offset, std::span<byte>)` - Stream data from chunk (thread-safe)

#### Load Priorities
- **kLow** - Background assets
- **kNormal** - Standard on-demand loading (default)
- **kHigh** - Important assets needed soon
- **kRealtime** - Critical assets needed immediately (used by WaitForChunks)

#### Threading
- Background loading thread processes queue by priority
- Completion notifications via condition variable
- Memory-mapped for zero-copy access

### File Operations
- `OpenFile()` - With automatic backup (kBackup flag)
- `Exists()`, `GetFileSize()`, `RemoveFile()`

### Versioned I/O Templates
- `WriteVersionedFile<T>()` - Save with version validation
- `ReadVersionedFile<T>()` - Load with version check
- Requires `static constexpr int64_t kiVersion`

## DifferenceStream.h

Delta compression for efficient state recording/replay. Records full state at start/end with only deltas in between.

### DifferenceStreamHeader<SAVED_TYPE, DIFFERENCE_TYPE>
- Full state snapshots at stream boundaries (savedStart, savedEnd)
- Initial difference and frame counter
- Combined version from SAVED_TYPE and DIFFERENCE_TYPE

### DifferenceStreamWriter<SAVED_TYPE, DIFFERENCE_TYPE>
- Records only changed states with frame numbers
- `Update(frame, difference)` - Record changes (skips if unchanged)
- `Save()` - Write header + .frames data file
- Reserves space for 1024 frames by default

### DifferenceStreamReader<SAVED_TYPE, DIFFERENCE_TYPE>
- Replays state at specific frames
- `Update(frame, difference, bIterate)` - Get state at frame, optionally advance
- `GetRecordedFrameCount()` - Total frame count
- `Loaded()` - Check if data loaded successfully
- Returns false when reaching savedEnd frame

### Use Cases
- Input recording/replay for deterministic playback
- Save states with minimal storage
- Network synchronization
- Requires `kiVersion` on both types and `operator==` on DIFFERENCE_TYPE

## Asset Loading Flow
```
Startup: Manifest → Memory-map packs → Build chunk maps
         ├─ Eager: Load immediately
         └─ Lazy:  Load on background thread when requested
```
