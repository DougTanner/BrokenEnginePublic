<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-03T16:27:16.589Z","dependsOn":["Documents/Plans/Graphics/WaterShoalingAmplitude.md"]} -->
# Depth-driven animated shoreline foam bands in Water.frag

## Context

The water renderer has no foam anywhere. From the RTS camera kilometers up, white foam bands crawling toward the beach are the single strongest visual cue that defines a coastline. This plan adds them as a pure fragment-shader effect in `Water.frag`, using only signals that are already free per pixel: `-fTerrainElevation` (water depth in the artist-curved shore coordinate, computed at line 67 from `elevationTextureSampler`), the already-bound `noiseTextureSampler` (binding 7), the `WaterShoalingAmplitude.md` breaker uniform `fWaterBreakDepthInv`, and the Jacobian determinant that plan bakes into the displacement normal image `.w`. Zero new bindings, textures, or render targets.

The technique is the standard depth-as-shore-coordinate foam gradient (Cyanilux / Alisavakis shoreline-foam breakdowns: bands are periodic in a depth-derived coordinate, scrolled by time, cut by noise). Bands hug island contours automatically because iso-depth lines follow the coast. The known limitation — depth gradient direction diverging from the true shore direction at cliffs or in narrow bays — is accepted here; `WaterShoreDistanceField.md` is the contingency upgrade and states its trigger.

`Documents/Features/Graphics/WaterFoam.md` (unimplemented manual note) covers general whitecap/foam composition; this plan is deliberately narrower — shoreline bands only, deep-water whitecaps stay out — and does not modify that note.

Risk tier: Tier 2 scoped rendering behavior — client-only visuals, no simulation/serialization/wire/CRC exposure. Declared trigger for Step 1 review: adds fields to `ShaderLayoutsBase.h`'s shared CPU/GLSL `GlobalLayout` (per-frame uniform, never persisted or CRC'd); a reviewer may escalate under data-layout exposure.

## Scope contract

The listed scope is both target and ceiling: smallest complete change satisfying the acceptance criteria plus the mechanical necessities the named regions require. No new textures, no foam persistence/advection, no lighting-model changes.

## In scope

Only these regions:

- `Engine/Data/Shaders/Water/Water.frag`: a new foam block inserted after the `f3PreLightingColor` mix (line 113) and before `fDirectionalLighting` (line 115), per Design; a new input varying `layout (location = 4) in float fInFoamJacobian;`.
- `Engine/Data/Shaders/Water/Water.vert`: extend the `displacementNormalTextureSampler` fetch (line 61) to `.xyzw`, pass `.w` out as `layout (location = 4) out float fOutFoamJacobian;`; the over-land early-out path (lines 47-54) outputs `0.0f` for it. Nothing else in the vertex shader changes.
- `Engine/Data/Shaders/ShaderLayoutsBase.h`: add `GlobalLayout` floats `fWaterFoamDepthRangeInv`, `fWaterFoamBandCount`, `fWaterFoamPhase`, `fWaterFoamIntensity`, `fWaterFoamNoiseCutoff`, `fWaterFoamNoiseMultiplier`, `fWaterFoamJacobianWeight`, with comments naming the CPU-folded terms.
- `Engine/Source/Ui/WaterWrappersBase.h`/`.cpp`: new wrappers `gWaterFoamDepthRange`, `gWaterFoamBandCount` (integer-snapped, `Wrapper` step `1.0f`), `gWaterFoamSpeed`, `gWaterFoamIntensity`, `gWaterFoamNoiseCutoff`, `gWaterFoamNoiseMultiplier` (0.1-grid snapped, like `gWaterColorNoiseMultiplierOne`), `gWaterFoamJacobianWeight`.
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp`: a `Foam` slider block in the `Low` tab after the Shoaling block (registrar entries `Foam Depth Range`, `Foam Band Count`, `Foam Speed`, `Foam Intensity`, `Foam Noise Cutoff`, `Foam Noise Multiplier`, `Foam Jacobian Weight`), order-aligned with the registrar.
- `Engine/Source/Graphics/Render/WaterUniforms.cpp`: uploads in `PopulateWaterParameters` (fold `1.0f / gWaterFoamDepthRange.Get()` CPU-side) and a foam scroll-phase accumulator in `PopulateWaterReducedUv` (lines 143-174 region): integrate `gWaterFoamSpeed.Get() * dt` in `double`, reduce with `std::fmod(phase, 2.0)`, publish as `fWaterFoamPhase` — same per-frame single-call latch pattern as the existing reduced-time accumulators.

## Out of scope

- Deep-water whitecaps, crest foam away from shore, foam persistence/advection, and any dedicated foam texture or asset work (`noiseTextureSampler` only).
- Any change to `WaterDisplacement.comp`, the Gerstner bands, specular/reflection/Fresnel composition, the depth LUT, shadows, smoke, or the EWNS lighting paths — foam enters only through `f3PreLightingColor`, so existing lighting applies to it unmodified; foam deliberately does not suppress specular in this prototype.
- New descriptor bindings, render targets, or pipeline changes; shore-direction fields (`WaterShoreDistanceField.md`); `Documents/Features/Graphics/WaterFoam.md` (read-only); all simulation/CRC-relevant code.

## Design

Foam block in `Water.frag` (inserted after line 113):

```glsl
// Shore coordinate: 0 at the waterline, 1 at the seaward foam edge (artist-curved depth, fine for visuals).
float fShoreT = clamp(-fTerrainElevation * globalLayout.fWaterFoamDepthRangeInv, 0.0f, 1.0f);
// Breaker mask from WaterShoalingAmplitude.md's uniform: 1 at the shoreline, 0 seaward of break depth.
float fBreakMask = 1.0f - clamp(-fTerrainElevation * globalLayout.fWaterBreakDepthInv, 0.0f, 1.0f);
// Bands periodic in sqrt(fShoreT): wavelength shortens toward shore (bands crowd the beach);
// +phase scrolls crests shoreward.
float fBand = max(sin((sqrt(fShoreT) + globalLayout.fWaterFoamPhase) * kPi * globalLayout.fWaterFoamBandCount), 0.0f);
float fFoamNoise = <noise sample, precision-safe — see below>; // BC4 UNORM: already [0, 1], no remap
// Noise threshold breaks the bands into ragged chunks.
float fRagged = step(globalLayout.fWaterFoamNoiseCutoff, fFoamNoise * fBand);
// Jacobian crest boost: determinant → 0 where the surface compresses toward breaking (from Water.vert varying).
float fCrest = globalLayout.fWaterFoamJacobianWeight * clamp(1.0f - fInFoamJacobian, 0.0f, 1.0f);
float fFoam = clamp(globalLayout.fWaterFoamIntensity * fBreakMask * (fRagged * fBand + fCrest), 0.0f, 1.0f);
f3PreLightingColor = mix(f3PreLightingColor, vec3(0.9f), fFoam);
```

Blending before the lighting section means directional lighting, height darken, the sun/ambient shadow split, and smoke all apply to foam naturally with zero extra work. `fBreakMask` confines everything to the breaker zone, so the deep-water term of the crest boost is automatically zero (this is why deep whitecaps stay out of scope). `Foam Depth Range` is expected to be tuned a few times larger than `Break Depth` so bands form seaward and die at the beach.

Noise sample: follow the existing color-noise precision contract exactly (lines 90-107) — `fScaleFoam = fWaterFoamNoiseMultiplier * fWaterColorNoiseFrequency`, UV `= fScaleFoam * f2LocalDisplacedPos + fWaterFoamNoiseMultiplier * f2ReducedNoiseOrigin`, sampled with `textureGrad(noiseTextureSampler, fract(uv), fScaleFoam * f2NoiseLocalDx, fScaleFoam * f2NoiseLocalDy).x`, reusing the already-computed derivative pair. The 0.1-grid wrapper snap preserves the integer-product modulus-10 contract, so no new reduced origin is needed. The bound noise texture is `Textures\Water\[BC4]Noise.png` (`PipelineManager.cpp` water pipeline, `VK_FORMAT_BC4_UNORM_BLOCK`), so the sample is unsigned `[0, 1]` already: use it directly, with no `0.5 + 0.5 *` remap — line 47 above reads `float fFoamNoise = <the textureGrad sample>.x;`.

`fWaterFoamPhase` scrolls the band pattern; since the band function is periodic with period `2 / fWaterFoamBandCount` in the `sqrt` coordinate, `fmod(phase, 2.0)` keeps the reduced value exact only when `fWaterFoamBandCount` is an integer — at a fractional count the pattern jumps visibly each time the phase wraps. `gWaterFoamBandCount` is therefore integer-snapped (`Wrapper` step `1.0f`), which is the whole reason that slider carries a step at all. `kPi` already exists in the shader includes (use the established constant; add a local `const` only if none exists).

Slider defaults (starting points, tuned live): `gWaterFoamDepthRange(0.2f, 0.02f, 1.0f)`, `gWaterFoamBandCount(4.0f, 1.0f, 12.0f, 1.0f)` (integer snap, per the phase-reduction note above), `gWaterFoamSpeed(0.05f, 0.0f, 0.5f)`, `gWaterFoamIntensity(0.8f, 0.0f, 1.0f)`, `gWaterFoamNoiseCutoff(0.35f, 0.0f, 1.0f)`, `gWaterFoamNoiseMultiplier(0.5f, 0.0f, 2.0f)` (0.1 snap), `gWaterFoamJacobianWeight(0.3f, 0.0f, 1.0f)`.

Implementation order: layout fields → wrappers/uploads/phase accumulator → sliders → `Water.vert` varying → `Water.frag` foam block → repack + build → harness screenshots.

## Critical files

- `Engine/Data/Shaders/Water/Water.frag` — foam block and varying input.
- `Engine/Data/Shaders/Water/Water.vert` — Jacobian varying passthrough (over-land path outputs 0).
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — new `GlobalLayout` floats, CPU/GLSL kept in sync.
- `Engine/Source/Ui/WaterWrappersBase.h`/`.cpp`, `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp` — tunables and UI, order-aligned.
- `Engine/Source/Graphics/Render/WaterUniforms.cpp` — uploads and the reduced foam-phase accumulator.
- `Engine/Data/Shaders/Water/WaterDisplacement.comp` — read-only verification site: `.w` of the normal image carries the determinant this plan consumes.

## Risk triggers and invariants

- CRC safety: all changed files are client-render-only; no simulation reads or writes, so the per-tick CRC is unaffected by construction.
- Water precision contracts (`Engine/Data/Shaders/Water/AGENTS.md`): noise sampling must use `fract`-wrapped UVs, `textureGrad` with pre-scale derivatives, and the CPU-reduced origin with an integer-product multiplier — plain world-space UVs jitter far from origin.
- Reduced-time latch contract (`Engine/Source/Graphics/Render/AGENTS.md`): the foam-phase accumulator integrates per call and assumes exactly one `PopulateWaterReducedUv` call per frame; do not add a second call site.
- Do not add a second flat-normal fade or touch the cross-product normal path; foam is color-only.
- `ShaderLayoutsBase.h` field types/alignment/order stay CPU/GLSL identical for both builds; folded reciprocals stay CPU-side.

## Acceptance criteria

- `/compile` builds the client Debug|x64 after DataPacker shader repack; the TweaksScreen registration audit reports no missing/orphaned water sliders.
- `/agent-harness` at gameplay altitude over an island: a screenshot shows near-white ragged foam bands hugging the island's shoreline contour, absent from open ocean; two screenshots a few seconds apart show the band pattern displaced toward the shore (scrolling); setting `Foam Intensity` to 0 reproduces the pre-change frame (foam fully off); raising `Foam Band Count` visibly increases the number of concentric bands.
- Bands remain stable (no shimmering/jitter) in a screenshot taken far from the world origin, confirming the precision-safe noise path.
- Zero new descriptor bindings or render targets in the diff.

## Notes

- References, one line each: Cyanilux and Alisavakis depth-gradient shoreline-foam shader breakdowns (band-in-depth-coordinate + noise cut); Green's-law breaker collapse supplies the `fBreakMask` confinement via the prerequisite plan.
- If bands misbehave at cliffs or in narrow bays, do not patch here — that is the recorded trigger for `WaterShoreDistanceField.md`.
