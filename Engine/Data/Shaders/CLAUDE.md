# Shaders - Vulkan GLSL Shader Source

## Overview

GLSL shader source for the Vulkan 1.2 pipeline, compiled to SPIR-V by the DataPacker. Shaders share data structure definitions with C++ through dual-language headers that preprocessor-switch between DirectXMath and GLSL types. Per-shader-family sources live in subdirectories with their own CLAUDE.md (see *See Also*); this hub documents the shared includes, cross-cutting conventions, and the standalone top-level shaders.

## Shared Includes

Nearly every shader `#include "ShaderLayouts.h"` — the per-project wrapper (`Projects/*/Data/Shaders/`) that includes the engine's `ShaderLayoutsBase.h`, giving the game a layout-extension point (exceptions: a few self-contained shaders that need no layouts, e.g. the UI depth prepass below, the debug wireframe frag, the BRDF LUT pair). The shared GLSL helper headers live here at top level:

- **ShaderLayoutsBase.h** — dual-language struct definitions (UBO/SSBO/push-constant layouts), shader-wide scalar constants, and `ke*` `VkFormat` constants. `BT_ENGINE` selects DirectXMath types + `constexpr` (C++) vs. GLSL vec types + extension directives. Compile-time debug toggles (`debugPrintfEXT`, shader realtime clock) live at the top of this file so shader extension enablement and the matching C++ `kb*` constants flip with a single switch. Global Set-0 binding numbers are defined here as `kiGlobalBinding*` constants and consumed by both the GLSL `layout(set=0, binding=...)` qualifiers in every shader and the C++ descriptor layout/writes in `TextureDescriptors` — the single source of truth for Set-0 slot assignments. Set-1 per-pipeline binding numbers (`kiWaterBinding*` for water displacement, `kiModelBinding*` for mesh data and joint matrices) use the same dual-language mechanism — single-sourced here, consumed by both the GLSL `layout(set=1, ...)` qualifiers in the water and model shaders and the C++ descriptor writes in `PipelineManager` and `BufferManager`.
- **ShaderFunctions.h** — shared fragment/vertex helpers: transforms, EWNS directional/ambient/water lighting, specular, smoke blending, visible-area projection, normal-map sampling.
- **ShaderRandom.h** — xorshift32 + splitmix32 RNG mirroring `common::RandomEngine`; 32-bit because GLSL `uint` is universal (uint64 needs an extension).

## Standalone Shaders

- **Clear.frag** — writes the push-constant pipeline color; VS-out interface declared-but-unused so it matches paired fullscreen vertex shaders and avoids validation warnings.
- **Log.vert** — fullscreen vertex shader that emits per-frame `debugPrintfEXT` diagnostics (frame/render number).
- **DebugTexture.frag** — render-target visualizer; branches on a format selector to decode each debug-view family (EWNS directional including combined-direction modes, linear, terrain elevation, plain RGB/grayscale), applying the same Uchimura tone map as the lighting combine pass to the float16 lighting-directional view (combined-direction modes skip tone mapping).

## Ui Shaders

The `Ui/` subdirectory has no CLAUDE.md of its own; documented here:

- **ProfileText.frag** — profiler-overlay text; per-instance packed color, alpha modulated by the glyph texture's `.r` channel (the font atlas is single-channel BC4).
- **UiDepthPrepass.vert/.frag** — depth-only prepass writing depth under opaque ImGui rects so the world behind them is early-Z rejected. Self-contained (declares its own scalar layout instead of including `ShaderLayouts.h`); expands two triangles per rect from `gl_VertexIndex` reading an unsized rect SSBO, with an empty fragment shader.

## Architecture Notes

- **Scalar block layout**: All uniform and storage buffers use `GL_EXT_scalar_block_layout` with a global `layout(scalar) uniform;` directive. Struct members are tightly packed with C++ alignment rules (no std140 padding), keeping CPU/GPU layouts in sync. DataPacker passes `--scalar-block-layout` to spirv-opt. Plain `float[]` arrays are 4-byte stride, not 16-byte.
- **`BT_ENGINE` is the language discriminator, not `BT_CLIENT`**: it is defined in both client and server builds, so layout structs and constants are visible to the headless server — no `BT_CLIENT` guards in these files.
- **Constant naming**: `kf` floats, `ki` ints, `ke` enum-like / `VkFormat` constants, `kb` bools.
- **Bindless textures**: Unsized `texture2D[]` arrays with separate samplers and `nonuniformEXT()` dynamic indexing.
- **Multi-set descriptors**: Set 0 = global (UBOs, samplers, bindless textures), Set 1 = per-pipeline (SSBOs, combined image samplers), Set 2 = per-material (models only).
- **Push-constant render modes**: Vertex shaders select camera/visible-area/shadow projection without separate pipeline permutations.
- **Four-channel EWNS directional lighting**: RGB stored as separate render targets with EWNS directional weights. Rationale: directional and ambient EWNS samples are summed first, then passed together through the normal-weighted path so both contributions are normal-weighted consistently.
- **World-space directional deposit**: *Light-source* deposit shaders compute EWNS direction weights from the fragment's world-space offset to the light's center using cos^2 lobe weighting, with an epsilon fallback to omnidirectional near the center. *Surface-normal* deposit shaders (e.g. `HexShieldLighting.frag`) instead project the blended center-normal's XY, with a zero-deposit epsilon fallback — see [Objects/CLAUDE.md](Objects/CLAUDE.md).
- **Unit-preserving identities (skip the redundant `normalize()`)**: `reflect(I, N)` is unit when both `I` and `N` are unit (and stays unit under a leading or single-axis sign flip); `cross(a, b)` is unit when `a`, `b` are unit and orthogonal. Callers that pre-normalize their inputs consume the result directly as a direction, saving one `rsqrt + 3 muls` per call site. The `Specular(...)` helper in `ShaderFunctions.h` relies on this (parameters typed as direction normals). Family-specific call sites are documented in the relevant child CLAUDE.md (Water, Model, Objects, Particles).

## Known Issues

**NVIDIA driver bug**: never call `inverse()` on mat3/mat4 in shaders — NVIDIA's compiler hangs indefinitely during pipeline creation. Precompute inverse matrices on the CPU and pass via buffers. This is the sole confirmed trigger: an earlier attribution of the same hang to "large `mat4[]` arrays" (the joint-matrix buffer split) was a misdiagnosis — both changes landed in one fix commit, and the engine dynamically indexes the large runtime-sized `mat4 jointMatrices[]` SSBO hang-free. The joint split stays as a layout choice (no embedded per-mesh joint cap).

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
