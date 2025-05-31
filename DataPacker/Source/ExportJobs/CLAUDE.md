# /DataPacker/Source/ExportJobs/

The `/DataPacker/Source/ExportJobs/` directory contains the export job classes that process raw assets into optimized binary formats for the game engine. Each export job handles a specific asset type and inherits from the base ExportJob class.

Export jobs participate in DataPacker's two-phase process:
- **Pre-export phase**: ExportGltf and ExportIsland can create new assets that need processing
- **Main export phase**: All export jobs process their respective asset types

## Base Class

### ExportJob.h & ExportJob.cpp
- **Base abstract class** for all export jobs
- **Key functionality**:
  - Dirty checking system comparing file modification times
  - Chunk allocation with 16-byte alignment
  - CRC64 generation from relative file paths
  - Temp file caching to avoid reprocessing unchanged assets
- **Public interface**:
  - `CheckDirty()` - Determines if re-export needed (also checks shader includes)
  - `RunExport()` - Main export execution returning chunk data
  - `AllocateHeaderAndData()` - Memory allocation helper
- **Pure virtual**: `Export()` - Must be implemented by derived classes
- **Note**: Cached chunks stored in temp directory using CRC64 as filename

## Export Job Types

### ExportAudio.h & ExportAudio.cpp
- **Handles**: `.wav` files
- **Processing**: Converts WAV to ADPCM compressed format using Windows SDK's `adpcmencode3.exe`
- **Output**: Compressed audio data
- **Chunk flags**: `kAudio`
- **Name**: "Audio"

### ExportFont.h & ExportFont.cpp
- **Handles**: `.fnt` files (BMFont binary format version 3)
- **Processing**: Parses BMFont blocks extracting character metrics and common data
- **Output**: `FontHeader` + character IDs + `Character` metrics arrays
- **Chunk flags**: `kFont`
- **Name**: "Font"
- **Note**: Kerning pairs not implemented (count = 0)

### ExportGltf.h & ExportGltf.cpp
- **Handles**: `.gltf` and `.glb` files
- **Processing**:
  - Pre-export: Extracts textures and saves model data to `.GLTF_MODEL`
  - Export: Creates material data with texture CRC references
  - Supports PBR metallic-roughness workflow only
  - Node hierarchy traversal with transformation matrices
- **Output**: `GltfHeader` + `GltfShaderData` array for materials
- **Chunk flags**: `kGltf`
- **Name**: "Gltf"
- **Limits**: Maximum 16 textures per file

### ExportIsland.h & ExportIsland.cpp
- **Handles**: Directories under "Islands/" containing terrain data files
- **Processing**:
  - AmbientOcclusion.r32 → BC4 compressed, 2x downsampled
  - Color.exr → BC7 compressed with mipmaps
  - Elevation.r32 → R16_UNORM, 4x downsampled
  - Normals.exr → BC7 compressed
  - Auto-calculates beach elevation from height data
- **Output**: `IslandHeader` with CRC references to sub-chunks
- **Chunk flags**: `kIsland`
- **Name**: "Islands"
- **Constants**: Island size 8192x8192 pixels

### ExportModel.h & ExportModel.cpp
- **Handles**: `.obj` and `.GLTF_MODEL` files
- **Processing**:
  - OBJ: Uses tinyobjloader with vertex format detection from filename tags
    - `[N]` = Normals, `[FN]` = Face normals, `[T]` = Texcoords, `[NT]` = Both
  - Auto-generates normals if missing
  - Centers geometry at origin
  - Vertex deduplication for optimization
- **Output**: `ModelHeader` + indices + vertices (various formats)
- **Chunk flags**: `kModel` (with optional `kNormals`/`kTexcoords`/`kFaceNormals`)
- **Name**: "Model"

### ExportShader.h & ExportShader.cpp
- **Handles**: `.vert`, `.frag`, `.comp` HLSL shader files
- **Processing**:
  1. Preprocesses with `glslc.exe` to resolve includes
  2. Compiles to SPIR-V with `glslangValidator.exe` (Vulkan 1.1 target)
  3. Extracts reflection data via SPIRV-Cross
  4. Generates Vulkan descriptor set layouts
- **Output**: `ShaderHeader` + SPIR-V bytecode + Vulkan structures
- **Chunk flags**: `kShader` + (`kVertex`/`kFragment`/`kCompute`)
- **Name**: "Shader"
- **Dependencies**: Also watches ShaderLayoutsBase.h, ShaderFunctions.h, ShaderLayouts.h

### ExportTexture.h & ExportTexture.cpp
- **Handles**: Image files (`.png`, `.tga`, `.jpg`, `.ktx`) and raw formats
- **Processing**:
  - `[BC4]` prefix → BC4 compression (single channel)
  - `[BC7]` prefix → BC7 compression (RGBA)
  - KTX files → Direct cubemap loading
  - Raw formats → Direct copy without compression
  - Auto-generates mipmaps for compressed formats
- **Output**: `TextureHeader` + mipmap data
- **Chunk flags**: `kTexture` (with optional `kCubemap`)
- **Name**: "Texture"
- **Raw formats**: `.BC4_UNORM_BLOCK`, `.BC7_UNORM_BLOCK`, `.R8_UNORM`, `.R8G8B8A8_UNORM`, `.R16_UNORM`, `.R16G16_UNORM`, `.R32_SFLOAT`

## Common Patterns

- All export jobs use static `kpcName` for output filename generation
- All implement static `Handles()` method for file type detection
- Thread-safe operation using thread-local storage
- Sorted by relative path for consistent chunk ordering
- Cached chunks stored in temp directory to avoid reprocessing