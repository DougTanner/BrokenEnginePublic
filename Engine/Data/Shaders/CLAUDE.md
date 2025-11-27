# Engine/Data/Shaders - Vulkan GLSL Shaders

GLSL shader source files for the Vulkan 1.2 rendering pipeline. Shaders are compiled to SPIR-V by the DataPacker.

## Shader Headers

### `ShaderLayoutsBase.h`
Defines data structures shared between C++ and GLSL, including uniform buffer layouts and vertex input formats. Uses preprocessor directives to provide compatible definitions for both languages. Contains global constants, push constant layouts, and all uniform buffer object structures used across the rendering pipeline.

### `ShaderFunctions.h`
Common shader utility functions used across multiple shader stages:

- **CalculateDirectionalLight()** - Computes four-component directional lighting weights (East/West/North/South) from a texture coordinate position relative to quad center
- **WorldToVisibleArea()** - Converts world-space position to normalized visible area coordinates
- **BaseHeightPosition()** - Projects a 3D position onto the base height plane along the view ray
- **Transform()** - Matrix-vector multiplication for 4x4 and 3x4 transformation matrices
- **Rotate()** - 2D vector rotation
- **DirectionalLighting()** - Calculates lighting contribution from four cardinal directions with height-based falloff
- **SpecularDirectionalLighting()** - Computes specular highlights from directional lighting
- **Specular()** - Calculates specular reflection using Phong model with three-term falloff
- **SunLighting()** - Combines sun light, ambient light, and shadow/ambient occlusion
- **Lighting()** / **SpecularLighting()** - High-level functions combining directional lighting with color and applying gamma-like power curve
- **ReadLighting()** - Samples three lighting textures (RGB channels) into array
- **IntensityLighting()** / **Sum()** - Computes total lighting intensity
- **SampleNormal()** - Samples and unpacks normal map with animated offset
- **SmokeWindNoise()** / **SmokeNoise()** - Generate animated noise for smoke simulation
- **SmokeShadow()** / **AddSmoke()** - Apply smoke shadowing and blending to scene

## Vertex Shaders

### `QuadsVisibleArea.vert`
Renders arbitrary quads in world space projected to visible area coordinates. Supports multiple rendering modes via push constants: normal visible area, shadow area, or smoke area. Transforms quad vertices from world space to clip space based on selected visible area bounds. Passes per-vertex data including texture coordinates and quad position to fragment shader.

## Fragment Shaders

### `AreaLight.frag`
Renders area lights as directional light sources. Uses `CalculateDirectionalLight()` to compute four-channel directional contributions based on fragment position within the quad. Multiplies directional weights by per-quad direction multipliers, color, texture, and intensity to produce RGB lighting output.

### `PointLight.frag`
Renders point lights using axis-aligned quads. Similar to area lights but uses axis-aligned quad layout without per-quad directional multipliers. Supports rotation via texture coordinate transformation.

## Architecture

- **Dual-language headers**: ShaderLayoutsBase.h uses preprocessor to define structures compatible with both C++ (DirectXMath types) and GLSL (vec4/ivec4 types)
- **Directional lighting system**: Four-channel approach (East/West/North/South) for ambient and area lighting
- **Visible area rendering**: Shaders transform world coordinates to normalized visible area space for efficient culling and rendering
- **Shared utility functions**: Common lighting, transformation, and noise functions centralized in ShaderFunctions.h
- **Non-uniform descriptor indexing**: Shaders using dynamic descriptor array indexing enable `GL_EXT_nonuniform_qualifier` extension and wrap indices with `nonuniformEXT()` for Vulkan validation compliance
