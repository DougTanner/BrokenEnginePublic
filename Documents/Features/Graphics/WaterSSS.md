# Water Subsurface Scattering Tint

## Context

View- and height-dependent turquoise tint on wave crests — thin water at crests lets light through, so crests read brighter/greener than troughs. Subtle from top-down but adds a depth cue and color richness the flat depth color lacks.

Salvaged from the retired ocean-shader-rewrite phase plans (was "Phase 6"), retargeted as a purely additive term in the current shader's color pipeline.

## Design

All in `Water.frag`, applied to the base color before the skybox-specular combine (so it inherits height darken, the sun/ambient shadow split, and the reflection energy split):

```glsl
// Crest thinness: 0 at/below base, 1 for high crests
float fSSSHeight = clamp((f3InPosition.z - globalLayout.fWaterSSSHeightBase) * globalLayout.fWaterSSSHeightScale, 0.0f, 1.0f);
// View-dependent: strongest looking toward the sun through a crest; f3ToEyeNormal and
// f4SunMoonNormal already exist. The +0.2 floor keeps a height-only effect from directly above.
float fSSSSun = pow(clamp(1.0f - dot(f3ToEyeNormal, globalLayout.f4SunMoonNormal.xyz), 0.0f, 1.0f), globalLayout.fWaterSSSSunPower);
float fSSS = fSSSHeight * (fSSSSun + 0.2f) * globalLayout.fWaterSSSIntensity;
f3LightingColor += fSSS * f3WaterSSSColor * f3SunOrMoon;
```

- Adding into `f3LightingColor` before the `(1.0f - fReflection)` factoring keeps total energy behavior consistent with the existing base/specular split; the sun/moon `max`-combine gives moonlit crests at night.
- The top-down camera makes the view-dependent term weak by design — the `fWaterSSSSunPower` slider (low = visible at moderate angles, high = only into-sun) plus the 0.2 height-only floor control how much survives.
- No texture fetches — pure ALU, trivially cheap.

### New uniforms / sliders

~7 new `GlobalLayout` floats: `fWaterSSSIntensity`, `fWaterSSSHeightBase`, `fWaterSSSHeightScale`, `fWaterSSSSunPower`, `fWaterSSSColorR/G/B`. Wrappers in `WaterWrappersBase.{h,cpp}`, an "SSS" slider block in `RenderWaterSection()` (`TweaksScreenWater.cpp`), uploads in `WaterUniforms.cpp`. Intensity 0 disables.

## Critical files

- `Engine/Data/Shaders/Water/Water.frag` — SSS block before the skybox combine
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — new `GlobalLayout` floats
- `Engine/Source/Ui/WaterWrappersBase.{h,cpp}` — wrappers
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp` — sliders
- `Engine/Source/Graphics/Render/WaterUniforms.cpp` — uploads

## Out of scope

- Physical scattering / transmittance model (this is an art-directed additive tint)
- Interaction with the wave-trough height darken (they compose: darken pulls troughs down, SSS lifts crests — complementary, tune together but no code coupling)
- Foam, caustics, glitter (separate plans)

## Notes

- Client-only rendering path; no determinism/CRC exposure.
- Overlaps in spirit with the existing height term inside the color-noise mix (`f3InPosition.z * fWaterColorHeightInv` in the `f3WaterColor` blend) — that term shifts between two fixed color constants, while SSS adds a tunable sun-coupled tint. If they fight during tuning, note it in Tweaks rather than pre-coupling them.
