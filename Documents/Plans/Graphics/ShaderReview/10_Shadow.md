# Shader Review — Shadow

Files: `Shadow.comp`, `ShadowBlurH.comp`, `ShadowBlurV.comp`, `ObjectShadowsBlurH.comp`, `ObjectShadowsBlurV.comp`

## NEEDS FIXES

### `Engine/Data/Shaders/Shadow/Shadow.comp`

Correctness:
- lines 21, 39 — `texture()` in compute has no implicit derivatives; LOD is undefined. Use `textureLod(elevationTextureSampler, uv, 0.0)`.
- line 24 — `normalize(vec3(fShadowDirectionMultiplier, 0, 0))` produces NaN when the scalar is 0 and otherwise just equals `vec3(sign(x), 0, 0)` — replace with `vec3(sign(fShadowDirectionMultiplier), 0, 0)`.
- line 52 — `acos(dot(a, b))` of two normalized vectors can feed values slightly outside `[-1, 1]` from rounding → NaN. Wrap: `acos(clamp(dot(...), -1.0, 1.0))`.
- line 31 — loop counter `i` is `uint` but `iShadowIncrement` is signed `int32_t`; with negative increment the equality test can miss the exit condition. Use `int i` with `<`/`>` based on sign, or make exit distance-based only.

Performance:
- line 14 — image is write-only; add `writeonly`.
- lines 21, 39 — hoist `vec2 invSize = 1.0 / vec2(...)` outside the loop and multiply.
- line 6 — `local_size_x = 1, local_size_y = 64` hurts 2D texture-cache locality on elevation fetch; consider `8×8`.

### `Engine/Data/Shaders/Shadow/ShadowBlurH.comp`

Performance:
- line 5 — workgroup `kiComputeTileSize * kiShadowTextureExecutionSize` = 8×64 = **512** exceeds the [32, 256] guideline; reduce Y (e.g. 8×8 or 16×16) to match `ObjectShadowsBlurH.comp`, or justify with a comment.
- line 14 — missing `writeonly`.
- line 38 — 11 dependent sampler fetches per invocation; consider bilinear-tap trick to halve sample count.

Correctness:
- line 30 — `fInvSigma = iRadius / fSigma`; if `fSigma` is ever 0, Inf; `fA` then collapses all weights to 1.0. Guard `max(fSigma, 1e-6)`.
- line 32 — `fTotal` not normalized by weight sum; for sigmas where the tail isn't negligible at radius 5, output brightness drifts with sigma. Confirm intent (also applies to `ShadowBlurV.comp:32`).

### `Engine/Data/Shaders/Shadow/ShadowBlurV.comp`

Correctness:
- lines 30 / 35 — `fInvSigma` is actually `iRadius/sigma`; `fA = j / fInvSigma = j*sigma/iRadius` inverts standard Gaussian (larger sigma → narrower kernel). If `fShadowBlurSigma` is meant as a true sigma, replace with `float fA = float(j) / fSigma;`.
- line 27 — `fSigma` used as divisor with no zero-guard.
- line 37 — UV sampling omits the `+0.5` pixel-center offset; bilinear sampling biases blur by half a texel per axis.

Performance:
- line 5 — workgroup 8×64 = 512 threads; consider 8×32 = 256 to match `ObjectShadowsBlurV.comp`.
- line 14 — missing `writeonly`.
- line 36 — 11 weights are constant per frame; precompute or use a const float array keyed by `|j|`.

### `Engine/Data/Shaders/Shadow/ObjectShadowsBlurH.comp`

Correctness:
- line 26 — `fObjectShadowsBlurSigma` unchecked; if CPU ever writes 0, `fInvSigma` becomes Inf/NaN. Guard `max(..., 1e-4f)`.
- line 37 — `texture()` in compute has undefined LOD; use `textureLod(..., 0.0)`.

Performance:
- line 14 — missing `writeonly`.
- lines 32-39 — hoist `float fInvWidth = 1.0 / float(i2Size.x)` out of the loop.
- line 26 — CPU could upload `fInvSigma` directly to save a per-thread divide.

### `Engine/Data/Shaders/Shadow/ObjectShadowsBlurV.comp`

PASS — 8×8=64 workgroup, no barriers/shared, no `inverse()`, no races, `fWeightSum` cannot be zero (j=0 contributes weight 1).

Cleanup:
- line 14 — missing `writeonly`.
- line 34 — rename `fInvSigma` (actually holds `radius/sigma`) for readability, or fix the algebra to match the name.
