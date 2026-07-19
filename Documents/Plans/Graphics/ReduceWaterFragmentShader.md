# Reduce Water Fragment Shader

## Context

The GLSL domain review of the current water-fragment optimization measured `Engine/Data/Shaders/Water/Water.frag` at 9,160 `bt-token-v1` tokens, with `main()` containing most of the file. The shader currently combines wave-normal sampling, analytic specular filtering, skybox composition, reflected base-height lighting-texture projection, and final lighting/smoke composition in one function. This is pre-existing structural debt: the reviewed optimization remains correct, but future water changes must reason across an oversized monolithic entry point.

## Design

Split cohesive water-shading stages into narrowly named shader helpers or water-local include files while preserving the existing data flow, operation ordering, compile-time feature switches, descriptor bindings, and rendered output. Keep `main()` as the composition outline. Choose boundaries from the live shader rather than introducing a generic shader framework; likely candidates are sampled-normal construction, specular/skybox composition, and lighting-texture projection.

If new shader include files are added, register them wherever the data packer and Visual Studio shader project membership require. Do not combine this structural work with numerical or visual tuning.

## Critical files

- `Engine/Data/Shaders/Water/Water.frag` — oversized `main()` and the water shading stages to extract.
- `Engine/Data/Shaders/` water-local helper files — only if extraction into includes is the smallest coherent boundary.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/` project metadata — only if new shader source membership requires it.

## Out of scope

- Changing water appearance, normal-octave weights, specular-AA behavior, reflection/refraction behavior, smoke, or lighting formulas.
- Adding new water effects or render passes.
- Performance tuning beyond behavior-preserving structural cleanup.
- Refactoring shared shader utilities unrelated to water.

## Acceptance criteria

- `Water.frag` has a readable composition-level `main()`, and every changed or newly extracted water-local shader source/helper stays below the repository's 5,000 `bt-token-v1` review threshold; the oversized body is not merely relocated into one include.
- Extracted stages preserve expressions, evaluation order where observable, descriptor/layout contracts, and all compile-time water modes.
- A static shader compile matrix passes `WATER_SPEC_AA_MODE` 0 through 4 and both enabled/disabled values of `WATER_SPEC_AA_MIP_HANDOFF` and `WATER_SPEC_AA_FADE_HANDOFF` where those handoffs are reachable, in addition to the shipping shader repack and affected client compilation, with no new GLSL warnings.
- The GLSL domain review finds no NaN/Inf, derivative-control-flow, early-Z, descriptor, scalar-layout, or NVIDIA `inverse()` regression.
- Agent-harness screenshots at representative near and far camera heights show no visible water regression against a pre-change baseline captured with identical settings.

## Notes

- Risk tier: Tier 1 local behavior-preserving shader refactor; the acceptance checks guard against accidental math or control-flow changes.
- Client/graphics-only. No deterministic PostRender/CRC, replay, wire protocol, `kiVersion`, save, or server behavior exposure.
- Shader source changes require a repack. New include files, if any, must participate in dependency tracking and project membership.
- No allocation-tracked CPU path changes are intended.
