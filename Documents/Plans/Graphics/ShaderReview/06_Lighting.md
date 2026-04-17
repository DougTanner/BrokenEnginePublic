# Shader Review — Lighting

Files: `VisibleLight.vert`, `VisibleLight.frag`, `AreaLight.frag`, `PointLight.frag`, `LightingSpread.frag`, `LightingBlurH.comp`, `LightingBlurV.comp`, `LightCombine.comp`

## PASS

- `Engine/Data/Shaders/Lighting/VisibleLight.vert` — scalar layout inherited, correct sets, `flat` on integer varying, uses `Transform`.

## Minor notes

### `Engine/Data/Shaders/Lighting/VisibleLight.frag`

- line 40 — `(fTerrainElevation - fBaseHeight) / fFalloff` with `fFalloff = 0.25 * fBaseHeight`; if `fBaseHeight` is ever 0 produces NaN/Inf. Guard with `max(fFalloff, 1e-6)`.

## NEEDS FIXES

### `Engine/Data/Shaders/Lighting/AreaLight.frag`

Broken Engine:
- line 25 — `buffer lightOccupancyBuffer` missing `layout(scalar, ...)` qualifier; inconsistent with line 20 and repo convention.

Correctness:
- line 47 — `int32_t(f4InParams.x + 0.4f)` — `+0.4` rounding bias with `int()` (truncates toward zero) mis-rounds values near integer boundaries. Pass as `flat int` varying, or use `+ 0.5f`.

Performance:
- line 82 — `discard` disables early-Z for the draw.
- line 47 — bindless texture sample before alpha-discard; if `f4Color.a` is often 0 from `uiColor`, sample is wasted. Test `f4Color.a` first, discard, then sample.
- line 58 — divergent `if (fDist > 1e-4f)` can be branchless: `f2NormDir = f2WorldDir / max(fDist, 1e-4f)`.

### `Engine/Data/Shaders/Lighting/PointLight.frag`

Broken Engine:
- line 25 — `lightOccupancyBuffer` missing explicit `scalar` qualifier; relies on global default.

Correctness:
- line 48 — `int32_t(f4InParams.x + 0.4f)` — use `+ 0.5f` for round-to-nearest, or pass int attribute directly.

### `Engine/Data/Shaders/Lighting/LightingSpread.frag`

Correctness:
- lines 132-135 — `fNorm = fTotalSamples * fInvSqrtDistance * mix(...)` has no zero-guard. If `uiRingCount == 0` or `fDistanceFalloff >= 1` at the outer ring → `fTotalSamples == 0`; three divides produce Inf/NaN that poisons every subsequent pass.
- line 84 — `fRingFalloff = 1 - fDistanceFalloff * ((j+1)/uiRingCount)` goes negative when `fDistanceFalloff > 1`, producing negative accumulated weights. Clamp with `max(..., 0.0)`.
- line 59 — `pow(fHeightT, fSpreadHeightPower)` at `fHeightT == 0` with negative power yields Inf. Clamp the power ≥ 0 CPU-side or guard.
- line 154 — `pfCombineCurvePoints[uint(fPassIndex)]` has no bounds check. Cheap `min(..., kiMaxSpreadPasses - 1)` insurance.

Performance:
- lines 91-94 — per-fragment `cos`/`sin` inside the nested `uiRingCount * uiDirectionCount` loop is the dominant cost. Precompute directions into an SSBO or use incremental angle addition.
- lines 114-123 / 142-151 — duplicate Reinhard-compressor body; factor into a local helper.
- lines 105-107 — three `texture()` calls per sample (main bandwidth hot spot).

### `Engine/Data/Shaders/Lighting/LightingBlurH.comp`

Correctness:
- line 28 — `fInvSigma = iRadius / fSigma`; at `iRadius == 0` → `0.0 / 0.0 = NaN`, written to image. Guard or passthrough.
- line 19 — `fSigma == 0` produces Inf → `fA = 0` → becomes silent box filter.
- line 37 — `texture(...)` in compute uses implicit LOD (undefined); use `textureLod(..., 0.0)`.

Performance:
- line 12 — image missing `writeonly` qualifier.
- line 36 — per-iteration `/ vec2(iWidth, iHeight)` — hoist reciprocal outside loop.

Vulkan/API:
- lines 12-13 — no explicit `layout(set = N, ...)`; defaults to set 0 (per-pipeline resources should be set 1). Verify pipeline layout.

### `Engine/Data/Shaders/Lighting/LightingBlurV.comp`

Correctness:
- line 30 — `fInvSigma = float(iRadius) / fSigma` is misnamed and inverts standard Gaussian: `fA = j / fInvSigma = j*sigma/radius` makes the Gaussian *wider* at smaller radii. Likely should be `fA = j / fSigma` (with the standard `weight = exp(-0.5*(j/sigma)^2)` formula).
- line 28 — packing `(int count, fract falloff)` into one float is fragile above ~2^23; split into two push-constant slots.
- line 30 — clamp `max(fSigma, 1e-6)`.
- line 45 — `pow(1.0 - fEdgeDist, fEdgeFalloff)` — at `fEdgeDist == 1` and `fEdgeFalloff == 0` is `pow(0, 0)` implementation-defined.

Performance/Vulkan:
- line 13 — missing `writeonly`.
- lines 12-13 — verify descriptor set.

### `Engine/Data/Shaders/Lighting/LightCombine.comp`

Correctness:
- lines 78, 79, 90 — `fPassCount` used as divisor/`pow` base without guard. If runtime slider can set it to 0, NaN/Inf propagates to output.
- lines 39, 58 — `pow(f4X / m, vec4(c))`: if `m == 0` → Inf; guard `m` CPU-side or `max(m, 1e-6)`.
- line 121 — hue-preservation computes Rec.709 luminance from EWNS directional weights, which aren't physical R/G/B radiance. Sanity-check the mapping; add a comment if deliberate.

Performance:
- lines 111-127 — `f4OutR/G/B` always computed even when `fHuePreserve == 1.0`.
