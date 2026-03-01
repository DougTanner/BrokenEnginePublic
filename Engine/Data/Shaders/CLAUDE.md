# Engine/Data/Shaders - Vulkan GLSL Shaders

GLSL shader source files for the Vulkan 1.2 rendering pipeline. Shaders are compiled to SPIR-V by the DataPacker.

## Shader Headers

### `ShaderLayoutsBase.h`
Dual-language header providing compatible data structure definitions for both C++ and GLSL. Uses preprocessor directives (`BT_ENGINE`) to map DirectXMath types (C++) to GLSL vec types. Contains all uniform buffer object layouts, push constant structures, vertex formats, and global constants shared between CPU and GPU code.

Layout structs use individually named scalar fields rather than packed vec4 "misc" fields, with explicit pad fields maintaining 16-byte alignment where needed. Storage buffers use `scalar` layout qualifier (via `GL_EXT_scalar_block_layout`) for C-like struct packing; uniform buffers use `std140` with 16-byte alignment padding. Bindless texture indices are stored as float fields in layout structs and resolved from texture CRCs via `CrcToIndex()` on the CPU at creation time.

Provides constexpr bool equivalents of shader debug defines for C++ code, enabling `if constexpr` usage instead of preprocessor conditionals.

### `ShaderFunctions.h`
Common GLSL utility functions shared across multiple shaders. Provides coordinate space transformations, four-channel directional lighting calculations with both diffuse and specular variants, multi-term Phong-based specular highlights, normal map sampling with animation, smoke shadow/color effects, base height parallax projection, and sun lighting calculations.

## Shader Subdirectories

- **Lighting/** - Area lights, point lights, visible lights, and lighting post-processing (blur, combine)
- **Water/** - Gerstner wave vertex animation in world space with Schlick Fresnel reflections and depth-based coloring
- **Terrain/** - Base terrain mesh rendering and G-buffer generation passes (color, normal, elevation, AO)
- **Quads/** - World-space to clip-space quad transforms for visible area, shadow area, and fullscreen passes
- **Particles/** - GPU-driven particle lifecycle (compute) with wind-influenced physics and rendering with shape variants
- **Shadow/** - Compute-based shadow map generation, filtering, and object shadow passes
- **Smoke/** - Volumetric smoke simulation with wind-driven displacement and spreading
- **Wind/** - 2D wind velocity field simulation with deposit, ping-pong architecture, and magnitude-dependent behavior
- **Objects/** - Game object rendering including hex shields and player-specific shaders
- **Ui/** - Debug profiler text rendering

## Other Root Shaders

- **Clear.frag** - Outputs push constant color for clearing render targets
- **Log.vert** - Debug vertex shader that emits `debugPrintfEXT` with frame and render number

## Architecture

- **Dual-language headers**: `ShaderLayoutsBase.h` uses preprocessor to define structures compatible with both C++ (DirectXMath types) and GLSL (vec4/ivec4 types). All shaders include `ShaderLayouts.h` (generated/project-specific wrapper) and `ShaderFunctions.h`
- **Four-channel directional lighting**: RGB lighting stored as separate render targets, each with EWNS (East/West/North/South) directional weights for ambient and area lighting
- **Visible area rendering**: Shaders transform world coordinates to normalized visible area space for efficient culling and rendering
- **Multi-set descriptor layout**: Set 0 contains global descriptors shared across all pipelines (uniform buffers, samplers, bindless texture array), owned by TextureManager. Set 1 contains per-pipeline descriptors (storage buffers, combined image samplers). Model shaders additionally use Set 2 for per-material bindings
- **Bindless texture arrays**: Unsized `texture2D pTextures[]` declarations paired with a separate `sampler` object, enabling dynamic indexing without compile-time size limits. Shaders using dynamic indexing enable `GL_EXT_nonuniform_qualifier` and wrap indices with `nonuniformEXT()` for Vulkan validation compliance
- **Rendering modes via push constants**: Many vertex shaders support multiple rendering modes (camera projection, visible area projection, shadow projection) selected via push constant values, avoiding the need for separate shader permutations

## Known Issues

### NVIDIA Driver Bug: Avoid `inverse()` on Matrices in Shaders

**DO NOT use `inverse()` on mat3 or mat4 in shaders.** NVIDIA's shader compiler can hang indefinitely during `vkCreateGraphicsPipelines()` when compiling shaders that call `inverse()` on matrices. The driver's memory usage grows rapidly while it hangs, indicating a compiler bug rather than just slow compilation.

**Symptoms**: Pipeline creation hangs forever on NVIDIA GPUs; works fine on other vendors.

**Solution**: Precompute any inverse matrices on the CPU and pass them to the shader via uniform/storage buffers. For normal matrix computation (`transpose(inverse(mat3(worldMatrix)))`), compute this CPU-side and store the result. See `AnimationData.cpp` and `ModelCommon.h` for the implementation pattern using 3 vec4s to store a mat3.

## See Also

- [Lighting/CLAUDE.md](Lighting/CLAUDE.md) - Dynamic light rendering (area, point, visible) and blur/combine post-processing
- [Particles/CLAUDE.md](Particles/CLAUDE.md) - GPU-driven particle compute and render shaders with wind integration
- [Model/CLAUDE.md](Model/CLAUDE.md) - Physically-based rendering shaders for models
- [Objects/CLAUDE.md](Objects/CLAUDE.md) - Game object shaders including hex shields and player rendering
- [Shadow/CLAUDE.md](Shadow/CLAUDE.md) - Terrain shadow map generation, Gaussian blur, and object shadow blur
- [Terrain/CLAUDE.md](Terrain/CLAUDE.md) - Island terrain G-buffer generation and final compositing
- [Water/CLAUDE.md](Water/CLAUDE.md) - Gerstner wave ocean surface with Fresnel reflections
- [Smoke/CLAUDE.md](Smoke/CLAUDE.md) - Volumetric smoke simulation with wind-driven displacement
- [Wind/CLAUDE.md](Wind/CLAUDE.md) - 2D wind velocity field simulation and deposit shaders
