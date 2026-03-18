# Architecture: FileManager.cpp File Split

Source: /external-architecture-review on Engine/Source/File

FileManager.cpp is 685 lines (exceeds 500-line soft guideline). Split into multiple .cpp files sharing the single FileManager.h, organized by responsibility.

**Execute after**: TechDebt_DeadCode.md and TechDebt_Duplication.md (line numbers shift after those changes).

## Changes

### Engine/Source/File/FileManager.cpp (keep ~120 lines)
- Keep: constructor, destructor, file operations (Exists, OpenFile, RemoveFile, GetFilePath, GetDataFilePath) [~15m]
- Keep: IsEagerChunk(), DataTypeFromFlags() constexpr functions
- Keep: gpFileManager lifecycle, log file setup
- Keep: RequestTextureChunkLoad() free function

### Engine/Source/File/FileManagerEagerLoading.cpp (new, ~150 lines)
- Move: LoadPackFiles() function (lines 159-306) [~30m]
- Move: GetEagerChunkMap() (lines 308-318)
- This file handles manifest reading, eager chunk map building, and async eager load pipeline

### Engine/Source/File/FileManagerLazyLoading.cpp (new, ~280 lines)
- Move: LoadingThread() (lines 384-414) [~15m]
- Move: LoadChunk() (lines 416-485)
- Move: RequestChunkLoad() (lines 332-364)
- Move: WaitForChunks() (lines 366-382)
- Move: NotifyChunkCompletion() (lines 487-491)
- Move: IsChunkReady() (lines 325-330)
- Move: GetLazyChunk() (lines 493-496)
- Move: GetLazyChunkMap() (lines 320-323)
- Move: ResetTextureChunkStates() (lines 498-531)
- Move: ReadChunkData() (lines 533-607) — streaming I/O from lazy chunks, belongs with lazy loading

### Engine/Source/File/FileManagerStats.cpp (new, ~30 lines)
- Move: GetEagerStats() and GetLazyStats() (post-duplication-fix versions) [~5m]
- Move: GetMemoryStats() (lines 655-677)

### Project .vcxproj files
- Add new .cpp files to BrokenEngineSandbox and BrokenEngineSandboxServer vcxproj and filters [~10m]

## Verification Notes
- ReadChunkData() moved to lazy loading file (not stats) since it's core I/O for audio streaming
- GetFileSize() removed by TechDebt_DeadCode.md, not listed in keep
- Stats methods reflect post-duplication-fix API (2 methods instead of 4)
