# Engine/Data/Shaders - Vulkan GLSL Shaders

GLSL shader source files for the Vulkan 1.2 rendering pipeline. Shaders are compiled to SPIR-V by the DataPacker.

## Shader Headers

### `ShaderLayoutsBase.h`
Dual-language header providing compatible data structure definitions for both C++ and GLSL. Uses preprocessor directives to map DirectXMath types (C++) to GLSL vec types. Contains all uniform buffer object layouts, push constant structures, vertex formats, and global constants shared between CPU and GPU code.

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
- **Smoke/** - Volumetric smoke simulation and spreading
- **Objects/** - Game object rendering including hex shields and player-specific shaders
- **Ui/** - Debug profiler text rendering

## Architecture

- **Dual-language headers**: ShaderLayoutsBase.h uses preprocessor to define structures compatible with both C++ (DirectXMath types) and GLSL (vec4/ivec4 types)
- **Four-channel directional lighting**: RGB lighting stored as separate render targets, each with EWNS (East/West/North/South) directional weights for ambient and area lighting
- **Visible area rendering**: Shaders transform world coordinates to normalized visible area space for efficient culling and rendering
- **Non-uniform descriptor indexing**: Shaders using dynamic descriptor array indexing enable `GL_EXT_nonuniform_qualifier` extension and wrap indices with `nonuniformEXT()` for Vulkan validation compliance

## See Also

- [Gltf/CLAUDE.md](Gltf/CLAUDE.md) - Physically-based rendering shaders for glTF models
