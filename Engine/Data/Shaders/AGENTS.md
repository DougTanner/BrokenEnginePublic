# Shaders - Shared Vulkan GLSL

## Overview

Vulkan 1.2 GLSL compiled to SPIR-V by DataPacker. The project `ShaderLayouts.h` wrapper extends engine `ShaderLayoutsBase.h`, keeping CPU and shader layouts single-sourced.

## Shared Contract

- `BT_ENGINE` selects C++ versus GLSL declarations; it is not a client-affinity guard. Layout headers are consumed by client and server builds.
- Uniform blocks inherit scalar layout from `ShaderLayoutsBase.h`; storage blocks declare it explicitly where the CPU/GLSL contract requires it. Preserve CPU/GLSL field types, alignment, and order together; plain scalar arrays in scalar-layout blocks retain scalar stride.
- Descriptor ownership is semantic: Set 0 is global, Set 1 is per-pipeline, and Set 2 is per-material. Binding constants live in the shared layout header and must match C++ layout/writes.
- Bindless texture indices require `nonuniformEXT()`. Push constants select projection/render modes where command buffers are recorded once.
- `ShaderFunctions.h` owns shared transforms, projection, normal mapping, smoke blending, and lighting helpers. `ShaderRandom.h` mirrors the engine's 32-bit RNG family.

## Lighting and Vector Invariants

- Directional lighting uses four EWNS channels. Light-source deposits derive directional weights from world-space source offsets; surface-normal deposits use the projected blended normal. Keep the documented epsilon fallbacks at family leaves.
- Skip redundant normalization only where the identity is proven: `reflect(I, N)` preserves length for unit inputs, and `cross(a, b)` is unit only for unit orthogonal inputs.
- Do not call GLSL `inverse()` on matrices. Supply inverse transforms from the CPU or use a proven family-specific alternative.

## Top-Level Shaders

- HDR resolve performs the final frame tone map and color grading; material shaders output linear HDR.
- Debug texture display decodes renderer targets, including EWNS views.
- The UI depth prepass is self-contained and expands opaque ImGui rectangles to populate depth before world rendering.

## Family Documentation

- [Debug](Debug/AGENTS.md), [Lighting](Lighting/AGENTS.md), [Model](Model/AGENTS.md), [Objects](Objects/AGENTS.md), [Particles](Particles/AGENTS.md), [Quads](Quads/AGENTS.md), [Shadow](Shadow/AGENTS.md), [Smoke](Smoke/AGENTS.md), [Terrain](Terrain/AGENTS.md), [Water](Water/AGENTS.md), [Wind](Wind/AGENTS.md)

Family documents own algorithms and local correctness constraints; they do not repeat layouts, bindings, EWNS, or the `inverse()` rule.
