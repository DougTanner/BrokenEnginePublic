# Shader Review — Lighting Performance

Successor to `06_Lighting.md` (executed and deleted). That review's correctness/consistency items were either already mitigated by existing slider clamps or landed in the session that deleted `06_Lighting.md` (ring-count slider min raised to 2 to guard the `LightingSpread.frag` `fNorm` divide; `scalar` qualifier added to `lightOccupancyBuffer` in `AreaLight.frag`/`PointLight.frag`). The **performance** items were explicitly deferred here for a separate, profiling-guided pass.

## Context

The lighting spread pipeline runs Deposit (MRT fragment) → Spread×N (MRT fragment, N from `gSpreadPassCount`, up to 40) → Combine (compute). The spread fragment shader is the per-frame hot path: its inner work scales as `uiRingCount * uiDirectionCount` per fragment per pass. The items below were flagged by `/glsl-review` as performance opportunities but are unverified by profiling — measure before and after each.

## Design

Performance items, roughly highest-value first:

1. **`LightingSpread.frag` per-fragment `cos`/`sin` hot path** — the `vec2 f2Direction = vec2(cos(fAngle), sin(fAngle));` inside the nested `uiRingCount * uiDirectionCount` loop (currently `Engine/Data/Shaders/Lighting/LightingSpread.frag:101-102`) is the dominant per-fragment cost. Options:
   - Incremental angle addition (rotation recurrence: advance `f2Direction` by a precomputed per-step `(cos dθ, sin dθ)` rotation each iteration). Cheapest, but watch for accumulated drift across many directions — re-seed per ring.
   - Precompute the per-(ring,direction) unit directions into an SSBO on the CPU and index it. Avoids trig entirely; costs bandwidth + a CPU upload when spread params change.
   - Decide between these in the grill — it's a data-layout choice (`Resolving Ambiguity` → architectural).
   - Note: the per-sample x/y jitter at `:111-113` already calls `cos`/`sin` on a random angle; that is independent of the direction-loop trig and is a separate (smaller) cost.

2. **`LightingSpread.frag` duplicate Reinhard compressor** — the pass-0 inline compressor (`:122-137`) and the per-pass output compressor (`:154-164`) share an identical hue-preserving Reinhard body differing only in threshold/compress inputs. Factor into a local helper taking the three `vec4` channel accumulators plus `fThreshold`/`fCompress`. Pure dedup; no behavior change.

3. **`LightCombine.comp` skip the discarded Uchimura path** — when `fHuePreserve >= 0.999f`, the `mix(f4OutR, f4HueR, fHuePreserve)` at `:135-137` returns essentially the hue-preserving result, so the three `Uchimura(...)` calls at `:117-119` are wasted. Guard them behind `if (fHuePreserve < 0.999f)` (mirror of the existing `< 0.001f` fast path at `:109`).

4. **`LightingBlurH.comp` hoist reciprocal** — `vec2(...) / vec2(float(iWidth), float(iHeight))` is recomputed every loop iteration at `:36`. Hoist `vec2 vInvDim = 1.0 / vec2(float(iWidth), float(iHeight));` above the loop and multiply. (Check `LightingBlurV.comp` for the same idiom at its UV line.)

5. **`AreaLight.frag` sample-after-discard reorder** — the bindless texture sample at `:49` happens before the alpha discard at `:83-84`. When the per-quad `f4Color.a` (from `uiColor`, `:52`) is frequently 0, the sample is wasted. Reorder: unpack `f4Color`, test `f4Color.a` and discard early, then sample. Note this only helps if per-quad alpha is commonly 0 in practice — verify with content before committing. (`PointLight.frag` has the same shape if it's worth mirroring.)

## Critical files

- `Engine/Data/Shaders/Lighting/LightingSpread.frag` — items 1, 2
- `Engine/Data/Shaders/Lighting/LightCombine.comp` — item 3
- `Engine/Data/Shaders/Lighting/LightingBlurH.comp` (and `LightingBlurV.comp`) — item 4
- `Engine/Data/Shaders/Lighting/AreaLight.frag` (and possibly `PointLight.frag`) — item 5
- If item 1 chooses the SSBO route: a CPU upload site in `Engine/Source/Graphics/Render/LightingUniforms.cpp` (or a dedicated buffer) + `ShaderLayoutsBase.h` for the layout struct.

## Out of scope

- All correctness / NaN-Inf guards from `06_Lighting.md` — resolved (slider clamps already in place, or landed). Do not re-add shader guards for hazards the slider mins already prevent; see `Engine/Source/Ui/LightingWrappersBase.cpp` load-bearing comments.
- `AreaLight.frag:84` `discard` disabling early-Z — inherent to the deposit logic; not addressed.
- The `if (fDist > 1e-4f)` branch in `AreaLight.frag:60` — the `else` is an intentional omnidirectional epsilon fallback near the light center (see `Engine/Data/Shaders/CLAUDE.md`), not a divide guard; do **not** "branchless" it.
- The inverted/misnamed Gaussian sigma in `LightingBlurH/V.comp` (`fInvSigma = radius/sigma` makes the blur narrower at larger sigma) — left as-is: the blur is tuned against current behavior and `gLightingBlurSigma` (`0.01–4.0`) is calibrated to it. Changing it would invert the slider's effect and require re-tuning. Revisit only as a deliberate retune, not a perf pass.
- `LightCombine.comp:127` Rec.709 luminance computed from EWNS directional weights (not physical RGB) — a correctness/clarity observation, not perf; add a clarifying comment if revisited, but it is intentional given the EWNS storage model.

## Acceptance criteria

- Each landed item is profiled before/after (GPU timestamp on the spread/combine/blur passes) and shows a non-regression or improvement; visual output is unchanged (compare a frame capture).
- Item 1's chosen approach (incremental vs SSBO) is recorded with its rationale.
- No new shader validation warnings; SPIR-V recompiles clean via DataPacker.

## Notes

- These are micro-to-medium GPU opts with unverified magnitudes; the `cos`/`sin` claim ("dominant cost") came from static review, not a profile. If item 1 profiles as negligible, drop it and keep only the trivially-safe items (2, 3, 4).
- Items 2, 3, 4 are behavior-neutral and low-risk; item 1 (SSBO route) and item 5 carry the design/verification burden.
