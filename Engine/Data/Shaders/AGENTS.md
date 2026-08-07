# Shaders - Shared Vulkan GLSL

## Overview

Vulkan 1.2 GLSL compiled to SPIR-V by DataPacker. The project `ShaderLayouts.h` wrapper extends engine `ShaderLayoutsBase.h`, keeping CPU and shader layouts single-sourced.

## Shared Contract

- `BT_ENGINE` selects C++ versus GLSL declarations; it is not a client-affinity guard. Layout headers are consumed by client and server builds.
- Uniform blocks inherit scalar layout from `ShaderLayoutsBase.h`; storage blocks declare it explicitly where the CPU/GLSL contract requires it. Preserve CPU/GLSL field types, alignment, and order together; plain scalar arrays in scalar-layout blocks retain scalar stride.
- Uniform-only terms — products, reciprocals, and pre-normalized vectors whose every operand is invocation-invariant — are merged CPU-side into precomputed layout fields by the Render populators (`../../Source/Graphics/Render/AGENTS.md`); shaders read the finished field rather than recomputing it per invocation. Route new such math to the populator, not the shader; `ShaderLayoutsBase.h` field comments own the reason each term was merged CPU-side and the operands deliberately left raw.
- Descriptor ownership is semantic: Set 0 is global, Set 1 is per-pipeline, and Set 2 is per-material. Binding constants live in the shared layout header and must match C++ layout/writes.
- Bindless texture indices require `nonuniformEXT()`. Push constants select projection/render modes where command buffers are recorded once.
- `ShaderFunctions.h` owns shared transforms, projection, normal mapping, smoke blending, and lighting helpers. `ShaderRandom.h` mirrors the engine's 32-bit RNG family.

## Lighting and Vector Invariants

- EWNS is one 4-float vector holding four directional values in slot order east, west, north, south. The consuming helpers in `ShaderFunctions.h` read them that way: slot 0 is weighted by a surface normal pointing +x (east), slot 1 by -x (west), slot 2 by +y (north), and slot 3 by -y (south).
- Every depositor packs the direction the light arrives from, so slot 0 fills when the light source lies east of the receiving texel. Offset-based depositors (`Lighting/AreaLight.frag`, `Lighting/PointLight.frag`) pack the light-center-to-fragment offset as `(-x, +x, -y, +y)`; the surface-normal depositor (`Objects/HexShieldLighting.frag`) packs its projected blended normal the same way.
- Directional lighting uses four EWNS channels. Light-source deposits derive directional weights from world-space source offsets; surface-normal deposits use the projected blended normal. Keep the documented epsilon fallbacks at family leaves.
- Final lighting and shadow helpers are plain texture reads: every pass writes its whole texture, so the border samplers (transparent black for lighting, white for shadow) own rejection outside the footprint. Keep those sampler flags on every consumer pipeline.
- Skip redundant normalization only where the identity is proven: `reflect(I, N)` preserves length for unit inputs, and `cross(a, b)` is unit only for unit orthogonal inputs.
- Never call GLSL `inverse()` on a `mat3` or `mat4`: NVIDIA's shader compiler hangs forever while the pipeline is being created, and reports nothing. This is a recorded debugging result — reading the shader source cannot show it. Supply inverse transforms from the CPU or use a proven family-specific alternative. That call is the only confirmed trigger; large `mat4[]` arrays were investigated and ruled out, because the runtime-sized `jointMatrices[]` storage buffer (`Model/ModelCommon.h`) is indexed dynamically without hanging.

## Top-Level Shaders

- HDR resolve performs the final frame tone map and color grading; material shaders output linear HDR.
- Debug texture display decodes renderer targets, including EWNS views.
- The UI depth prepass is self-contained and expands opaque ImGui rectangles to populate depth before world rendering.

## Family Documentation

- Debug (`Debug/AGENTS.md`), Lighting (`Lighting/AGENTS.md`), Model (`Model/AGENTS.md`), Objects (`Objects/AGENTS.md`), Particles (`Particles/AGENTS.md`), Quads (`Quads/AGENTS.md`), Shadow (`Shadow/AGENTS.md`), Smoke (`Smoke/AGENTS.md`), Terrain (`Terrain/AGENTS.md`), Water (`Water/AGENTS.md`), Wind (`Wind/AGENTS.md`)

Family documents own algorithms and local correctness constraints; they do not repeat layouts, bindings, EWNS, or the `inverse()` rule.
