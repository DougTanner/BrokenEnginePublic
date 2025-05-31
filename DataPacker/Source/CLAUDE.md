# /DataPacker/Source/

The asset preprocessing tool that converts raw assets into optimized binary formats.

## Core Files in `/DataPacker/Source/`

### Main.cpp
- **Purpose**: Entry point and main processing orchestration
- **Key functions**:
  - `main()` - Entry point with exception handling and memory leak detection
  - `MainThread()` - Coordinates the export process and initialization
  - `RunExportJobs<T>()` - Template function for type-safe parallel job management
- **Processing phases**:
  1. Pre-export: glTF files, Island files (can create new textures/models)
  2. Main export: Audio, Font, Model, Shader, Texture files
- **Features**: Command line argument parsing, parallel execution via std::async

### FileManager.h & FileManager.cpp
- **Purpose**: Manages input/output directories and SDK paths
- **Key class**: `FileManager` (singleton accessed via `gpFileManager`)
- **Key functionality**:
  - Input directory management (Engine/Data and project-specific Data folders)
  - Output directory management (platform-specific output folder)
  - Temp directory management (system temp under `DataPacker/`)
  - SDK path discovery (Windows SDK and Vulkan SDK)
  - Clean export mode control (`mbCleanExport`)
- **SDK integration**: 
  - Windows SDK: Searches in `C:\Program Files (x86)\Windows Kits\10\bin\` or registry
  - Vulkan SDK: Uses `VK_SDK_PATH` environment variable

### Texture.h & Texture.cpp
- **Purpose**: Image loading, format conversion, and texture compression utilities
- **Key class**: `Texture` - handles image data and compression
- **Key functionality**:
  - Image loading (PNG, TGA, JPG via stb_image; KTX via gli; EXR via OpenEXR; raw .r32)
  - Texture compression (BC4, BC7, R16_UNORM, R8G8B8A8_UNORM)
  - Mipmap generation with filtering
  - Gamma correction support
  - Format conversion utilities
  - Thread-safe static initialization
- **Special features**:
  - Automatic mipmap generation down to 4x4 for BC formats
  - EXR gamma correction when requested

### Pch.h & Pch.cpp
- **Purpose**: Precompiled header for faster compilation
- **Contents**: Common includes and standard library headers used throughout DataPacker

## Output Structure

For each asset type, generates three files:
- **`.manifest`**: CRC → chunk location mapping
- **`.pack`**: Binary asset data
- **`.h`**: C++ header with CRC constants

Example: `Audio.manifest`, `Audio.pack`, `Audio.h`

## Command Line Arguments
```
DataPacker.exe [engine_data_dir] [project_data_dir] [output_dir] [subfolder]
```
Defaults to paths relative to executable if no arguments provided.


## Export Job Names
- **Audio**: "Audio"
- **Font**: "Font"  
- **Gltf**: "Gltf"
- **Island**: "Islands" (note: plural)
- **Model**: "Model"
- **Shader**: "Shader"
- **Texture**: "Texture"

## Processing Details

The `RunExportJobs<T>()` template function:
1. Checks if output files are dirty (modification time comparison)
2. Collects matching files from input directories
3. Sorts by relative path for consistent chunk ordering
4. Launches async tasks for parallel processing
5. Uses temp files with atomic rename on success
6. Only updates headers if content changed

## Clean Export Mode
- Enabled when debugger attached or `mbCleanExport` flag set
- Forces full regeneration regardless of modification times

## See Also
- Export Jobs: [ExportJobs/CLAUDE.md](ExportJobs/CLAUDE.md)
