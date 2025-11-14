# `DataPacker/Source`

Asset preprocessing tool that converts raw assets into optimized binary formats for runtime loading.

## Build & Usage

**Executable**: `DataPacker.exe`
**When**: Automatically runs as pre-build event in Projects
**Command**: `DataPacker.exe [engine_data_dir] [project_data_dir] [output_dir] [subfolder]`
**Output**: `/Projects/*/Platforms/VisualStudio2022/Output/Data/[ProjectName]/`
**Project Name**: Automatically extracted from `[project_data_dir]` parent folder name

## Key Components

### Main.cpp - Entry Point & Orchestration
- **Phases**:
  1. **Pre-export**: glTF, Islands (can create new assets)
  2. **Main export**: Audio, Font, Model, Shader, Texture
  3. **Data.h generation**: Unified header with enums
  4. **Attribution**: ThirdParty license collection
- **Features**:
  - Parallel processing via `std::async`
  - Dirty checking (modification times)
  - Atomic file updates with temp files
  - Memory leak detection in debug

### FileManager - Path & SDK Management
- **Singleton**: `gpFileManager`
- **Manages**:
  - Input dirs: `Engine/Data/`, `Project/Data/`
  - Output dir: Platform-specific build output with project subdirectory
  - Project name: Extracted from project data directory parent folder
  - Temp dir: System temp under `DataPacker/`
- **SDK Discovery**:
  - Vulkan SDK: `VK_SDK_PATH` environment variable (for shader compilation)
- **Clean Export**: Force regeneration when debugger attached
- **Project Name Extraction**:
  - Parses project name from `[project_data_dir]` by taking parent folder name
  - Example: `.../Projects/BrokenEngineSandbox/Data` → `BrokenEngineSandbox`
  - Appends project name as subdirectory to output path
- **CopyThirdPartyLicenses()**:
  - Collects license files from `/ThirdParty/*/` subdirectories
  - Copies to `[output_dir]/Attribution/[library_name]/`
  - **Priority system for license file selection**:
    - **Priority 1**: LICENSE, LICENSE.md, or LICENSE.txt (exact match, case-insensitive)
      - If found, copies ONLY that file and skips all other files
    - **Fallback**: If no priority file found, copies all alternative license files
      - Includes: licence, copying, manual.md, readme (substring match, case-insensitive)
  - Preserves directory structure per ThirdParty library
  - Dirty checking via modification times (only copies when source newer than destination)
  - Only creates attribution directory when files need copying
  - Conditional logging: only logs when files actually copied
  - Uses manual indent control (`LOG_INDENT()`) instead of scoped indentation
  - Asserts if any ThirdParty library missing license files

### Texture - Image Processing
- **Formats In**: PNG, TGA, JPG, KTX, EXR, raw (.r32)
- **Compression**: BC4, BC7, R16_UNORM, R8G8B8A8_UNORM
- **Features**:
  - Auto-mipmap generation (down to 4x4)
  - Gamma correction for EXR
  - Thread-safe static initialization

## Output Files

**Directory Structure**:
```
Output/Data/[ProjectName]/
├── Audio.manifest, Audio.pack, Audio.h
├── Font.manifest, Font.pack, Font.h
├── Gltf.manifest, Gltf.pack, Gltf.h
├── Islands.manifest, Islands.pack, Islands.h
├── Model.manifest, Model.pack, Model.h
├── Shader.manifest, Shader.pack, Shader.h
├── Texture.manifest, Texture.pack, Texture.h
├── Data.h
└── Attribution/
    ├── [library_name]/LICENSE
    ├── [library_name]/README.md
    └── ...
```

**Per Asset Type** (Audio, Font, Gltf, Islands, Model, Shader, Texture):
- `.manifest` - CRC → chunk location mapping
- `.pack` - Binary asset data
- `.h` - C++ header with CRC constants

**Unified Header**:
- `Data.h` - Includes all type headers + `DataType` enum

**Attribution Files**:
- `Attribution/[library_name]/` - ThirdParty license files organized by library

## Processing Pipeline

```
Raw Assets → DataPacker → Binary Chunks + Headers + Attribution
             ↓
    Phase 1: Pre-export (creates assets)
    Phase 2: Main export (processes all)
    Phase 3: Data.h generation
    Phase 4: License collection
             ↓
    Dirty Check → Parallel Jobs → Atomic Write
```

## Important Patterns

- **Template Pattern**: `RunExportJobs<T>()` for type-safe job management
- **Optimization**: `ContentsEqual()` prevents unnecessary recompilation
- **Consistency**: Files sorted by path for deterministic chunk ordering
- **Caching**: Temp files avoid reprocessing unchanged assets

## Warnings

- Clean export mode bypasses all caching
- Pre-export phase must complete before main export
- Vulkan SDK path must be valid for shader compilation

## See Also
- [ExportJobs/CLAUDE.md](ExportJobs/CLAUDE.md) - Asset-specific processors
