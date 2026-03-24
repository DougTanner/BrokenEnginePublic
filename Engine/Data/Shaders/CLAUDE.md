# Shaders - Vulkan GLSL Shader Source

## Overview

GLSL shader source files for the Vulkan 1.2 rendering pipeline, compiled to SPIR-V by the DataPacker. Shaders share data structure definitions with C++ through dual-language headers that use preprocessor directives to map between DirectXMath and GLSL types.

## Key Files

- **ShaderLayoutsBase.h** - Dual-language (C++/GLSL) header defining all uniform buffer layouts, push constants, vertex formats, and shared constants between CPU and GPU
- **ShaderFunctions.h** - Common GLSL utilities for coordinate transforms, lighting (directional weights inlined into `Lighting` and `SpecularLighting`), specular highlights, normal mapping, parallax projection (`BaseHeightPosition`), smoke functions (`WorldToSmokeTexcoord`, `SmokeShadow`, `AddSmoke`, `BlendSmoke`), and shadow stretch projection (`ShadowStretchProjection`)
- **Clear.frag / Log.vert** - Simple utility shaders for render target clearing and debug logging

## Architecture Notes

- **Dual-language headers**: `ShaderLayoutsBase.h` preprocessor-switches between DirectXMath (C++) and GLSL vec types, keeping CPU/GPU struct layouts in sync
- **Bindless textures**: Unsized `texture2D[]` arrays with separate samplers and `nonuniformEXT()` dynamic indexing
- **Multi-set descriptors**: Set 0 = global (UBOs, samplers, bindless textures), Set 1 = per-pipeline (SSBOs, combined image samplers), Set 2 = per-material (models only)
- **Rendering modes via push constants**: Vertex shaders support camera/visible-area/shadow projection modes without separate permutations
- **Four-channel directional lighting**: RGB stored as separate render targets with EWNS directional weights; direction weights are computed once per call in `Lighting`/`SpecularLighting` using component extraction and applied across all three color channels in a single pass

## Known Issues

**NVIDIA driver bug**: Do NOT use `inverse()` on mat3/mat4 in shaders. NVIDIA's compiler hangs indefinitely during pipeline creation. Precompute inverse matrices on the CPU and pass via buffers.

## See Also

- [Lighting/CLAUDE.md](Lighting/CLAUDE.md) - Area, point, and visible light shaders with compute-based cascaded light spreading
- [Model/CLAUDE.md](Model/CLAUDE.md) - PBR model rendering shaders
- [Objects/CLAUDE.md](Objects/CLAUDE.md) - Game object shaders (hex shields, player)
- [Particles/CLAUDE.md](Particles/CLAUDE.md) - GPU-driven particle compute and render shaders
- [Shadow/CLAUDE.md](Shadow/CLAUDE.md) - Shadow map generation and blur
- [Smoke/CLAUDE.md](Smoke/CLAUDE.md) - Volumetric smoke simulation
- [Terrain/CLAUDE.md](Terrain/CLAUDE.md) - Terrain G-buffer generation and compositing
- [Water/CLAUDE.md](Water/CLAUDE.md) - Gerstner wave ocean surface
- [Wind/CLAUDE.md](Wind/CLAUDE.md) - 2D wind velocity field simulation
