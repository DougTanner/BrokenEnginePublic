# `/DataPacker/Source/ExportJobs/`

Asset-specific processors that convert raw formats to optimized binary chunks.

## Architecture

**Base Class**: `ExportJob` - Abstract base for all processors
- **Interface**:
  - `CheckDirty()` - Modification time + dependency checking
  - `RunExport()` - Main processing with caching
  - `Export()` - Pure virtual, asset-specific logic
- **Features**:
  - 16-byte aligned chunks
  - CRC64 from relative paths
  - Temp file caching (CRC as filename)

**Processing Phases**:
1. **Pre-export**: glTF, Islands (create new assets)
2. **Main export**: All types process assets

## Asset Processors

### Audio - WAV Compression
- **Input**: `.wav`
- **Tool**: Windows SDK `adpcmencode3.exe`
- **Output**: ADPCM compressed audio
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
  - `Elevation.r32` → R16_UNORM, 4x downsample
  - `Normals.exr` → BC7
- **Output**: `IslandHeader` + sub-chunk CRCs
- **Flags**: `kIsland`
- **Size**: 8192×8192 pixels

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