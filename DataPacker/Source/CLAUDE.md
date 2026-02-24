# `DataPacker/Source`

Asset preprocessing tool that converts raw assets (textures, models, shaders, audio, fonts) into optimized binary formats for runtime loading.

## Architecture

### Main.cpp - Entry Point & Orchestration
Coordinates the asset processing pipeline through six phases:
1. Pre-export phase (Scene, Islands) -- generates intermediate assets consumed by later phases
2. Irradiance cubemap generation -- offline diffuse IBL convolution
3. Pre-filtered cubemap generation -- offline specular IBL prefiltering
4. Main export phase (Audio, Font, Model, Shader, Texture, Raw) -- parallel async processing
5. Header generation -- produces `DataTypes.h` and `Data.h`, split so files needing only the enum avoid recompilation when asset CRCs change
6. Attribution collection -- copies ThirdParty license files to output

In debug builds, uses CRT debug heap for memory leak detection with break-on-allocation support. Returns non-zero exit code on any export failure so MSBuild can halt the build.

### FileManager - Path & SDK Management
Singleton (`gpFileManager`) that manages input directories (Engine Data and Project Data), output directory, temp directory (project-specific subdirectory in system temp), and Vulkan SDK path (from `VK_SDK_PATH` environment variable). Also handles collecting ThirdParty license files into an Attribution directory with timestamp-based dirty checking.

### Texture - Image Processing
Utility class for loading images from multiple formats and compressing them to GPU-friendly block-compressed or uncompressed formats. Generates mipmaps with box-filter downsampling. Compression is thread-safe after one-time static initialization.

## Design Patterns

**Template-based processing**: `RunExportJobs<T>()` provides type-safe job management with consistent dirty checking and parallel execution across all asset types. Each export job type provides static `Handles()`, `kName`, and implements `Export()`.

**Atomic writes**: Output files written to temp directory first, then atomically renamed to final location only on success. Prevents partial writes from breaking builds.

**Content-based updates**: Generated headers only overwritten when content differs, preventing unnecessary game recompilation.

**Deterministic output**: Assets sorted by relative path (case-insensitive) before processing to ensure chunk ordering is consistent across runs, optimizing Steam patching.

## Output Structure

Each asset type produces three files: `.manifest` (CRC-to-chunk-location mapping), `.pack` (binary data), `.h` (C++ CRC constants). Additionally generates `DataTypes.h` (enum and names only) and `Data.h` (includes all CRC headers), plus an `Attribution/` directory with ThirdParty licenses.

## See Also
- [ExportJobs/CLAUDE.md](ExportJobs/CLAUDE.md) - Individual asset type processors and base class pipeline
