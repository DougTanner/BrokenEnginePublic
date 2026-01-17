# `DataPacker/Source`

Asset preprocessing tool that converts raw assets (textures, models, shaders, audio, fonts) into optimized binary formats for runtime loading.

## Architecture

### Main.cpp - Entry Point & Orchestration
Coordinates the asset processing pipeline through four phases:
1. Pre-export phase (glTF, Islands) - can generate intermediate assets for later phases
2. Main export phase (Audio, Font, Model, Shader, Texture) - parallel async processing
3. Data.h generation - unified header with DataTypes enum and includes for all asset headers
4. Attribution collection - copies ThirdParty license files to Attribution directory

Uses `RunExportJobs<T>()` template function to process each asset type with dirty checking, parallel async execution via `std::async`, and atomic file writes via temp files. Only writes header files when content changes to avoid triggering unnecessary game recompilation.

### FileManager - Path & SDK Management
Singleton (`gpFileManager`) that manages directories and SDK paths:
- Input directories: Engine Data and Project Data folders (passed via command line or defaults)
- Output directory: Platform-specific build output
- Temp directory: System temp with project-specific subdirectory for intermediate files
- Vulkan SDK: Discovered via `VK_SDK_PATH` environment variable

**CopyThirdPartyLicenses()**: Collects license files from `/ThirdParty/` subdirectories with priority system (LICENSE/LICENSE.md/LICENSE.txt preferred, falls back to COPYING/README/manual.md). Uses timestamp-based dirty checking. Asserts if any library is missing a license file.

### Texture - Image Processing
Utility class for loading and processing images. Loads various formats (PNG, TGA, JPG via stb_image; EXR via OpenEXR; raw float32 files). Compresses to GPU-friendly formats (BC4, BC7 via bc7enc_rdo; R16_UNORM; R8G8B8A8_UNORM). Generates mipmaps with box-filter downsampling. Thread-safe compression via mutex-protected bc7enc/rgbcx calls.

## Design Patterns

**Template-based processing**: `RunExportJobs<T>()` provides type-safe job management with consistent dirty checking and parallel execution across all asset types. Each export job type provides static `Handles()`, `kpcName`, and implements `Export()`.

**Atomic writes**: Output files written to temp directory first, then atomically renamed to final location only on success. Prevents partial writes from breaking builds.

**Content-based updates**: Headers only overwritten when content differs (via `ContentsEqual()`), preventing unnecessary game recompilation triggers.

**Deterministic output**: Assets sorted by relative path (case-insensitive) before processing to ensure chunk ordering is consistent across runs, optimizing Steam patching.

## Output Structure

Each asset type produces three files: `.manifest` (CRC to chunk location mapping), `.pack` (binary data), `.h` (C++ constants with CRC values). Plus unified `Data.h` header and `Attribution/` directory with ThirdParty licenses.

## See Also
- [ExportJobs/CLAUDE.md](ExportJobs/CLAUDE.md) - Individual asset type processors
- [ThirdParty/CLAUDE.md](ThirdParty/CLAUDE.md) - Third-party library integrations
