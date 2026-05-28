# Shaders - Vulkan GLSL Shader Source

## Overview

GLSL shader source for the Vulkan 1.2 pipeline, compiled to SPIR-V by the DataPacker. Shaders share data structure definitions with C++ through dual-language headers that preprocessor-switch between DirectXMath and GLSL types. Per-shader-family sources live in subdirectories with their own CLAUDE.md (see *See Also*); this hub documents the shared includes, cross-cutting conventions, and the standalone top-level shaders.

## Shared Includes

Every shader `#include "ShaderLayouts.h"` — the per-project wrapper (`Projects/*/Data/Shaders/`) that includes the engine's `ShaderLayoutsBase.h`, giving the game a layout-extension point. The shared GLSL helper headers live here at top level:

- **ShaderLayoutsBase.h** — dual-language struct definitions (UBO/SSBO/push-constant layouts), shader-wide scalar constants, and `ke*` `VkFormat` constants. `BT_ENGINE` selects DirectXMath types + `constexpr` (C++) vs. GLSL vec types + extension directives.
- **ShaderFunctions.h** — shared fragment/vertex helpers: transforms, EWNS directional/ambient/water lighting, specular, smoke blending, visible-area projection, normal-map sampling.
- **ShaderRandom.h** — xorshift32 + splitmix32 RNG mirroring `common::RandomEngine`; 32-bit because GLSL `uint` is universal (uint64 needs an extension).

## Standalone Shaders

- **Clear.frag** — writes the push-constant pipeline color; VS-out interface declared-but-unused so it matches paired fullscreen vertex shaders and avoids validation warnings.
- **Log.vert** — fullscreen vertex shader that emits per-frame `debugPrintfEXT` diagnostics (frame/render number).
- **DebugTexture.frag** — render-target visualizer; branches on `fDebugTextureFormat` (the `kiDebugTextureFormat*` constants) to decode EWNS directional, linear, terrain-elevation, and RGB/grayscale views, applying the same Uchimura tone map as the lighting combine pass for the float16 directional view.

## Architecture Notes

- **Scalar block layout**: All uniform and storage buffers use `GL_EXT_scalar_block_layout` with a global `layout(scalar) uniform;` directive. Struct members are tightly packed with C++ alignment rules (no std140 padding), keeping CPU/GPU layouts in sync. DataPacker passes `--scalar-block-layout` to spirv-opt. Plain `float[]` arrays are 4-byte stride, not 16-byte.
- **Constant naming**: `kf` floats, `ki` ints, `ke` enum-like / `VkFormat` constants, `kb` bools.
- **Bindless textures**: Unsized `texture2D[]` arrays with separate samplers and `nonuniformEXT()` dynamic indexing.
- **Multi-set descriptors**: Set 0 = global (UBOs, samplers, bindless textures), Set 1 = per-pipeline (SSBOs, combined image samplers), Set 2 = per-material (models only).
- **Push-constant render modes**: Vertex shaders select camera/visible-area/shadow projection without separate pipeline permutations.
- **Four-channel EWNS directional lighting**: RGB stored as separate render targets with EWNS directional weights. Rationale: directional and ambient EWNS samples are summed first, then passed together through the normal-weighted path so both contributions are normal-weighted consistently.
- **World-space directional deposit**: Deposit shaders compute EWNS direction weights from the fragment's world-space offset to the light's center using cos^2 lobe weighting, with an epsilon fallback to omnidirectional near the center.
- **Unit-preserving identities (skip the redundant `normalize()`)**: `reflect(I, N)` is unit when both `I` and `N` are unit (and stays unit under a leading or single-axis sign flip); `cross(a, b)` is unit when `a`, `b` are unit and orthogonal. Callers that pre-normalize their inputs consume the result directly as a direction, saving one `rsqrt + 3 muls` per call site. The `Specular(...)` helper in `ShaderFunctions.h` relies on this (parameters typed as direction normals). Family-specific call sites are documented in the relevant child CLAUDE.md (Water, Model, Objects, Particles).

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
