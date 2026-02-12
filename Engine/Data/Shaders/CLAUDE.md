# Engine/Data/Shaders - Vulkan GLSL Shaders

GLSL shader source files for the Vulkan 1.2 rendering pipeline. Shaders are compiled to SPIR-V by the DataPacker.

## Shader Headers

### `ShaderLayoutsBase.h`
Dual-language header providing compatible data structure definitions for both C++ and GLSL. Uses preprocessor directives to map DirectXMath types (C++) to GLSL vec types. Contains all uniform buffer object layouts, push constant structures, vertex formats, and global constants shared between CPU and GPU code.

Layout structs use individually named scalar fields for clarity (e.g., `fSize`, `fRotation`, `fAlpha`) rather than packed vec4 "misc" fields. Fields shared across multiple shader types use `f4Params`/`pf4Params` naming. All GLSL storage buffer declarations use `scalar` layout qualifier (via `GL_EXT_scalar_block_layout`) for C-like struct packing with no alignment restrictions beyond the scalar size, eliminating padding fields that `std430` would require. Uniform buffers continue to use `std140` layout with 16-byte alignment padding. Particle textures use a fixed-size cookie array (`kiParticlesCookieCount`) indexed per-particle via `iCookie` for dynamic texture selection in shaders.

Provides constexpr bool equivalents of shader debug defines (`kbEnableDebugPrintf`, `kbEnableShaderRealtimeClock`) for C++ code, enabling `if constexpr` usage instead of preprocessor conditionals.

### `ShaderFunctions.h`
Common GLSL utility functions shared across multiple shaders. Provides coordinate space transformations, four-channel directional lighting calculations, Phong-based specular highlights, normal map sampling with animation, and smoke/shadow effects.

## Shader Subdirectories

- **Lighting/** - Area lights, point lights, visible lights, and lighting post-processing (blur, combine)
- **Water/** - Gerstner wave vertex animation with Schlick Fresnel reflections and depth-based coloring
- **Terrain/** - Base terrain mesh rendering and G-buffer generation passes (color, normal, elevation, AO)
- **Quads/** - World-space to clip-space quad transforms for visible area, shadow area, and fullscreen passes
- **Particles/** - GPU-driven particle lifecycle (compute) and rendering with shape variants (billboards, long, square)
- **Shadow/** - Compute-based shadow map generation, filtering, and object shadow passes
- **Smoke/** - Volumetric smoke simulation with wind-driven displacement and spreading
- **Wind/** - 2D wind velocity field simulation with deposit, advection, and decay passes
- **Objects/** - Game object rendering including hex shields and player-specific shaders
- **Ui/** - Debug profiler text rendering

## Architecture

- **Dual-language headers**: ShaderLayoutsBase.h uses preprocessor to define structures compatible with both C++ (DirectXMath types) and GLSL (vec4/ivec4 types)
- **Four-channel directional lighting**: RGB lighting stored as separate render targets, each with EWNS (East/West/North/South) directional weights for ambient and area lighting
- **Visible area rendering**: Shaders transform world coordinates to normalized visible area space for efficient culling and rendering
- **Non-uniform descriptor indexing**: Shaders using dynamic descriptor array indexing enable `GL_EXT_nonuniform_qualifier` extension and wrap indices with `nonuniformEXT()` for Vulkan validation compliance

## Known Issues

### NVIDIA Driver Bug: Avoid `inverse()` on Matrices in Shaders

**DO NOT use `inverse()` on mat3 or mat4 in shaders.** NVIDIA's shader compiler can hang indefinitely during `vkCreateGraphicsPipelines()` when compiling shaders that call `inverse()` on matrices. The driver's memory usage grows rapidly while it hangs, indicating a compiler bug rather than just slow compilation.

**Symptoms**: Pipeline creation hangs forever on NVIDIA GPUs; works fine on other vendors.

**Solution**: Precompute any inverse matrices on the CPU and pass them to the shader via uniform/storage buffers. For normal matrix computation (`transpose(inverse(mat3(worldMatrix)))`), compute this CPU-side and store the result. See `AnimationData.cpp` and `ModelCommon.h` for the implementation pattern using 3 vec4s to store a mat3.

## See Also

- [Model/CLAUDE.md](Model/CLAUDE.md) - Physically-based rendering shaders for models
- [Objects/CLAUDE.md](Objects/CLAUDE.md) - Game object shaders including hex shields and player rendering
- [Shadow/CLAUDE.md](Shadow/CLAUDE.md) - Terrain shadow map generation, Gaussian blur, and object shadow blur
- [Water/CLAUDE.md](Water/CLAUDE.md) - Gerstner wave ocean surface with Fresnel reflections
- [Smoke/CLAUDE.md](Smoke/CLAUDE.md) - Volumetric smoke simulation with wind-driven displacement
- [Wind/CLAUDE.md](Wind/CLAUDE.md) - 2D wind velocity field simulation and deposit shaders
