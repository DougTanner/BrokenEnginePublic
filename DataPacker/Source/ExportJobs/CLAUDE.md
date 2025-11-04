# `/DataPacker/Source/ExportJobs/`

- Asset-specific processors that convert raw formats to optimized binary chunks.
- IMPORTANT: When any changes are made to an export job, the GetVersion() return should be incremented

## Architecture

**Base Class**: `ExportJob` - Abstract base for all processors
- **Interface**:
  - `CheckDirty()` - Modification time + dependency checking
  - `RunExport()` - Main processing with caching
  - `Export()` - Pure virtual, asset-specific logic
  - `GetVersion()` - Pure virtual, returns export format version
- **Features**:
  - 16-byte aligned chunks
  - CRC64 from relative paths
  - Temp file caching with versioning
  - Magic number validation (0xDA7ACCCC)
- **Versioning**:
  - Each derived class overrides `GetVersion()` to return its format version
  - Chunk files store magic + version at start
  - Assets re-export if version changes
  - Ensures format consistency across runs

**Processing Phases**:
1. **Pre-export**: glTF, Islands (create new assets)
2. **Main export**: All types process assets

## Asset Processors

### Audio - WAV Compression
- **Input**: `.wav` (PCM format: int8, int16, or float32)
- **Tool**: Windows SDK `adpcmencode3.exe`
- **Output**: ADPCM compressed audio (~4:1 compression)
- **Process**:
  - Executes adpcmencode3.exe with input/output paths
  - Verifies output file is smaller than input
  - Parses ADPCMWAVEFORMAT from output file
  - Stores metadata in AudioHeader, compressed data in chunk
- **Memory Layout**:
  - AudioHeader stored in ChunkHeader union
  - Chunk data contains only compressed audio (no WAV headers)
- **Flags**: `kAudio`

### Font - BMFont Parser
- **Input**: `.fnt` (BMFont v3 binary)
- **Output**: `FontHeader` + character metrics
- **Flags**: `kFont`
- **Note**: No kerning support

### glTF - 3D Scene Processing
- **Input**: `.gltf`, `.glb`
- **Processing**:
  - Pre-export: Extract textures → `.GLTF_MODEL`
  - Export: Material data with texture CRCs
- **Features**: PBR metallic-roughness only
- **Flags**: `kGltf`
- **Limit**: 16 textures max

### Islands - Terrain Data
- **Input**: `Islands/*/` directories
- **Files**:
  - `AmbientOcclusion.r32` → BC4, 2x downsample
  - `Color.exr` → BC7 + mipmaps
  - `Elevation.r32` → R16_UNORM (GPU), 4x downsample + float32 array (CPU)
  - `Normals.exr` → BC7
- **Processing**:
  - GPU elevation texture: R16_UNORM format, downsampled 4x for rendering
  - CPU heightmap: Float32 array, downsampled 4x (1/4 of each source dimension) for collision/gameplay
  - Source dimensions detected from file size (assumes square textures)
  - Beach elevation calculated from most common non-zero elevation value
- **Output**: `IslandHeader` (texture CRCs, beach elevation, heightmap dimensions) + CPU heightmap data (float array)
- **Chunk Data**: Contains float32 heightmap array of size `iHeightmapWidth * iHeightmapHeight`
- **Flags**: `kIsland`
- **Size**: Dynamic (default 8192×8192 source → 2048×2048 downsampled)

### Model - 3D Geometry
- **Input**: `.obj`, `.GLTF_MODEL`
- **Filename Tags**:
  - `[N]` - Normals
  - `[FN]` - Face normals
  - `[T]` - Texcoords
  - `[NT]` - Both
- **Processing**:
  - Auto-generate missing normals
  - Center at origin
  - Vertex deduplication
- **Flags**: `kModel` + format flags

### Shader - HLSL Compilation
- **Input**: `.vert`, `.frag`, `.comp`
- **Pipeline**:
  1. `glslc.exe` - Preprocess includes
  2. `glslangValidator.exe` - HLSL → SPIR-V
  3. SPIRV-Cross - Extract reflection data
  4. Generate descriptor layouts
- **Output**: `ShaderHeader` + SPIR-V + Vulkan structs
- **Flags**: `kShader` + stage flag
- **Dependencies**: Watches all include files

### Texture - Image Processing
- **Input**: `.png`, `.tga`, `.jpg`, `.ktx`, raw formats
- **Compression**:
  - `[BC4]` prefix → BC4 (single channel)
  - `[BC7]` prefix → BC7 (RGBA)
  - Auto-mipmaps for compressed
- **Raw Formats**:
  - `.BC4_UNORM_BLOCK`, `.BC7_UNORM_BLOCK`
  - `.R8_UNORM`, `.R8G8B8A8_UNORM`
  - `.R16_UNORM`, `.R16G16_UNORM`
  - `.R32_SFLOAT`
- **Flags**: `kTexture` + `kCubemap` (if KTX)

## Common Patterns

- **Naming**: Static `kpcName` for output files
- **Detection**: Static `Handles()` for file types
- **Threading**: Thread-local storage for safety
- **Ordering**: Sort by path for deterministic chunks
- **Caching**: Skip unchanged via temp files

## Warnings

- Pre-export must complete before main export
- Shader compilation requires valid SDK paths
- Island processing expects exact file structure
- Model tags must be in filename for format detection