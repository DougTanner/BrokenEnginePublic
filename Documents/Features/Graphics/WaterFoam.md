# Water Foam and Whitecaps

## Context

The water renderer (`Water.vert`/`Water.frag` + `WaterDisplacement.comp` pre-bake) has no foam. From RTS altitude, whitecaps at wave crests and shore foam are the most visible missing ocean features — they convey wind/wave energy and define coastlines.

The displacement compute pre-bake already accumulates the full Jacobian terms (`fA`, `fB`, `fD`) for the wave normal, and both RGBA16F output images store an unused `0.0f` in W — so a Jacobian-based foam factor (the physically-motivated "surface compression → breaking wave" signal) is nearly free to bake.

Salvaged from the retired ocean-shader-rewrite phase plans + `WaterFoamAndRefraction.txt`, retargeted as a pure add-on to the current shader.

## Design

### Foam factor — compute pre-bake (`WaterDisplacement.comp`)

After the two band loops, compute the horizontal-displacement Jacobian determinant from the existing accumulators:

```glsl
float fJacobian = (1.0f - fA) * (1.0f - fD) - fB * fB; // → 0 as the surface compresses toward breaking
float fFoam = clamp(1.0f - fJacobian / globalLayout.fWaterFoamJacobianStart, 0.0f, 1.0f);
```

Pack `fFoam` into the displacement image W (currently written as `0.0f` — no layout or pipeline change). The over-land early-out already stores `vec4(0.0f)`, so land texels get foam 0 for free. When zoom-out amplitude fade zeroes a band (`iWater*Count` → 0), the accumulators stay at identity → `fJacobian = 1` → foam 0, consistent.

### Vertex passthrough (`Water.vert`)

Pass the fetched displacement W through as a new varying (`layout (location = 4) out float fOutFoamFactor`). The over-land flat-vertex path outputs `0.0f`.

### Fragment composition (`Water.frag`)

1. **Shore foam**: `fShoreFoam = smoothstep` window on `-fTerrainElevation` — full near the beach, zero past `fWaterFoamShoreDepth`. Combined factor = `max(fInFoamFactor, fShoreFoam)`.
2. **Noise breakup**: sample `noiseTextureSampler` (binding 7, already bound) at a foam scale/speed. MUST follow the precision-safe sampling contract (Water/AGENTS.md): `fract()`-wrapped UV + `textureGrad` with derivatives of the camera-relative position, CPU-reduced origin/time supplied like the color-noise path in `WaterUniforms.cpp` — plain world-space UVs jitter far from origin.
3. **Blend**: foam is diffuse — mix the base color toward the foam albedo *before* the shadow-apply site (i.e. into `f3LightingColor` ahead of the `f3BaseDarkened` computation) so height darken, the sun/ambient shadow split, and `fSunScalar`/`fAmbientScalar` all apply naturally. Scale `f3SkyboxSpecular` down by the foam amount — foam is not specular.

### New uniforms / sliders

~7 new `GlobalLayout` floats (`ShaderLayoutsBase.h`): `fWaterFoamJacobianStart`, `fWaterFoamShoreDepthMin/Max`, `fWaterFoamIntensity`, `fWaterFoamNoiseScale`, `fWaterFoamNoiseSpeed`, `fWaterFoamTint`. Wrappers in `WaterWrappersBase.{h,cpp}`, a "Foam" slider block in `RenderWaterSection()` (`TweaksScreenWater.cpp`, `engine::giTweakSectionWater`), uploads + foam-noise reduced origin/time in `WaterUniforms.cpp`.

## Critical files

- `Engine/Data/Shaders/Water/WaterDisplacement.comp` — Jacobian determinant → displacement image W
- `Engine/Data/Shaders/Water/Water.vert` — new foam varying
- `Engine/Data/Shaders/Water/Water.frag` — shore foam, noise breakup, diffuse blend
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — new `GlobalLayout` floats
- `Engine/Source/Ui/WaterWrappersBase.{h,cpp}` — wrappers
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp` — sliders
- `Engine/Source/Graphics/Render/WaterUniforms.cpp` — uploads, reduced origin/time for foam noise

## Out of scope

- Dedicated foam texture / asset pipeline work (reuse `noiseTextureSampler`; revisit only if the pattern quality disappoints)
- Refraction (separate plan: `WaterRefraction.md`)
- Any change to the existing shading model (specular lobes, Fresnel, depth-LUT color)
- Foam persistence/advection simulation

## Notes

- Client-only rendering path; no determinism/CRC exposure (visual only, no sim state).
- If foam should fade with zoom-out, resolve CPU-side by camera eye height like the `fWaterNormalWeight*` uniforms (`LightingUniforms.cpp`, `engine::LerpAtHeight`) per the Camera-Height-Conditional Uniforms rule — decide at grill.
- Open decision for `/external-grill-plan`: whether crest-height foam (a `smoothstep` on `f3InPosition.z`) is wanted as a third source, or the Jacobian term alone covers crests.
