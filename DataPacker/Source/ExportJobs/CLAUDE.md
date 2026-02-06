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
2. `RunExport()` - Loads cached chunk if clean, otherwise calls `Export()` and caches result; calls `CleanupOnFailure()` if export throws
3. `Export()` - Pure virtual method where derived classes implement asset-specific conversion
4. `CleanupOnFailure()` - Virtual method (empty by default) that derived classes override to clean up intermediate files on export failure
5. `AllocateHeaderAndData()` - Helper that allocates aligned buffer and returns header pointer plus data span

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
- Pre-export extracts embedded textures to intermediate `.BC4`/`.BC7_UNORM_BLOCK` files using parallel compression via `std::async`
- Pre-export generates `.GLTF_MODEL` intermediate file with global vertex buffer and per-material index buffers (following Vulkan-glTF-PBR reference implementation approach)
- Stores all 5 UV channels (TEXCOORD_0 through TEXCOORD_4) per vertex for per-material texture coordinate selection
- Main export stores PBR material data with texture CRCs and texture set indices
- Computes and stores `modelCrc` in GltfHeader linking to the .GLTF_MODEL chunk for runtime model buffer lookup
- Only supports metallic-roughness workflow; asserts if KHR_materials_pbrSpecularGlossiness extension is present. Warns for non-OPAQUE alpha modes, non-default alphaCutoff values, and alphaMode in material additional values (alpha masking not yet fully supported). Applies node hierarchy transforms
- **Material splitting**: When multiple primitives from different mesh nodes share a material, separate material entries are created to ensure correct per-primitive mesh world transforms at runtime. The `.GLTF_MODEL` file may contain more materials than the original glTF model. `GltfMaterialInfo.iOriginalMaterialIndex` tracks the original glTF material index for split materials
- **Skinned vertex handling**: Skinned vertices remain in local/model space at export time. The runtime skinning pipeline applies mesh world transform along with joint matrices. Only static models without skeleton data have their vertices pre-transformed
- **Animation path selection**: Uses skeletal animation only if ALL animation channels target skin joints; otherwise uses node-based animation
- **Skeletal animation**: Extracts all nodes from the glTF scene hierarchy into `GltfSkeleton.nodes[]`, with `skinJointToNode[]` mapping skin joints to their node indices. Inverse bind matrices stored per skin joint (loaded directly from glTF column-major into DirectXMath row-major - no transpose needed since column-major data loaded as row-major places translation in row 3 where DirectXMath expects it). Animation channels reference nodes by `uiNodeIndex`. TRS properties and matrix stored separately per node. Runtime combines as `localMatrix = matrix * S * R * T` (row-major DirectXMath, equivalent to Vulkan-glTF-PBR's `T * R * S * matrix` in column-major GLM)
- **Node-based animation**: For models with animations targeting non-skin nodes, builds skeleton from the node hierarchy. Stores ALL nodes from the glTF scene in `GltfSkeleton.nodes[]`. If a skin exists (for models with mixed animation targets), loads skin joint data (`uiSkinJointCount`, `skinJointToNode[]`, `inverseBindMatrices[]`) to enable proper skinning for skinned meshes. Same matrix loading as skeletal animation (no transpose - glTF column-major loaded directly as DirectXMath row-major)
- **Per-material mesh world matrix**: `GltfMaterialInfo.iParentNodeIndex` and `f4x4RelativeTransform` enable runtime mesh world matrix computation. For skinned materials, captures mesh node index directly (relative transform is identity). For non-skinned materials attached to animated nodes, finds nearest animated ancestor and stores relative transform from mesh bind pose to ancestor bind pose
- **Debug logging**: Writes `gltf_comparison_data_packer.log` when processing the "free_cyberpunk_hovercar" model for comparing export-time data against Vulkan-glTF-PBR reference implementation
- **Failure cleanup**: Tracks intermediate files (textures, `.GLTF_MODEL`, `.PreExport` marker) and deletes them via `CleanupOnFailure()` if export throws to prevent partial/corrupt intermediate files from persisting

**.GLTF_MODEL binary format** (written by ExportGltf pre-export, read by ExportModel and ExportGltf main export):
Uses a global vertex buffer with per-material index buffers, matching the Vulkan-glTF-PBR reference implementation:
1. `size_t uiMaterialCount` - number of materials
2. `uint32_t[uiMaterialCount]` - materialIndexPositions (index offset per material into the global index buffer)
3. `GltfMaterialInfo[uiMaterialCount]` - per-material skinning metadata (parent node index, relative transform)
4. `size_t uiIndexCount` - total index count across all materials
5. `size_t uiVertexCount` - total vertex count in the global vertex buffer
6. `uint16_t[]` or `uint32_t[]` - indices (16-bit if vertex count < 65535, else 32-bit)
7. `GltfVertex[]` - global vertex buffer containing all vertices

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
- Tracks shader include file dependencies (ShaderLayoutsBase.h, ShaderFunctions.h, GltfCommon.h, ShaderLayouts.h, TextureCounts.h) for dirty checking
- Stage type (.comp/.frag/.vert) detected from extension, targets Vulkan 1.2
- Version includes `VK_HEADER_VERSION` to re-export when SDK updates
- **Failure cleanup**: Deletes intermediate preprocessing files via `CleanupOnFailure()` if compilation fails

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

## Version Bumping

**IMPORTANT**: When modifying an export processor's output format or validation logic, increment the version number in `GetVersion()` to force re-export of all affected assets.

Each exporter has a version in its header file (e.g., `ExportGltf.h`):
```cpp
virtual int64_t GetVersion() const override { return N + sizeof(common::ChunkHeader); }
```

Bump the version (increment `N`) when:
- Adding/removing/reordering fields in exported data
- Changing validation logic that affects which assets pass/fail
- Modifying intermediate file formats (e.g., `.GLTF_MODEL`)
- Changing compression or encoding of exported data

## See Also
- [../CLAUDE.md](../CLAUDE.md) - DataPacker orchestration and output structure
