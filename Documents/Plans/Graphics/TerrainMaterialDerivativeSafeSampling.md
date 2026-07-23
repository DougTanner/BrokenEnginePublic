<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-19T15:40:15.000Z","dependsOn":[]} -->
# Terrain Material Derivative-Safe Sampling

## Context

The height-faded terrain detail-normal path has a shader-correctness hazard in the `main` material branches of `Engine/Data/Shaders/Terrain/Terrain.frag` (currently around lines 86-110). `fRockPercent` and `fBeachPercent` come from the per-fragment material-mask texture, so the branches they control may diverge within a fragment quad. Inside those branches, six `SampleNormal` calls and the `rockSampler` / `sandSampler` color reads use implicit LOD.

`SampleNormal` currently calls `texture()` in `Engine/Data/Shaders/ShaderFunctions.h:61`. Implicit-LOD sampling depends on fragment derivatives, which are not defined when evaluated only by a non-uniform subset of the quad. The height-fade gates are dynamically uniform and remain nested inside the varying material-mask branches, so they neither create nor resolve the varying-mask hazard. Derivative-safe sampling is independent from height-based sample trimming.

No live plan found by the authoritative queue validation owns this terrain-material root cause. `Documents/Plans/Graphics/ShaderReview/00_Overview.md` mentions implicit-LOD sampling only for compute shaders, which is a different stage and implementation boundary.

## Design

1. Compute every gradient needed by the rock and beach detail UVs before entering the `fRockPercent` / `fBeachPercent` branches, at uniform control-flow depth.
2. Replace all implicit-LOD reads inside those varying branches with `textureGrad`, or another reviewed derivative-safe equivalent that preserves the intended automatic mip selection. This includes the three rock normal reads, three beach normal reads, rock color read, and sand color read.
3. Keep material-mask conditions, normal weights, color blends, sampler bindings, and the dynamically uniform height-fade gates unchanged. Keep any helper change narrowly scoped to the affected terrain call sites.

## Critical files

- `Engine/Data/Shaders/Terrain/Terrain.frag` — `main` rock and beach material branches; compute gradients before varying control flow and consume them in all eight affected samples.
- `Engine/Data/Shaders/ShaderFunctions.h` — `SampleNormal`; adjust or add the narrow gradient-taking form only if needed by the terrain implementation.

## Out of scope

- Changing material masks, branch thresholds, normal weights, color blend behavior, or height-fade behavior.
- Removing, combining, or retuning texture samples.
- Auditing unrelated implicit-LOD samples in other shaders or compute-stage sampling already covered by the shader-review overview.
- Changing texture assets, sampler objects, descriptors, or pack layout.

## Acceptance criteria

- No implicit-LOD texture operation remains inside the fragment-varying rock or beach material branches.
- Every explicit gradient used by those samples is computed before the varying branches, and shader review confirms the resulting sampling is derivative-safe.
- Local shader export succeeds without GLSL or SPIR-V errors.
- A live terrain comparison across rock, beach, and their mask boundaries shows no new seams, mip discontinuities, or material-detail regressions at representative near and distant camera heights.

## Notes

- Client graphics and shader export only. No deterministic simulation/CRC, replay, wire protocol, client/server guard, allocation-tracked path, `DataHeader::kiVersion`, or `.pack` layout exposure.
- This plan may change `ShaderFunctions.h` source but should not broaden the shared helper contract beyond the currently affected terrain sampling need.
