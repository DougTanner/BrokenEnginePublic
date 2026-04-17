# Shaders - Vulkan GLSL Shader Source

## Overview

GLSL shader source for the Vulkan 1.2 pipeline, compiled to SPIR-V by the DataPacker. Shaders share data structure definitions with C++ through dual-language headers that preprocessor-switch between DirectXMath and GLSL types.

## Architecture Notes

- **Scalar block layout**: All uniform and storage buffers use `GL_EXT_scalar_block_layout` with a global `layout(scalar) uniform;` directive. Struct members are tightly packed with C++ alignment rules (no std140 padding), keeping CPU/GPU layouts in sync. DataPacker passes `--scalar-block-layout` to spirv-opt. Plain `float[]` arrays are 4-byte stride, not 16-byte.
- **Dual-language headers**: `ShaderLayoutsBase.h` preprocessor-switches between DirectXMath (C++) and GLSL vec types so the same struct definition serves both sides.
- **Bindless textures**: Unsized `texture2D[]` arrays with separate samplers and `nonuniformEXT()` dynamic indexing.
- **Multi-set descriptors**: Set 0 = global (UBOs, samplers, bindless textures), Set 1 = per-pipeline (SSBOs, combined image samplers), Set 2 = per-material (models only).
- **Push-constant render modes**: Vertex shaders select camera/visible-area/shadow projection without separate pipeline permutations.
- **Four-channel EWNS directional lighting**: RGB stored as separate render targets with EWNS directional weights. Rationale: directional and ambient EWNS samples are summed first, then passed together through the normal-weighted path so both contributions are normal-weighted consistently.
- **World-space directional deposit**: Deposit shaders compute EWNS direction weights from the fragment's world-space offset to the light's center using cos^2 lobe weighting, with an epsilon fallback to omnidirectional near the center.

## Known Issues

**NVIDIA driver bug**: Do NOT use `inverse()` on mat3/mat4 in shaders. NVIDIA's compiler hangs indefinitely during pipeline creation. Precompute inverse matrices on the CPU and pass via buffers.

## See Also

- [Debug/CLAUDE.md](Debug/CLAUDE.md)
- [Lighting/CLAUDE.md](Lighting/CLAUDE.md)
- [Quads/CLAUDE.md](Quads/CLAUDE.md)
- [Model/CLAUDE.md](Model/CLAUDE.md)
- [Objects/CLAUDE.md](Objects/CLAUDE.md)
- [Particles/CLAUDE.md](Particles/CLAUDE.md)
- [Shadow/CLAUDE.md](Shadow/CLAUDE.md)
- [Smoke/CLAUDE.md](Smoke/CLAUDE.md)
- [Terrain/CLAUDE.md](Terrain/CLAUDE.md)
- [Water/CLAUDE.md](Water/CLAUDE.md)
- [Wind/CLAUDE.md](Wind/CLAUDE.md)
