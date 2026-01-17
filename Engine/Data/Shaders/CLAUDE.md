# Engine/Data/Shaders - Vulkan GLSL Shaders

GLSL shader source files for the Vulkan 1.2 rendering pipeline. Shaders are compiled to SPIR-V by the DataPacker.

## Shader Headers

### `ShaderLayoutsBase.h`
Dual-language header providing compatible data structure definitions for both C++ and GLSL. Uses preprocessor directives to map DirectXMath types (C++) to GLSL vec types. Contains all uniform buffer object layouts, push constant structures, vertex formats, and global constants shared between CPU and GPU code.

### `ShaderFunctions.h`
Common GLSL utility functions shared across multiple shaders. Provides coordinate space transformations, four-channel directional lighting calculations, Phong-based specular highlights, normal map sampling with animation, and smoke/shadow effects.

## Shader Categories

### Lighting System
- **AreaLight.frag** / **PointLight.frag** - Render light sources using four-channel directional output (East/West/North/South) for deferred lighting accumulation
- **LightingBlur.frag** / **LightingCombine.frag** - Post-process lighting buffers with blur and final compositing

### Water Rendering
- **Water.vert** - Generates animated water surface using Gerstner wave simulation with configurable low and medium frequency wave sets
- **Water.frag** - Composites water appearance with depth-based coloring, animated normal maps, skybox reflections using Schlick's Fresnel approximation, and specular highlights from area lights

### Terrain System
- **Terrain.vert** / **Terrain.frag** - Base terrain mesh rendering
- **TerrainColor.frag** / **TerrainNormal.frag** / **TerrainElevation.frag** / **TerrainAmbientOcclusion.frag** - Terrain G-buffer generation passes

### Quad Rendering
- **QuadsVisibleArea.vert** / **QuadsAxisAligned.vert** / **QuadsAxisAlignedVisibleArea.vert** - Transform world-space quads to clip-space with support for multiple render targets (visible area, shadow area, smoke area)
- **QuadsFullscreen.vert** - Fullscreen triangle for post-processing

### Particle System
- **ParticlesSpawn.comp** / **ParticlesUpdate.comp** - Compute shaders for GPU-driven particle lifecycle management
- **ParticlesRender.frag** / **LongParticlesRender.vert** / **SquareParticlesRender.vert** - Particle rendering with shape variants

### Shadow System
- **Shadow.comp** / **ShadowBlur.comp** - Compute-based shadow map generation and filtering
- **ObjectShadows.frag** / **ObjectShadowsBlur.frag** - Object shadow rendering passes

### Smoke System
- **Smoke.frag** / **SmokeSpreadOne.frag** / **SmokeSpreadTwo.frag** - Volumetric smoke simulation and spreading

### UI/Debug
- **Widgets.vert** / **Widgets.frag** - UI widget rendering
- **ProfileText.frag** / **Log.vert** - Debug text and profiler output

## Architecture

- **Dual-language headers**: ShaderLayoutsBase.h uses preprocessor to define structures compatible with both C++ (DirectXMath types) and GLSL (vec4/ivec4 types)
- **Four-channel directional lighting**: RGB lighting stored as separate render targets, each with EWNS (East/West/North/South) directional weights for ambient and area lighting
- **Visible area rendering**: Shaders transform world coordinates to normalized visible area space for efficient culling and rendering
- **Non-uniform descriptor indexing**: Shaders using dynamic descriptor array indexing enable `GL_EXT_nonuniform_qualifier` extension and wrap indices with `nonuniformEXT()` for Vulkan validation compliance

## See Also

- [Vulkan-glTF-PBR/CLAUDE.md](Vulkan-glTF-PBR/CLAUDE.md) - Physically-based rendering shaders for glTF models
