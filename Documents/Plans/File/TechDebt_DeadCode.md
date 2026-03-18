# Tech Debt: Dead Code

Source: /external-tech-debt on Engine/Source/File

## Changes

### Engine/Source/File/FileManager.h
- Remove `GetFileSize()` declaration (line 113) [~5m]
- Remove unused `eDataType` member from `LazyChunk` struct (line 62) [~5m]

### Engine/Source/File/FileManager.cpp
- Remove `GetFileSize()` definition (lines 79-82) [~5m]
