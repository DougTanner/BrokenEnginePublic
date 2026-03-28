# Shaders - Vulkan GLSL Shader Source

## Overview

GLSL shader source files for the Vulkan 1.2 rendering pipeline, compiled to SPIR-V by the DataPacker. Shaders share data structure definitions with C++ through dual-language headers that use preprocessor directives to map between DirectXMath and GLSL types.

## Key Files

- **ShaderLayoutsBase.h** - Dual-language (C++/GLSL) header defining all uniform buffer layouts, push constants, vertex formats, and shared constants between CPU and GPU
- **ShaderFunctions.h** - Common GLSL utilities for coordinate transforms, lighting (directional weights inlined into `Lighting` and `SpecularLighting`), specular highlights (`Specular`), sun lighting (`SunLighting`), normal mapping, parallax projection (`BaseHeightPosition`), smoke functions (`WorldToSmokeTexcoord`, `SmokeShadow`, `AddSmoke`, `BlendSmoke`), and shadow stretch projection (`ShadowStretchProjection`)
- **Clear.frag / Log.vert** - Simple utility shaders for render target clearing and debug logging
- **DebugTexture.frag** - Debug visualization shader supporting three format modes: float16 directional lighting (tone-mapped EWNS), UNORM directional lighting (raw EWNS), and float16 linear-range grayscale (value divided by a configurable range). Format is selected per-texture slot at runtime via a uniform index; command buffers need not be re-recorded when cycling textures

## Architecture Notes

- **Scalar block layout**: All uniform and storage buffers use `GL_EXT_scalar_block_layout` with a global `layout(scalar) uniform;` directive in `ShaderLayoutsBase.h`. This means struct members are tightly packed with C++ alignment rules (no std140 padding). The DataPacker also passes `--scalar-block-layout` to spirv-opt. Plain `float[]` arrays in uniform buffers are 4-byte stride, not 16-byte
- **Dual-language headers**: `ShaderLayoutsBase.h` preprocessor-switches between DirectXMath (C++) and GLSL vec types, keeping CPU/GPU struct layouts in sync
- **Bindless textures**: Unsized `texture2D[]` arrays with separate samplers and `nonuniformEXT()` dynamic indexing
- **Multi-set descriptors**: Set 0 = global (UBOs, samplers, bindless textures), Set 1 = per-pipeline (SSBOs, combined image samplers), Set 2 = per-material (models only)
- **Rendering modes via push constants**: Vertex shaders support camera/visible-area/shadow projection modes without separate permutations
- **Four-channel directional lighting**: RGB stored as separate render targets with EWNS directional weights; direction weights are computed once per call in `Lighting`/`SpecularLighting` using component extraction and applied across all three color channels in a single pass
- **World-space directional deposit**: Area light fragment shaders receive interpolated world position and world center varyings from the vertex shader; EWNS direction weights are derived from world-space offset. Both area and point deposit shaders currently use omnidirectional deposit, with alternative directional modes available via compile-time switches

## Adding New Shaders

New shader files are auto-discovered by the DataPacker at build time, but must also be added to the client `.vcxproj` for IDE visibility:

1. Add a `<None Include>` entry in `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` alongside existing shader entries
2. Add a matching `<None Include>` with `<Filter>` in the `.vcxproj.filters` file, using the appropriate `Engine\Data\Shaders\<Subdirectory>` filter

## Known Issues

**NVIDIA driver bug**: Do NOT use `inverse()` on mat3/mat4 in shaders. NVIDIA's compiler hangs indefinitely during pipeline creation. Precompute inverse matrices on the CPU and pass via buffers.

## See Also

- [Lighting/CLAUDE.md](Lighting/CLAUDE.md) - Area, point, and visible light shaders with deposit and radial spread pipeline
- [Quads/CLAUDE.md](Quads/CLAUDE.md) - Quad vertex shaders (visible-area, axis-aligned, fullscreen)
- [Model/CLAUDE.md](Model/CLAUDE.md) - PBR model rendering shaders
- [Objects/CLAUDE.md](Objects/CLAUDE.md) - Game object shaders (hex shields, player)
- [Particles/CLAUDE.md](Particles/CLAUDE.md) - GPU-driven particle compute and render shaders
- [Shadow/CLAUDE.md](Shadow/CLAUDE.md) - Shadow map generation and blur
- [Smoke/CLAUDE.md](Smoke/CLAUDE.md) - Volumetric smoke simulation
- [Terrain/CLAUDE.md](Terrain/CLAUDE.md) - Terrain G-buffer generation and compositing
- [Water/CLAUDE.md](Water/CLAUDE.md) - Gerstner wave ocean surface
- [Wind/CLAUDE.md](Wind/CLAUDE.md) - 2D wind velocity field simulation
