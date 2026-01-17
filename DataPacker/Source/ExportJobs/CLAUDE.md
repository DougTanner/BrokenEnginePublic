# `DataPacker/Source/ExportJobs/`

Asset-specific processors that convert raw file formats into optimized binary chunks for runtime loading.

## Architecture

**ExportJob** - Abstract base class defining the export pipeline
- Converts raw assets into binary chunks with `common::ChunkHeader`
- Handles dirty checking via modification timestamps, version tracking, and cached chunk validation
- Provides caching system using temp files with magic number and version header
- Generates CRC64 identifiers from relative file paths
- Ensures 16-byte alignment for all chunks using `common::RoundUp<int64_t, common::kiAlignmentBytes>()`
- Each derived class provides a version number via `GetVersion()`; assets re-export when version changes

**Processing Flow**
1. `CheckDirty()` - Validates pack file exists, compares timestamps, validates cached chunk magic/version
2. `RunExport()` - Loads cached chunk if clean, otherwise calls `Export()` and caches result
3. `Export()` - Pure virtual method where derived classes implement asset-specific conversion
4. `AllocateHeaderAndData()` - Helper that allocates aligned buffer and returns header pointer plus data span

**Two-Phase System**
- **Pre-export**: glTF and Islands generate intermediate assets (textures, model files)
- **Main export**: All asset types process files in parallel via `std::async`

## Asset Processors

**ExportAudio** - Converts WAV files to 16-bit PCM format
- Uses DirectXTK `WAVFileReader` for WAV parsing
- Normalizes 32-bit float samples to 16-bit PCM
- Stores `WAVEFORMATEX` metadata in header

**ExportFont** - Parses BMFont binary format (version 3)
- Extracts line height, base, scale from common block
- Stores character IDs and metrics (x, y, width, height, offsets, xadvance)
- Reads kerning pairs block but exports count as 0

**ExportGltf** - Processes glTF 3D scenes via tinygltf
- Pre-export extracts embedded textures to intermediate `.BC4`/`.BC7_UNORM_BLOCK` files
- Pre-export generates `.GLTF_MODEL` intermediate file with deduplicated vertices and indices
- Main export stores PBR material data with texture CRCs
- Supports metallic-roughness workflow only; applies node hierarchy transforms

**ExportIsland** - Processes terrain data from directory structure
- Converts source textures: elevation (`.r32`), color (`.exr`), normals (`.exr`), ambient occlusion (`.r32`)
- Generates GPU texture (R16_UNORM) and CPU heightmap (float array) from elevation data
- Calculates beach elevation as the most common non-zero elevation value
- Stores texture CRCs and heightmap dimensions in header

**ExportModel** - Converts OBJ and glTF intermediate geometry via tinyobjloader
- Filename tags control vertex format: `[FN]` face normals, `[N]` smooth normals, `[T]` texcoords, `[NT]` both
- Auto-generates normals when not present in source (smooth via vertex position hashing, or per-face)
- Centers geometry at origin using bounding box center
- Deduplicates vertices using memcmp comparison
- Uses uint16 indices when vertex count permits, otherwise uint32
- Handles `.GLTF_MODEL` intermediate files from glTF pre-export

**ExportShader** - Compiles GLSL shaders to SPIR-V
- Multi-stage pipeline: glslc preprocessing (for `#include` support), glslangValidator compilation
- Uses SPIRV-Cross for reflection to generate Vulkan descriptor layout information
- Tracks shader include file dependencies (ShaderLayoutsBase.h, ShaderFunctions.h, ShaderLayouts.h) for dirty checking
- Stage type (.comp/.frag/.vert) detected from extension, targets Vulkan 1.2
- Version includes `VK_HEADER_VERSION` to re-export when SDK updates

**ExportTexture** - Processes images with optional compression
- Supports block compression (BC4, BC7) with automatic mipmap generation
- Handles raw format passthrough for pre-processed textures (`.BC4_UNORM_BLOCK`, `.BC7_UNORM_BLOCK`, `.R16_UNORM`)
- Filename prefix tags control compression: `[BC4]`, `[BC7]`, `[C]` for cubemap
- Cubemaps loaded from 6 face images (px/nx/py/ny/pz/nz) or `.ktx` files
- Generates C++ header constants with texture CRC arrays (game vs UI textures)

## Common Patterns

- Static `Handles()` method determines which files each processor accepts via extension or path matching
- Static `kpcName` constant defines output manifest/pack filename
- Thread-local storage ensures thread safety during parallel processing
- Path-based sorting ensures deterministic chunk ordering
- Version-based invalidation forces re-export when format changes

## See Also
- [../CLAUDE.md](../CLAUDE.md) - DataPacker orchestration and output structure
