# Shader Review — Water

Files: `Water.vert`, `Water.frag`

## NEEDS FIXES

### `Engine/Data/Shaders/Water/Water.vert`

Correctness:
- line 94 — `gl_Position = vec4(0, 0, -100, 0)` with `w = 0` causes divide-by-zero in perspective; some drivers propagate NaN into primitive assembly and can corrupt neighboring triangles. Prefer a guaranteed-outside-clip kill like `vec4(2.0, 2.0, 2.0, 1.0)`.
- line 77 — `normalize(f3LowNormal + f3MediumNormal)` safe only because both seeds are `(0,0,1)`; a refactor changing the seeds could zero-sum. Use `v / max(length(v), 1e-6)` for robustness.

Performance:
- lines 46-47, 67-68 — per-vertex `sin`/`cos` inside loops up to 256 iterations. Expected for Gerstner; current reuse of `fRadians` is the right pattern.
- line 91 — `texture()` in a vertex shader; prefer explicit `textureLod(..., 0.0)` to document intent and avoid implementation-defined behavior.

Style (non-blocking):
- line 29 — function name `Gertsner` should be `Gerstner`.

### `Engine/Data/Shaders/Water/Water.frag`

Broken Engine:
- lines 17-26 — all samplers use `set = 1` but several are global resources (shadow, objectShadows, elevation, skybox, noise, smoke, depthLut, lighting). Audit vs the pipeline layout; likely `pLightingSamplers` and `smokeSampler` should move to set 0.

Correctness:
- line 110 — `normalize(f3SampledNormalOne + f3SampledNormalTwo)` can be zero if two sampled normals cancel. Guard with `length(v) > 1e-6`.
- line 133 — `normalize((1.0 - w)*f3SampledNormal + w*f3InNormal)` can degenerate to zero if inputs are opposite. Same guard.
- line 141 — `normalize(reflect(unit, unit))` is already unit; drop the outer normalize.
- `Fresnel()` defined in this file is never called — dead code, remove.

Performance:
- line 134 — `textureLod(skyboxSampler, -normalize(reflect(...)), lod)` — drop outer `normalize`.
- lines 170-172 — four `pow()` calls per fragment × i (8 total) for hue-preserving scaling. `mix(...)` always evaluates both branches. Consider uniform branch on `fWaterAmbientPowerMode`.
- line 198 + line 152 — `SmokeShadow` (line 152) and the explicit `pow(fSmokeRaw, fSmokePower)` at 198 re-sample the same `smokeSampler`. Sample once.
- line 162 — aggregate-initialized `vec4 pf4LightingBaseHeight[3] = {…}`; `ReadLighting()` helper exists in `ShaderFunctions.h` — prefer it.
