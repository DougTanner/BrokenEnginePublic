# `DataPacker/Source/ExportJobs/`

Asset-specific processors that convert raw file formats into optimized binary chunks for runtime loading.

## Architecture

**ExportJob** - Abstract base class defining the export pipeline
- Converts raw assets (audio, models, textures, etc.) into binary chunks with headers
- Handles dirty checking via modification timestamps and version tracking
- Provides caching system using temp files to skip unchanged assets
- Generates CRC64 identifiers from relative file paths
- Ensures 16-byte alignment for all chunks
- Each derived class provides a version number; assets re-export when version changes

**Processing Flow**
1. `CheckDirty()` - Compares timestamps, validates cached chunks, checks version numbers
2. `RunExport()` - Loads cached chunk if clean, otherwise calls `Export()` and caches result
3. `Export()` - Pure virtual method where derived classes implement asset-specific conversion
4. Cached chunks stored in temp directory with magic number and version header

**Two-Phase System**
- **Pre-export**: glTF and Islands can generate new intermediate assets
- **Main export**: All asset types process files in parallel

## Asset Processors

**ExportAudio** - Converts WAV files to uncompressed PCM format
- Uses DirectXTK for WAV parsing
- Normalizes float samples to 16-bit PCM
- Stores audio metadata and raw PCM data

**ExportFont** - Parses BMFont binary format
- Extracts character metrics and texture references
- No kerning support

**ExportGltf** - Processes glTF/glb 3D scenes
- Pre-export phase extracts textures to intermediate files
- Main export stores PBR material data with texture CRCs
- Supports only metallic-roughness workflow

**ExportIsland** - Processes terrain data from directory structure
- Combines multiple source textures (elevation, color, normals, ambient occlusion)
- Generates dual elevation data: GPU texture and CPU heightmap for collision
- Downsamples textures at different rates optimized for rendering vs gameplay
- Calculates beach elevation from source data

**ExportModel** - Converts OBJ and intermediate glTF geometry
- Auto-generates missing normals
- Centers geometry at origin
- Deduplicates vertices
- Filename tags control vertex format

**ExportShader** - Compiles HLSL shaders to SPIR-V
- Multi-stage pipeline: glslc preprocessing, glslangValidator compilation, SPIRV-Cross reflection
- Tracks shader include file dependencies for dirty checking
- Generates Vulkan descriptor layout information
- Stage type detected from file extension

**ExportTexture** - Processes images with optional compression
- Supports block compression (BC4, BC7) with automatic mipmap generation
- Handles raw format passthrough for pre-processed textures
- Filename prefix tags control compression type

## Common Patterns

- Static `Handles()` method determines which files each processor accepts
- Static `kpcName` constant defines output manifest/pack filename
- Thread-local storage ensures thread safety during parallel processing
- Path-based sorting ensures deterministic chunk ordering
- Version-based invalidation forces re-export when format changes

## Important Notes

- **Version tracking**: Increment `GetVersion()` whenever export format changes
- **Pre-export dependency**: Main export phase depends on pre-export completion
- **SDK requirements**: Shader compilation requires Vulkan SDK environment variable
- **Format detection**: Some processors use filename tags to control output format