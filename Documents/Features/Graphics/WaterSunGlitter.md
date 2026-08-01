# Water Sun Glitter

## Context

Bright specular pinpoints where a noise-perturbed micro-normal aligns with the sun reflection — the characteristic sparkle field on real ocean viewed from altitude with the sun high. The current three-lobe skybox specular resolves broad highlights but has no sub-normal-map-resolution sparkle. Purely additive on top of the existing specular chain.

Salvaged from the retired ocean-shader-rewrite phase plans (was "Phase 4"), retargeted as an add-on: the original assumed a Ward BRDF that is not being built; the glitter term needs only a half-vector, which the current shader's existing direction vectors provide.

## Design

All in `Water.frag`, after the specular-lobe block:

```glsl
// Micro-normal jitter from a high-frequency noise fetch (precision-safe UV contract)
vec3 f3GlitterNormal = normalize(f3SkyboxWaveNormal + vec3(f2GlitterNoise * globalLayout.fWaterGlitterNoiseStrength, 0.0f));
// Half-vector from existing directions
vec3 f3Half = normalize(f3BiasedSunNormal + f3ToEyeNormal);
float fGlitter = smoothstep(globalLayout.fWaterGlitterThreshold, 1.0f, dot(f3GlitterNormal, f3Half)) * globalLayout.fWaterGlitterIntensity;
f3SkyboxSpecular += fGlitter * f3SunOrMoon;
```

- Adding into `f3SkyboxSpecular` means glitter inherits `fHeightDarkenLighting`, the full `fEffectiveShadow` (shadow kills sparkle, correct), and the sun/moon `max`-combine (`f3SunOrMoon`) so moon glitter appears at night.
- Noise fetch reuses `noiseTextureSampler` (binding 7) at high-frequency UV; MUST follow the precision-safe sampling contract (`fract()`-wrapped UV + `textureGrad`, CPU-reduced origin — see Water/AGENTS.md). World-anchored UV makes sparkles ride the surface and twinkle as waves move.
- `fGlitter = 0` when intensity slider is 0 — no separate enable toggle needed.

### New uniforms / sliders

~4 new `GlobalLayout` floats: `fWaterGlitterThreshold` (0.9–0.999), `fWaterGlitterIntensity`, `fWaterGlitterNoiseScale`, `fWaterGlitterNoiseStrength`. Wrappers in `WaterWrappersBase.{h,cpp}`, sliders in `RenderWaterSection()`, uploads in `WaterUniforms.cpp`.

## Critical files

- `Engine/Data/Shaders/Water/Water.frag` — glitter block after the specular lobes
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — new `GlobalLayout` floats
- `Engine/Source/Ui/WaterWrappersBase.{h,cpp}` — wrappers
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp` — sliders
- `Engine/Source/Graphics/Render/WaterUniforms.cpp` — uploads + glitter-noise reduced origin

## Out of scope

- Bloom / HDR post-processing (glitter benefits from it but does not require it; see `HdrResolveAndColorGrading.txt`)
- Changes to the existing specular lobes or `WATER_SPEC_AA_MODE` filtering
- Dedicated sparkle texture

## Notes

- Client-only rendering path; no determinism/CRC exposure.
- Aliasing: sparkle is intentionally high-frequency and the analytic spec-AA filters do not cover this new term. The threshold slider is the flicker control (higher = sparser/steadier). If pointwise evaluation still flickers objectionably, widen the smoothstep by the same screen-space variance kernel `WATER_SPEC_AA_MODE 2` already computes (`f3NormalDx`/`f3NormalDy` are available) — pre-staged decision for `/external-grill-plan`.
