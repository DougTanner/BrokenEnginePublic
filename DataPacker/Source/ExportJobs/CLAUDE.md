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

**Shader Header Dependency Tracking** - For shader assets, `CheckDirty()` uses two-level optimization:
1. `ShaderHeadersChanged()` - Quick early-out: compares the most recent modification time across all files in the Shaders directories against a cached timestamp in temp. If no shader file has changed, skips per-shader include checking entirely. Result is computed once per run (static memoization)
2. `CollectShaderIncludes()` - Recursive `#include` parser that discovers all transitive header dependencies for a given shader file, resolving includes relative to the file, then against both engine and project Shaders directories. Only the specific headers included by each shader are checked, rather than all shader headers globally

**Processing Flow**
1. `CheckDirty()` - Validates pack file exists, compares timestamps, validates cached chunk magic/version
2. `RunExport()` - Loads cached chunk if clean, otherwise calls `Export()` and caches result; calls `CleanupOnFailure()` if export throws
3. `Export()` - Pure virtual method where derived classes implement asset-specific conversion
4. `CleanupOnFailure()` - Virtual method (empty by default) that derived classes override to clean up intermediate files on export failure
5. `AllocateHeaderAndData()` - Helper that allocates aligned buffer and returns header pointer plus data span

**Two-Phase System**
- **Pre-export**: Scene and Islands generate intermediate assets (textures, model files)
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

**ExportScene** - Processes glTF 3D scenes via tinygltf
- Pre-export extracts embedded textures to intermediate `.BC4`/`.BC7_UNORM_BLOCK` files using parallel compression via `std::async`
- Pre-export generates `.MODEL` intermediate file with global vertex buffer and per-material index buffers (following Vulkan-glTF-PBR reference implementation approach)
- **Vertex deduplication**: `LoadVertices()` deduplicates vertices per-primitive using `std::unordered_map<ModelVertex, uint32_t>` with hash-based O(1) lookups (hash specialization in `DataFile.h`), remapping indices to reference shared vertices
- Stores all 5 UV channels (TEXCOORD_0 through TEXCOORD_4) per vertex for per-material texture coordinate selection
- **Scene chunk data layout**: `[textureCrcs ALIGN16] [indexStarts ALIGN16] [MaterialShaderData[]] [AnimationData...]`. Texture CRCs and per-material index starts are stored as separate 16-byte-aligned arrays before the material shader data
- Main export stores PBR material data with texture set indices
- Computes and stores `modelCrc` in SceneHeader linking to the .MODEL chunk for runtime model buffer lookup
- Only supports metallic-roughness workflow; asserts if KHR_materials_pbrSpecularGlossiness extension is present. For BLEND alpha mode, inspects actual alpha content (base color factor alpha < 1.0 or non-opaque pixels in base color texture) and sets `fAlphaMask = 2.0` only if transparency is detected; overrides to opaque if no alpha content found. Non-BLEND materials get `fAlphaMask = 0.0`. Exports `fAlphaMaskCutoff` from glTF `alphaCutoff`. Applies node hierarchy transforms
- **Material splitting**: When multiple primitives from different mesh nodes share a material, separate material entries are created to ensure correct per-primitive mesh world transforms at runtime. The `.MODEL` file may contain more materials than the original glTF model. `MaterialInfo.iOriginalMaterialIndex` tracks the original glTF material index for split materials
- **Skinned vertex handling**: Skinned vertices remain in local/model space at export time. The runtime skinning pipeline applies mesh world transform along with joint matrices. Only static models without skeleton data have their vertices pre-transformed
- **Animation path selection**: Uses skeletal animation only if ALL animation channels target skin joints; otherwise uses node-based animation
- **SkeletonData**: Defined in `ExportScene.h`, separates skeleton counts (`common::Skeleton`) from variable-length data (nodes, skinJointToNode, inverseBindMatrices) using dynamic vectors. Used by `BuildNodeSkeleton()` and `LoadSkeleton()` for DataPacker-side building; serialized as variable-length arrays in the scene chunk
- **Skeletal animation**: Extracts all nodes from the glTF scene hierarchy into `SkeletonData`. Inverse bind matrices loaded directly from glTF column-major into DirectXMath row-major (no transpose needed since column-major data loaded as row-major places translation in row 3 where DirectXMath expects it). Animation channels reference nodes by `uiNodeIndex`. TRS properties and matrix stored separately per node. Runtime combines as `localMatrix = matrix * S * R * T` (row-major DirectXMath, equivalent to Vulkan-glTF-PBR's `T * R * S * matrix` in column-major GLM)
- **Node-based animation**: For models with animations targeting non-skin nodes, builds skeleton from the node hierarchy via `BuildNodeSkeleton()` returning `SkeletonData`. Stores ALL nodes from the glTF scene. If a skin exists (for models with mixed animation targets), `LoadSkeleton()` loads skin joint data to enable proper skinning for skinned meshes. Same matrix loading as skeletal animation (no transpose - glTF column-major loaded directly as DirectXMath row-major)
- **Per-material mesh world matrix**: `MaterialInfo.iParentNodeIndex` and `f4x4RelativeTransform` enable runtime mesh world matrix computation. For skinned materials, captures mesh node index directly (relative transform is identity). For non-skinned materials attached to animated nodes, finds nearest animated ancestor and stores relative transform from mesh bind pose to ancestor bind pose
- **Failure cleanup**: Tracks intermediate files (textures, `.MODEL`, `.PreExport` marker) and deletes them via `CleanupOnFailure()` if export throws to prevent partial/corrupt intermediate files from persisting

**.MODEL binary format** (written by ExportScene pre-export, read by ExportModel and ExportScene main export):
Uses a global vertex buffer with per-material index buffers, matching the Vulkan-glTF-PBR reference implementation:
1. `size_t uiMaterialCount` - number of materials
2. `uint32_t[uiMaterialCount]` - materialIndexPositions (index offset per material into the global index buffer)
3. `MaterialInfo[uiMaterialCount]` - per-material skinning metadata (parent node index, relative transform)
4. `size_t uiIndexCount` - total index count across all materials
5. `size_t uiVertexCount` - total vertex count in the global vertex buffer
6. `uint16_t[]` or `uint32_t[]` - indices (16-bit if vertex count < 65535, else 32-bit)
7. `ModelVertex[]` - global vertex buffer containing all vertices

**Animation data format** (appended after MaterialShaderData in the scene chunk when animations are present):
Uses variable-length serialization with `AnimationHeader` containing counts, followed by data arrays:
1. `AnimationHeader` - counts (animations, channels, keyframes, cubic keyframes, materials) + `Skeleton` (node/joint counts only)
2. `ModelNode[uiNodeCount]` - all scene nodes with parent indices and bind pose TRS
3. `uint16_t[uiSkinJointCount]` (4-byte aligned) - skin joint to node index mapping
4. `XMFLOAT4X4[uiSkinJointCount]` - inverse bind matrices
5. `AnimationClip[uiAnimationCount]` - animation clips with channel ranges and duration
6. `MaterialInfo[uiMaterialCount]` - per-material parent node index and relative transform
7. `AnimationChannel[uiChannelCount]` - channels with node index, target path, interpolation, keyframe range
8. `AnimationKeyframe[uiKeyframeCount]` - STEP/LINEAR keyframes
9. `AnimationKeyframeCubic[uiCubicKeyframeCount]` - CUBICSPLINE keyframes with in/out tangents

**ExportIsland** - Processes terrain data from directory structure
- Converts source textures: elevation (`.r32`), color (`.exr`), normals (`.exr`), ambient occlusion (`.r32`)
- Generates GPU texture (R16_UNORM) and CPU heightmap (float array) from elevation data
- Calculates beach elevation as the most common non-zero elevation value
- Stores texture CRCs and heightmap dimensions in header

**ExportModel** - Converts `.MODEL` intermediate geometry files produced by ExportScene pre-export
- Reads per-material index positions, index buffer, and vertex buffer from the `.MODEL` binary format
- Skips MaterialInfo data (not needed for model chunk export)
- Uses uint16 indices when vertex count permits, otherwise uint32
- Writes model chunk with index count, vertex count, stride, and packed index+vertex data

**ExportShader** - Compiles GLSL shaders to SPIR-V
- Multi-stage pipeline: glslc preprocessing (for `#include` support), glslangValidator compilation
- Uses SPIRV-Cross for reflection to generate Vulkan descriptor layout information
- Reflection exports per-binding descriptor set indices (`spv::DecorationDescriptorSet`) alongside bindings, enabling multi-set descriptor layout splitting at runtime. Chunk data layout: `[bindings ALIGN16] [setIndices ALIGN16] [attrs ALIGN16] [SPIR-V]`
- Shader dirty checking leverages `ShaderHeadersChanged()` guard and `CollectShaderIncludes()` recursive include parser in ExportJob base class to automatically discover and check all transitive header dependencies per shader
- Stage type (.comp/.frag/.vert) detected from extension, targets Vulkan 1.2
- Version includes `VK_HEADER_VERSION` to re-export when SDK updates
- **Failure cleanup**: Deletes intermediate preprocessing files via `CleanupOnFailure()` if compilation fails

**ExportTexture** - Processes images with optional compression
- Supports block compression (BC4, BC7) with automatic mipmap generation
- Handles raw format passthrough for pre-processed textures (`.BC4_UNORM_BLOCK`, `.BC7_UNORM_BLOCK`, `.R16_UNORM`)
- Filename prefix tags control compression: `[BC4]`, `[BC7]`, `[C]` for cubemap
- Cubemaps loaded from 6 face images (px/nx/py/ny/pz/nz) or `.ktx` files
- `AddToHeader()` is a no-op; texture array indices are assigned lazily at runtime by TextureManager

## Common Patterns

- Static `Handles()` method determines which files each processor accepts via extension or path matching
- Static `kName` constant defines output manifest/pack filename
- Thread-local storage ensures thread safety during parallel processing
- Path-based sorting ensures deterministic chunk ordering
- Version-based invalidation forces re-export when format changes

## Version Bumping

**IMPORTANT**: When modifying an export processor's output format or validation logic, increment the version number in `GetVersion()` to force re-export of all affected assets.

Each exporter has a version in its header file (e.g., `ExportScene.h`):
```cpp
virtual int64_t GetVersion() const override { return N + sizeof(common::ChunkHeader); }
```

Bump the version (increment `N`) when:
- Adding/removing/reordering fields in exported data
- Changing validation logic that affects which assets pass/fail
- Modifying intermediate file formats (e.g., `.MODEL`)
- Changing compression or encoding of exported data

## See Also
- [../CLAUDE.md](../CLAUDE.md) - DataPacker orchestration and output structure
