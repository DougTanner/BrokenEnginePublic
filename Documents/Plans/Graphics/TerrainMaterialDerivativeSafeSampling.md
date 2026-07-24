<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-19T15:40:15.000Z","dependsOn":[]} -->
# Terrain Material Derivative-Safe Sampling

## Context

`main` in `Engine/Data/Shaders/Terrain/Terrain.frag` blends rock and beach detail inside two branches gated by per-fragment material-mask values (currently lines 86-111): `if (fRockPercent > 0.001f)` and `if (fBeachPercent > 0.001f)`. `fRockPercent` and `fBeachPercent` derive from the `masksTextureSamplers` material-mask texture, so these branches can diverge within a fragment quad. Inside them, eight texture reads use implicit LOD:

- Rock branch: three `SampleNormal` calls (`rockNormalsSampler0/1/2`, UV base `f3InPosition.xy + f3InPosition.z`) plus the `rockSampler` color read (`texture(rockSampler, globalLayout.fTerrainRockSize * f3InPosition.xy)`).
- Beach branch: three `SampleNormal` calls (`sandNormalsSampler0` with UV base `f3InPosition.xy`; `sandNormalsSampler1/2` with UV base `f3InPosition.yx`) plus the `sandSampler` color read (`texture(sandSampler, globalLayout.fTerrainBeachSandSize * f3InPosition.xy)`).

`SampleNormal` (`Engine/Data/Shaders/ShaderFunctions.h`, currently lines 58-65) calls `texture()`, whose implicit-LOD mip selection depends on fragment derivatives that are undefined when only a non-uniform subset of the quad executes the call. The nested detail-normal gates (`globalLayout.fTerrainRockNormalsBlend > 0.0f`, `globalLayout.fTerrainBeachNormalsBlend > 0.0f` — the CPU-folded height-fade gates) are dynamically uniform; they neither create nor resolve the varying-mask hazard, and derivative-safe sampling is independent of height-based sample trimming.

The six terrain calls are the only `SampleNormal` call sites in the repository, so its signature can carry explicit gradients without affecting any other shader.

At plan creation, no live plan owned this root cause per authoritative queue validation; `Documents/Plans/Graphics/ShaderReview/00_Overview.md` covers implicit-LOD sampling only for compute shaders, a different stage and implementation boundary.

## Design

1. In `Terrain.frag` `main`, before the `fRockPercent` branch, compute the screen-space derivatives of every UV base the eight reads need via `dFdx`/`dFdy` at uniform control-flow depth: derivatives of `f3InPosition.xy + f3InPosition.z` (rock normals), `f3InPosition.xy` (beach normal 0, rock color, sand color), and `f3InPosition.yx` (beach normals 1-2; obtainable by swizzling the `f3InPosition.xy` derivatives). Store them in locals.
2. Convert `SampleNormal` in `ShaderFunctions.h` to a gradient-taking form: append explicit-gradient parameters (e.g. `vec2 f2GradX, vec2 f2GradY`, the derivatives of `f2Position`) and replace its `texture()` read with `textureGrad`, scaling the passed position derivatives by `fSize` for the final UV gradients (the `f2Offset` and `fSpeed * fElapsedTime` terms are invocation-uniform and contribute zero derivative). Do not keep a non-gradient overload — the terrain calls are the sole callers, and an unused implicit-LOD form would be dead code.
3. Update the six `SampleNormal` call sites in the rock and beach branches to pass the precomputed derivatives, and replace the `rockSampler` and `sandSampler` `texture()` reads with `textureGrad`, passing the `f3InPosition.xy` derivatives scaled by `globalLayout.fTerrainRockSize` / `globalLayout.fTerrainBeachSandSize` respectively.
4. Change nothing else: material-mask decode, branch thresholds, normal weights (`2.0f`/`0.5f`/`1.0f` beach sum, rock sum), color `mix` blends, sampler bindings, and the dynamically uniform detail-normal gates all keep their current values and structure. `textureGrad` with quad-wide gradients preserves the intended automatic mip selection.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change below, add no abstractions, configuration, refactors, or fixes to adjacent code, and touch nothing in a listed file beyond the named regions plus the mechanical necessities those changes require.

In scope:

- `Engine/Data/Shaders/Terrain/Terrain.frag` — inside `main` only: new gradient locals immediately before the `if (fRockPercent > 0.001f)` branch, and the eight texture reads inside the `fRockPercent` / `fBeachPercent` branches (six `SampleNormal` calls, `rockSampler` read, `sandSampler` read).
- `Engine/Data/Shaders/ShaderFunctions.h` — the `SampleNormal` function body and signature only, as described in Design step 2.

Out of scope:

- Changing material masks, branch thresholds, normal weights, color blend behavior, or the detail-normal (height-fade) gate behavior.
- Removing, combining, or retuning texture samples.
- Any other function in `ShaderFunctions.h`, any other read in `Terrain.frag` (the uniform-control-flow `texture()` reads outside the two branches stay implicit-LOD), and implicit-LOD audits in other shaders or compute-stage sampling covered by the shader-review overview.
- Changing texture assets, sampler objects, descriptors, bindings, or pack layout.

## Critical files

- `Engine/Data/Shaders/Terrain/Terrain.frag`
- `Engine/Data/Shaders/ShaderFunctions.h`

## Risk tier

Tier 2 — scoped client-graphics shader behavior. No determinism/CRC, wire/protocol, serialization or data-layout, save/replay, threading, or trust-boundary exposure; `SampleNormal`'s signature change is fully contained by its terrain-only call sites.

## Acceptance criteria

- No implicit-LOD texture operation remains inside the fragment-varying rock or beach material branches of `Terrain.frag`.
- Every explicit gradient those samples consume is computed before the varying branches at uniform control-flow depth, and shader review confirms the sampling is derivative-safe.
- No `SampleNormal` call site outside `Terrain.frag` exists (re-verify by grep before landing).
- Local shader export succeeds without GLSL or SPIR-V errors.
- A live terrain comparison across rock, beach, and their mask boundaries shows no new seams, mip discontinuities, or material-detail regressions at representative near and distant camera heights.

## Notes

- Client graphics and shader export only. No deterministic simulation/CRC, replay, wire protocol, client/server guard, allocation-tracked path, `DataHeader::kiVersion`, or `.pack` layout exposure.
- `ShaderFunctions.h` change must not broaden the shared helper contract beyond the terrain sampling need; the gradient parameters are that need.
