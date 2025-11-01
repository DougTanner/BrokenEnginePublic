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
| Font, Gltf, Islands, Model, Shader | Eager (startup) | `mEagerChunkMap` | `GetEagerChunkMap()` |
| Audio, Texture | Lazy (on-demand) | `mLazyChunkMap` | `GetLazyChunkMap()` |

- `RequestChunkLoad(crc, priority)` - Queue lazy loading
- `IsChunkReady(crc)` - Check load status
- `ReadChunkData(crc, offset, std::span<byte>)` - Stream data from chunk (thread-safe)
- Background thread for lazy loading
- Memory-mapped for zero-copy access

### File Operations
- `OpenFile()` - With automatic backup (kBackup flag)
- `Exists()`, `GetFileSize()`, `RemoveFile()`

### Versioned I/O Templates
- `WriteVersionedFile<T>()` - Save with version validation
- `ReadVersionedFile<T>()` - Load with version check
- Requires `static constexpr int64_t kiVersion`

## DifferenceStream.h

Delta compression for efficient state recording/replay.

### Writer<SAVED_TYPE, DIFFERENCE_TYPE>
- Records only changed states with timestamps
- `Update(frame, difference)` - Record change
- `Save()` - Write header + .frames data

### Reader<SAVED_TYPE, DIFFERENCE_TYPE>
- Replays state at specific frames
- `Update(frame, difference)` - Get interpolated state
- `Loaded()` - Check load success

### Use Cases
- Save states, input recording, replays, network sync
- Requires `kiVersion` and `operator==` on DIFFERENCE_TYPE

## Asset Loading Flow
```
Startup: Manifest → Memory-map packs → Build chunk maps
         ├─ Eager: Load immediately
         └─ Lazy:  Load on background thread when requested
```
