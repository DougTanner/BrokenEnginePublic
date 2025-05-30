# /Engine/Source/File/

The `/Engine/Source/File/` directory contains the file management system for loading packed assets and handling save data.

## FileManager - `/Engine/Source/File/FileManager.h` / `/Engine/Source/File/FileManager.cpp`

**Global Access**: `gpFileManager`

**Purpose**: Centralized file I/O operations, loads packed binary assets from Data.bin and Textures.bin.

**Key Features**:
- `GetDataChunkMap()` - Access to packed Data.bin chunks indexed by CRC
- `GetTexturesChunkMap()` - Access to packed Textures.bin chunks indexed by CRC
- `WriteVersionedFile()` / `ReadVersionedFile()` - Type-safe file I/O with version validation
- `Exists()`, `GetFileSize()`, `OpenFile()`, `RemoveFile()` - Basic file operations
- Directory management for AppData and Temp folders
- Automatic log file creation and management

**FileFlags**: 
- `kAppDataDirectory` - User's roaming AppData folder
- `kTempDirectory` - System temp folder
- `kRead`, `kWrite`, `kBackup` - File operation modes

**Key Types**:
- `Chunk` - Contains header and data pointer for packed assets

## DifferenceStream - `/Engine/Source/File/DifferenceStream.h`

**Purpose**: Template-based system for recording and replaying state changes over time.

**Key Classes**:
- `DifferenceStreamWriter<SAVED_TYPE, DIFFERENCE_TYPE>` - Records state changes when they differ from previous
- `DifferenceStreamReader<SAVED_TYPE, DIFFERENCE_TYPE>` - Replays recorded state changes at specific frames

**Key Features**:
- Only stores differences when state actually changes
- Frame-accurate replay system
- Used for save states, recordings, and deterministic replay

**Requirements**:
- Types must have `kiVersion` static member
- DIFFERENCE_TYPE must implement equality operator
- Saves header file + separate .frames file for actual data