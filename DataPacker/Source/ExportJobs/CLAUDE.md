# `DataPacker/Source/ExportJobs/`

Asset-specific processors that convert raw file formats into optimized binary chunks for runtime loading.

## Architecture

**ExportJob** - Abstract base class defining the export pipeline. Handles dirty checking via timestamps, version tracking, and cached chunk validation. Each derived class provides a `GetVersion()` number; assets re-export when the version changes. Shader dirty checking uses two-level optimization: a quick global timestamp check across all shader files, then recursive `#include` dependency parsing per shader.

**Processing flow**: `CheckDirty()` determines staleness, `RunExport()` loads from cache or calls the derived `Export()`, and `CleanupOnFailure()` removes intermediate files on error.

**Two-phase system**: Scene and Islands run a pre-export phase generating intermediate assets (textures, `.MODEL` files), then all asset types process in parallel during the main export phase.

## Asset Processors

- **ExportAudio** - Converts WAV files to normalized 16-bit PCM via DirectXTK WAVFileReader
- **ExportFont** - Parses BMFont binary format, extracting character metrics and layout data
- **ExportScene** - Processes glTF 3D scenes via tinygltf. Pre-export extracts textures to compressed intermediates and generates `.MODEL` geometry files. Main export produces PBR material data, skeletal/node animation data, and per-material mesh transforms. Split across multiple files by responsibility:
  - `ExportScene.h/.cpp` - Class definition, glTF loading, Vulkan helpers, two-phase Export orchestration (PreExport/MainExport)
  - `ExportSceneVertices.h/.cpp` - Vertex loading, material splitting for per-primitive transforms, occlusion detection, and supporting structs (PairHash, Material, Parent, MaterialNodeInfo, AncestorJointResult)
  - `ExportSceneSkeleton.h/.cpp` - Skeleton construction from glTF skins or node hierarchies, node parent map building (SkeletonData struct)
  - `ExportSceneAnimation.h/.cpp` - Animation path selection (skeletal vs node-based) and keyframe loading
- **ExportIsland** - Processes terrain from elevation/color/normal/AO source textures, generating GPU textures and CPU heightmaps with computed beach elevation
- **ExportModel** - Reads `.MODEL` intermediate files from ExportScene pre-export, producing geometry chunks with adaptive 16/32-bit index buffers
- **ExportShader** - Compiles GLSL to SPIR-V via glslc preprocessing and glslangValidator, then uses SPIRV-Cross reflection to generate Vulkan descriptor layout info. Targets Vulkan 1.2, re-exports when SDK version changes
- **ExportTexture** - Processes images with BC4/BC7 block compression and mipmap generation. Handles raw format passthrough for pre-processed textures. Filename prefix tags (`[BC4]`, `[BC7]`, `[C]`) control compression mode. Cubemaps from face images or `.ktx` files
- **ExportRaw** - Copies files verbatim from directories named "Raw" into chunks without transformation
- **GenerateIrradianceCubemaps()** - Offline diffuse IBL convolution using CMFT spherical harmonics, producing RGBA16F intermediate cubemaps
- **GeneratePreFilteredCubemaps()** - Offline specular IBL prefiltering using CMFT radiance filter with Blinn BRDF, producing mipmapped RGBA16F cubemaps (one roughness per mip level). Uses GPU-accelerated OpenCL when available

## Common Patterns

- Static `Handles()` method determines which files each processor accepts via extension or path matching
- Static `kName` constant defines output manifest/pack filename
- Thread-local storage ensures thread safety during parallel processing
- Path-based sorting ensures deterministic chunk ordering

## Version Bumping

**IMPORTANT**: When modifying an export processor's output format, increment the version number in `GetVersion()` to force re-export of all affected assets. Bump when adding/removing/reordering fields, changing intermediate formats, or modifying compression/encoding.

## See Also
- [../CLAUDE.md](../CLAUDE.md) - DataPacker orchestration and output structure
