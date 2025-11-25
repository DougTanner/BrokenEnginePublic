# `DataPacker/Source`

Asset preprocessing tool that converts raw assets (textures, models, shaders, audio, fonts) into optimized binary formats for runtime loading.

## Architecture

### Main.cpp - Entry Point & Orchestration
Entry point that coordinates the entire asset processing pipeline through four phases:
1. Pre-export phase (glTF, Islands) - can generate new assets
2. Main export phase (Audio, Font, Model, Shader, Texture) - parallel processing
3. Data.h generation - unified header with enums and includes
4. Attribution collection - ThirdParty license files

Uses `RunExportJobs<T>()` template function to process each asset type with dirty checking, parallel async execution, and atomic file writes via temp files. Only writes output files when content changes to avoid triggering unnecessary game recompilation.

### FileManager - Path & SDK Management
Singleton (`gpFileManager`) that manages directories and SDK paths:
- Input directories: Engine and Project Data folders
- Output directory: Platform-specific build output with project subdirectory
- Temp directory: System temp for intermediate files
- Vulkan SDK: Discovered via `VK_SDK_PATH` environment variable

**CopyThirdPartyLicenses()**: Collects license files from `/ThirdParty/` subdirectories with priority system (LICENSE files preferred over fallback alternatives like COPYING or README). Uses dirty checking to only copy when source is newer. Asserts if any library missing license.

### Texture - Image Processing
Loads various image formats (PNG, TGA, JPG, EXR, raw float32) via stb_image and OpenEXR libraries. Compresses to GPU-friendly formats (BC4, BC7, R16, R8G8B8A8) using bc7enc_rdo. Generates mipmaps with box-filter downsampling. Thread-safe via mutex-protected compression calls.

## Design Patterns

**Template-based processing**: `RunExportJobs<T>()` provides type-safe job management with consistent dirty checking and parallel execution across all asset types.

**Atomic writes**: All output files written to temp directory first, then atomically renamed to final location only on success. Prevents partial writes from breaking builds.

**Content-based updates**: Headers only overwritten when content differs (via `ContentsEqual()`), preventing unnecessary game recompilation triggers.

**Deterministic output**: Assets sorted by path before processing to ensure chunk ordering is consistent across runs, optimizing Steam patching.

## Output Structure

Each asset type produces three files: `.manifest` (CRC to chunk location), `.pack` (binary data), `.h` (C++ constants). Plus unified `Data.h` header and `Attribution/` directory with ThirdParty licenses.

## See Also
- [ExportJobs/CLAUDE.md](ExportJobs/CLAUDE.md) - Individual asset type processors
- [ThirdParty/CLAUDE.md](ThirdParty/CLAUDE.md) - Third-party library integrations
