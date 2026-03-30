# `DataPacker/Source`

Asset preprocessing tool that converts raw assets (textures, models, shaders, audio, fonts) into optimized binary formats for runtime loading.

## Architecture

### Main.cpp - Entry Point & Orchestration
Enforces single-instance execution at startup (a second instance waits for the first to finish before proceeding), then coordinates the asset processing pipeline through six phases:
1. Pre-export phase (Scene, Islands) -- generates intermediate assets consumed by later phases (Scene produces `.MODEL` files and extracted textures for Model/Texture export; Islands produces GPU textures for Texture export)
2. Irradiance cubemap generation -- offline diffuse IBL convolution
3. Pre-filtered cubemap generation -- offline specular IBL prefiltering
4. Main export phase (Audio, Font, Model, Shader, Texture, Raw) -- parallel async processing
5. Header generation -- produces `DataTypes.h` (enum/names only) and `Data.h` (includes all CRC headers), split so files needing only the enum avoid recompilation when asset CRCs change
6. Attribution collection -- copies ThirdParty license files to output

### FileManager - Path & SDK Management
Singleton (`gpFileManager`) managing input directories, output directory, temp directory, and Vulkan SDK path. Also handles collecting ThirdParty license files with timestamp-based dirty checking.

### Texture - Image Processing
Loads images from multiple formats and compresses to GPU-friendly block-compressed or uncompressed formats with mipmap generation.

## Design Patterns

- **Template-based processing**: `RunExportJobs<T>()` provides type-safe job management with dirty checking and parallel execution. Each export job type provides static `Handles()`, `kName`, and implements `Export()`
- **Atomic writes**: Output files written to temp first, then renamed to final location on success
- **Content-based updates**: Generated headers only overwritten when content differs, preventing unnecessary recompilation
- **Deterministic output**: Assets sorted by relative path before processing to ensure consistent chunk ordering across runs

## Output Structure

Each asset type produces three files: `.manifest` (CRC-to-chunk-location mapping), `.pack` (binary data), `.h` (C++ CRC constants). Also generates `DataTypes.h`, `Data.h`, and an `Attribution/` directory with ThirdParty licenses.

## See Also
- [ExportJobs/CLAUDE.md](ExportJobs/CLAUDE.md) - Individual asset type processors and base class pipeline
