# `DataPacker/Source/ExportJobs/`

Asset-specific processors that convert raw file formats into optimized binary chunks for runtime loading.

## Architecture

**ExportJob** - Abstract base class defining the export pipeline. Handles dirty checking via timestamps, version tracking, and cached chunk validation. Each derived class provides a `GetVersion()` number; assets re-export when the version changes. Derived classes may override `CheckDirty()` to add extra conditions.

**Two-phase system**: Scene and Islands run a pre-export phase generating intermediate assets (textures, `.MODEL` geometry files), then all asset types process in parallel during the main export phase.

## Asset Processors

- **ExportAudio** - Converts WAV files to normalized 16-bit PCM
- **ExportFont** - Parses BMFont binary format, extracting character metrics and layout data
- **ExportScene** - Processes glTF 3D scenes via tinygltf. Pre-export extracts textures and generates `.MODEL` geometry. Main export produces PBR materials, animations, and per-material mesh transforms. Split across four files: `ExportScene.cpp` (core orchestration), `ExportSceneVertices.cpp`, `ExportSceneSkeleton.cpp`, `ExportSceneAnimation.cpp`
- **ExportIsland** - Processes terrain from elevation/color/normal/AO source textures, generating GPU textures and CPU heightmaps
- **ExportModel** - Reads `.MODEL` intermediate files from ExportScene pre-export, producing geometry chunks with adaptive 16/32-bit index buffers
- **ExportShader** - Compiles GLSL to SPIR-V via glslc/glslangValidator, then uses SPIRV-Cross reflection to generate Vulkan descriptor layout info. Targets Vulkan 1.2. Overrides `CheckDirty()` to also re-export when any shader header in the Shaders directories has changed, by recursively parsing `#include` dependencies
- **ExportTexture** - BC4/BC7 block compression with mipmap generation. Filename prefix tags control compression mode. Supports cubemaps from face images or `.ktx` files
- **ExportRaw** - Copies files verbatim from "Raw" directories into chunks without transformation
- **GenerateIrradianceCubemaps()** / **GeneratePreFilteredCubemaps()** - Offline IBL convolution using CMFT for diffuse and specular cubemaps

## Common Patterns

- Static `Handles()` method determines which files each processor accepts via extension or path matching
- Static `kName` constant defines output manifest/pack filename
- Path-based sorting ensures deterministic chunk ordering

## Version Bumping

**IMPORTANT**: When modifying an export processor's output format, increment `GetVersion()` to force re-export of all affected assets.

## See Also
- [../CLAUDE.md](../CLAUDE.md) - DataPacker orchestration and output structure
