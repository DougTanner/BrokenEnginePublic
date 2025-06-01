# /Engine/Source/File/

The `/Engine/Source/File/` directory contains the file management system for loading packed assets, save data operations, and state recording/replay.

## File Overview

### FileManager.h & FileManager.cpp

**Global Access**: `gpFileManager`

**Purpose**: Centralized file I/O operations and packed asset loading

**Key Components**:

#### Directory Management
- `kAppDataDirectory` - User's roaming AppData folder for saves/config
- `kTempDirectory` - System temp folder for temporary files
- Automatic creation of game-specific subdirectories
- Global log file redirection to AppData (Debug.txt/Profile.txt/Release.txt)

#### Packed Asset Loading
- `GetChunkMap()` - Unified access to all chunks indexed by CRC from multiple pack files
- Uses data type information from `data::kpcDataTypeNames` array to load pack files
- Asynchronous loading using std::async for parallel pack file loading
- Memory-mapped chunks for zero-copy asset access
- Supports all asset types: Audio, Font, Gltf, Islands, Model, Shader, Texture

#### File Operations
- `OpenFile()` - Opens files with automatic backup support (kBackup flag)
- `Exists()` - Check file existence
- `GetFileSize()` - Get file size in bytes
- `RemoveFile()` - Delete files

#### Versioned File I/O Templates
- `WriteVersionedFile<T>()` - Write structs with version/size validation
- `ReadVersionedFile<T>()` - Read structs with automatic version checking
- `ExistsVersionedFile<T>()` - Check if versioned file exists and is valid
- Requires types to have `static constexpr int64_t kiVersion`

**Key Types**:
- `Chunk` - Wrapper containing ChunkHeader* and data pointer
- `FileFlags` - Enum flags for directory and operation modes

### DifferenceStream.h

**Purpose**: Efficient state recording/replay system using delta compression

**Key Classes**:

#### DifferenceStreamWriter<SAVED_TYPE, DIFFERENCE_TYPE>
- Records state changes only when they differ from previous state
- Stores initial state + timestamped differences
- `Update(frame, difference)` - Record state change at frame
- `Save()` - Write header file + separate .frames data file

#### DifferenceStreamReader<SAVED_TYPE, DIFFERENCE_TYPE>  
- Replays recorded state changes at specific frames
- `Update(frame, difference)` - Get state at given frame
- `Loaded()` - Check if data was successfully loaded
- Automatic interpolation between recorded differences

**Use Cases**:
- Save states for game progress
- Input recording for replays
- Deterministic simulation recording
- Network state synchronization

**Requirements**:
- Types must have `kiVersion` static member
- DIFFERENCE_TYPE must implement `operator==`
- Generates two files: header + .frames data
