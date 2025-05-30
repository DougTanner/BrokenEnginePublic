# /DataPacker/Source/

The `/DataPacker/Source/` directory contains the DataPacker which is a multi-threaded asset processing tool that converts raw assets into optimized binary formats for the game engine. It preprocesses textures, models, shaders, audio, fonts, and other assets into efficient formats suitable for runtime loading.

## Source Files

### /DataPacker/Source/Main.cpp
- **Purpose**: Entry point and main processing orchestration
- **Key functions**:
  - `main()` - Entry point with exception handling and memory leak detection
  - `MainThread()` - Coordinates the export process and initialization
  - `RunExportJobs<T>()` - Template function for type-safe parallel job management
- **Processing order**:
  1. Pre-export: glTF files, Island files (can create new textures/models)
  2. Main export: Audio, Font, Model, Shader, Texture files
- **Features**: Command line argument parsing, parallel execution via std::async

### /DataPacker/Source/FileManager.h & FileManager.cpp
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

### /DataPacker/Source/Texture.h & Texture.cpp
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

### /DataPacker/Source/Pch.h & Pch.cpp
- **Purpose**: Precompiled header for faster compilation
- **Contents**: Common includes and standard library headers used throughout DataPacker

### /DataPacker/Source/ExportJobs/
- **Purpose**: Contains all export job implementations
- **See**: `ExportJobs/CLAUDE.md` for detailed information about each export job type

### /DataPacker/Source/ThirdParty/
Third-party library wrappers that include headers from `/ThirdParty/`:
- **SPIRV-Cross.cpp**: SPIR-V reflection and cross-compilation wrapper
- **bc7enc_rdo.cpp**: BC4/BC7 texture compression wrapper
- **stb.cpp**: STB image loading library wrapper
- **tinygltf.cpp**: glTF 2.0 parsing wrapper
- **tinyobjloader.cpp**: Wavefront OBJ parsing wrapper
- **openexr/**: OpenEXR image format support
  - OpenEXRConfig.h, openexr.c: Configuration and implementation

## Architecture

### Multi-Stage Processing
The DataPacker processes assets in two stages:
1. **Pre-export phase**: glTF and Island files are processed first as they can create new textures and models
2. **Main export phase**: All other asset types (audio, fonts, models, shaders, textures) are processed

### Output Structure
For each asset type, the DataPacker generates three files:
- **`.manifest`**: Contains chunk locations (CRC, offset, size) for each asset
- **`.pack`**: Contains the actual binary data for all assets of that type
- **`.h`**: C++ header file with CRC constants for compile-time asset references

Each asset type has its own set of files (e.g., `Audio.manifest`, `Audio.pack`, `Audio.h`).

### Threading Model
- Each export job runs in its own thread via `std::async`
- Thread-local storage is used for thread safety
- Uses templated `RunExportJobs<T>()` function for type-safe job management

## Key Components

### Dirty Checking & Caching
- Compares file modification times to determine if re-export is needed
- Caches processed chunks in temp directory
- Only regenerates header files if content changes (prevents unnecessary recompilation)
- Clean export mode forces full regeneration

## Build Process Integration

### Command Line Arguments
```
DataPacker.exe [engine_data_dir] [project_data_dir] [output_dir] [subfolder]
```
If no arguments provided, uses default paths relative to executable location:
- Engine data: `../../../Engine/Data`
- Project data: `../../../Projects/BrokenEngineSandbox/Data`
- Output directory: `../../../Projects/BrokenEngineSandbox/Platforms/VisualStudio2022/Output`
- Subfolder: `Data`

### Pre-Build Event
The DataPacker runs as a pre-build step in Visual Studio projects, ensuring assets are up-to-date before compilation.

### Clean Export Mode
- Automatically enabled when debugger is attached (`IsDebuggerPresent()`)
- Can be forced via `mbCleanExport` flag in FileManager
- Forces regeneration of all assets regardless of modification times
- Removes all existing output files before export

## CRC System
- Uses CRC64 hashes generated from relative file paths
- CRCs serve as unique identifiers for chunks at runtime
- Header files contain compile-time constants for each asset:
  ```cpp
  inline constexpr common::crc_t kTextureExplosionCrc = 12345678;
  ```

## Data Format
All output files use the common data format:
- **Header**: Contains magic number, version, and chunk count
- **Chunks**: Individual assets with headers and data
- **Alignment**: 16-byte alignment for optimal memory access

## Export Job Names
Each export job type has a static `kpcName` that determines output filenames:
- **Audio**: "Audio" → Audio.manifest, Audio.pack, Audio.h
- **Font**: "Font" → Font.manifest, Font.pack, Font.h  
- **Gltf**: "Gltf" → Gltf.manifest, Gltf.pack, Gltf.h
- **Island**: "Islands" → Islands.manifest, Islands.pack, Islands.h (note: plural name)
- **Model**: "Model" → Model.manifest, Model.pack, Model.h
- **Shader**: "Shader" → Shader.manifest, Shader.pack, Shader.h
- **Texture**: "Texture" → Texture.manifest, Texture.pack, Texture.h


## Export Jobs
See `ExportJobs/CLAUDE.md` for detailed information about each export job type.

## Performance Optimizations
- Parallel processing of export jobs using `std::async`
- Incremental builds via dirty checking
- Efficient caching system in temp directory
- Export jobs sorted by relative path (case-insensitive) for consistent chunk ordering
- Sorted output provides better Steam patching efficiency by minimizing binary diffs

## Error Handling
- Exception catching from async tasks
- Cleanup of temporary files on failure

## Processing Details

### Export Job Processing
The `RunExportJobs<T>()` template function handles the export process for each asset type:
1. Checks if output files exist and are dirty
2. Collects all matching files from input directories
3. Sorts jobs by relative path for consistent ordering
4. Launches async tasks for each export job
5. Writes temporary files first, then renames on success
6. Only updates header files if content changed (prevents unnecessary recompilation)

## Temporary File Handling
- Temporary files are created in system temp directory under `DataPacker/` folder
- Format: `{AssetType}.manifest`, `{AssetType}.pack`, `{AssetType}.h` 
- On success: Temporary files are renamed to final output location
- On failure: Temporary files are deleted
- Header files are only replaced if content differs from existing file

