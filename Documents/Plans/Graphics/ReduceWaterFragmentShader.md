<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-19T15:51:07.000Z","dependsOn":[]} -->
# Reduce Water Fragment Shader

## Context

The GLSL domain review measured `Engine/Data/Shaders/Water/Water.frag` at 9,160 `bt-token-v1` tokens, with `main()` containing most of the file (currently `main()` spans roughly lines 122-558 of a 558-line file). The shader combines terrain early-out, wave-normal sampling, water color, analytic specular filtering with skybox composition, height-darken/shadow/smoke composition, reflected base-height lighting-texture projection, and final lighting composition in one entry point. Six top-level helpers already exist above `main()`: `DecodeNormal`, `Fresnel`, and the `WATER_SPEC_AA_MODE`-guarded `BoxFilteredLobe`, `FilteredPowerLobe`, `MipVarianceLookup`, `GroupMipVariance`. This is pre-existing structural debt: the shader is correct, but future water changes must reason across an oversized monolithic entry point.

## Design

Behavior-preserving structural split, no numerical or visual change. Because the acceptance ceiling requires the changed `Water.frag` itself to drop below the 5,000 `bt-token-v1` review threshold, extraction into water-local include files is required — in-file helper extraction alone cannot satisfy it, and the oversized body must not merely relocate into one include.

Extract exactly these three cohesive stages of `main()` into new water-local GLSL include files under `Engine/Data/Shaders/Water/` (file names are trivial implementer detail; two or three includes are both acceptable provided every resulting file is under the threshold):

1. **Sampled wave-normal construction** — the `SAMPLE_NORMAL_PRECISE` macro, the three sample groups (`f3SampledNormalOne/Two/Three` blocks gated on `fWeightOne/Two/Three`), and the weighted-sum-then-normalize producing `f3WeightedSum` and `f3SampledNormal`; move `DecodeNormal` with this stage. Preserve the `textureGrad`/`fract` precision pact and the do-not-rotate-reduced-origins rule exactly as commented.
2. **Specular/skybox composition** — the skybox sample (`f3SkyboxWaveNormal`, `f3SkyboxColor`, `f3SkyboxColorSun`) through every `WATER_SPEC_AA_MODE` variant block (modes 0-4, including the `WATER_SPEC_AA_MIP_HANDOFF` and `WATER_SPEC_AA_FADE_HANDOFF` sections) up to and including `fReflection`; move `BoxFilteredLobe`, `FilteredPowerLobe`, `MipVarianceLookup`, `GroupMipVariance` with this stage, keeping their existing `#if` guards.
3. **Reflected base-height lighting-texture projection** — the `fReflectedScale > 0.0f` block producing `f2PositionAtBaseHeightFinal` (reflected-ray projection, falloff power-curve compression, reflected Fresnel mix); move `Fresnel` with this stage.

`main()` remains in `Water.frag` as the composition-level outline, invoking the extracted stages in the current order with the values the existing data flow already passes between them (notably `f2LocalDx`/`f2LocalDy`, the `fWeight*` values, `f3WeightedSum` — mode 3's agreement kernel and the mip handoff read normal-stage outputs — `f3SampledNormal`, `f3ToEyeNormal`, `f3BiasedSunNormal`). Parameter packaging (arguments versus file-scope sharing via textual include) is implementer detail; GLSL includes are textual, so extracted code may keep reading `globalLayout`, `mainLayout`, and the samplers declared in `Water.frag` provided each `#include` lands after those declarations and after the `WATER_SPEC_AA_*` defines. Preserve expressions, observable evaluation order, all `#if`/`#define` compile-time switches (`WATER_SPEC_AA_MODE`, `WATER_SPEC_AA_MIP_HANDOFF`, `WATER_SPEC_AA_FADE_HANDOFF`, `DT_LIGHTING_ONLY`), and every explanatory comment — comments move with their code.

No DataPacker change is needed: `DataPacker/Source/ExportJobs/ExportShader.cpp` captures include dependencies via glslc `-MF`, so new includes join dependency tracking automatically. New include files do need Visual Studio project membership via `/update-vcxproj` (existing shader sources appear as `None`/`ClInclude` items in `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` and `.filters`).

## Critical files

- `Engine/Data/Shaders/Water/Water.frag` — `main()` and the six existing top-level helpers listed above.
- `Engine/Data/Shaders/Water/` — new water-local include files receiving the three extracted stages.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` and `.vcxproj.filters` — membership entries for the new include files only.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the acceptance criteria; add no abstractions, configuration, generic shader framework, or fixes/cleanup to adjacent code encountered along the way.

**In scope** (named regions only; naming a file grants no permission beyond these regions plus the mechanical necessities — `#include` lines, forward declarations — the named change requires):

- `Water.frag`: `main()` body restructuring; relocation of `DecodeNormal`, `Fresnel`, `BoxFilteredLobe`, `FilteredPowerLobe`, `MipVarianceLookup`, `GroupMipVariance`; new `#include` directives.
- New water-local include files containing only the relocated code.
- `BrokenEngineSandbox.vcxproj`/`.filters`: entries for the new files, nothing else.

**Out of scope:**

- Any change to water appearance, normal-octave weights or size/speed multipliers, specular-AA math, reflection/refraction behavior, smoke, shadow, or lighting formulas.
- The `#version`, uniform blocks, sampler/descriptor declarations, `in`/`out` declarations, and the `WATER_SPEC_AA_*` define values in `Water.frag` — these stay in place and unchanged.
- `ShaderLayouts.h`, `ShaderFunctions.h`, `Water.vert`, `WaterDisplacement.comp`, and any CPU-side code.
- New water effects or render passes; performance tuning beyond behavior-preserving structural cleanup; refactoring shared shader utilities unrelated to water.

## Risk tier and invariants

- Tier 1 — local behavior-preserving shader refactor; the acceptance checks guard against accidental math or control-flow changes.
- Client/graphics-only. No deterministic PostRender/CRC, replay, wire protocol, `kiVersion`, save, or server behavior exposure. No allocation-tracked CPU path changes.
- Shader source changes require a DataPacker repack; new includes participate in dependency tracking automatically (glslc `-MF`) but require project membership.

## Acceptance criteria

- `Water.frag` has a readable composition-level `main()`, and every changed or newly extracted water-local shader source/include stays below the repository's 5,000 `bt-token-v1` review threshold; the oversized body is not merely relocated into one include.
- Extracted stages preserve expressions, evaluation order where observable, descriptor/layout contracts, and all compile-time water modes.
- A static shader compile matrix passes `WATER_SPEC_AA_MODE` 0 through 4 and both enabled/disabled values of `WATER_SPEC_AA_MIP_HANDOFF` and `WATER_SPEC_AA_FADE_HANDOFF` where those handoffs are reachable, in addition to the shipping shader repack and affected client compilation, with no new GLSL warnings.
- The GLSL domain review finds no NaN/Inf, derivative-control-flow, early-Z, descriptor, scalar-layout, or NVIDIA `inverse()` regression.
- Agent-harness screenshots at representative near and far camera heights show no visible water regression against a pre-change baseline captured with identical settings.
