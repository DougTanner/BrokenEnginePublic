# Phase 1: PBR Foundation (Ward BRDF + Beer-Lambert Depth Color)

## Context

Replace the ad-hoc shading in Water.frag with a physically-based core: Beer-Lambert absorption for depth coloring, Schlick Fresnel (F0=0.02), and Ward BRDF for sun specular. This is the largest single change and establishes the shading model all subsequent phases build on.

## What Gets Removed

- Noise-based color mixing (lines 75-77): the `fNoiseColorOne/Two` dual-noise approach with hardcoded color constants
- Depth LUT sampling (line 79): `depthLutSampler` lookup via `fWaterDepthLutFeather`
- Depth color feather blend (line 82): the `fWaterDepthColorFeather` + `fWaterDepthLutSunsetFade` mix
- Ad-hoc directional lighting (lines 84-89): the `fWaterDirectional` approach and double sunlight multiply
- Multi-term Specular() call (line 101): replaced by Ward BRDF
- The `Fresnel()` function (lines 37-47): replaced by inline Schlick with proper F0=0.02
- Skybox additive pass (lines 103-105): replaced by Ward BRDF specular + sky irradiance
- Double sunlight multiply (line 109): the second `*= fSunlight` that over-darkens

## What Gets Added

### Beer-Lambert Absorption
```glsl
vec3 BeerLambert(float fDepth, vec3 f3Extinction) {
    return exp(-f3Extinction * fDepth);
}
```
Replaces LUT + noise color. `fDepth` = `-fTerrainElevation * fDepthScale`. Extinction coefficients are tweakable (default: tropical water R=4.5, G=0.5, B=0.05).

### Ward BRDF with Schlick Fresnel
```glsl
float WardBRDF(vec3 f3View, vec3 f3Normal, vec3 f3Light, float fInvSlopeVar) {
    vec3 f3H = normalize(f3Light + f3View);
    float fHdotN = dot(f3H, f3Normal);
    float c = 1.0 - dot(f3View, f3H);
    float fFresnel = 0.02 + 0.98 * c * c * c * c * c;
    float p0 = fFresnel * (1.0 / (4.0 * PI)) * fInvSlopeVar;
    return exp((-2.0 * fInvSlopeVar) * (1.0 - fHdotN)) * p0;
}
```
Replaces the multi-term `Specular()` call. In Phase 1, `fInvSlopeVar = 1.0 / (roughness * roughness)` from a constant slider. Phase 2 upgrades this with computed slope variance.

### Mean Fresnel
```glsl
float MeanFresnel(float fMu, float fSlopeVariance) {
    float fSigma = sqrt(fSlopeVariance);
    return pow(fMu, 5.0 * exp(-2.69 * fSigma)) / (1.0 + 22.7 * fSlopeVariance * sqrt(fSigma));
}
```
Used for sky irradiance contribution: distant water where individual normals are unresolved.

### Final Radiance Composition
```glsl
float fNdotV = max(dot(f3ViewDir, f3Normal), 0.001);
float fSunSpec = WardBRDF(f3ViewDir, f3Normal, f3SunDir, fInvSlopeVar);
float fSkyFresnel = MeanFresnel(fNdotV, fSlopeVar);
vec3 f3SeaColor = (1.0 - fSkyFresnel) * f3SeaFloorAlbedo;
vec3 f3Radiance = fSunSpec * f3SunRadiance + (vec3(fSkyFresnel) + f3SeaColor) * f3SkyIrradiance / PI;
```
Where `f3SeaFloorAlbedo` = `BeerLambert(depth, extinction)` blended with the floor albedo constant.

## What Gets Preserved (unchanged)

- Terrain elevation early-out (lines 53-58)
- Normal map sampling (lines 64-72) - kept as-is, restructured later in Phase 2
- Shadow computation (line 112): SmokeShadow, shadow textures, object shadows
- Water transparency alpha (line 117)
- Smoke at base height (lines 120-123)
- Lighting at base height (lines 126-136): ReadLighting, SpecularLighting
- BlendSmoke (line 139)

## Shader Code Structure (After Phase 1)

```
1. Terrain elevation early-out
2. View direction computation
3. Sample normal maps (existing 6-octave, unchanged)
4. Beer-Lambert depth color from terrain elevation
5. Ward BRDF sun specular
6. Mean Fresnel sky irradiance
7. Compose base radiance = sun spec + sky + sea color
8. Height darken (existing, preserved)
9. Shadow (existing, preserved)
10. Ambient floor (simplified from existing)
11. Water alpha (existing, preserved)
12. Smoke at base height (existing, preserved)
13. Lighting at base height (existing, preserved)
14. BlendSmoke (existing, preserved)
```

## New Tweaks Sliders

Section: **"Water PBR"** (new TweakSection enum value)

| Slider | Min | Max | Default | Purpose |
|--------|-----|-----|---------|---------|
| Extinction R | 0.0 | 20.0 | 4.5 | Red extinction (absorbed fastest) |
| Extinction G | 0.0 | 10.0 | 0.5 | Green extinction |
| Extinction B | 0.0 | 5.0 | 0.05 | Blue extinction (absorbed slowest) |
| Floor Albedo R | 0.0 | 0.1 | 0.004 | Deep water red |
| Floor Albedo G | 0.0 | 0.1 | 0.016 | Deep water green |
| Floor Albedo B | 0.0 | 0.2 | 0.047 | Deep water blue |
| Fresnel F0 | 0.01 | 0.1 | 0.02 | Base reflectance at normal incidence |
| Roughness | 0.01 | 1.0 | 0.15 | Ward BRDF roughness (Phase 2 overrides with slope variance) |
| Depth Scale | 0.1 | 10.0 | 1.0 | Multiplier on terrain depth |
| Scatter R | 0.0 | 0.3 | 0.05 | In-scatter light, red |
| Scatter G | 0.0 | 0.3 | 0.15 | In-scatter light, green |
| Scatter B | 0.0 | 0.3 | 0.20 | In-scatter light, blue |

## Obsolete Sliders (Remove or Repurpose)

From existing Water Specular section:
- Skybox 1/1Power/2/2Power/3/3Power -> replaced by Ward Roughness

From existing GlobalLayout water params:
- fWaterFresnel -> replaced by Fresnel F0
- fWaterDepthLutFeather / fWaterDepthColorFeather -> replaced by extinction coefficients
- fWaterColorNoiseFrequency / fWaterColorNoiseAmount / fWaterColorBottom / fWaterColorHeightInv -> replaced by Beer-Lambert
- fWaterDirectional -> removed (Ward BRDF handles directional naturally)

Sliders that REMAIN:
- Sampled Normals Size/SizeMod/Speed (still control normal map sampling)
- Sun Bias, Normal Soften, Normal Blend Wave, Skybox Lod (skybox still used for sky irradiance)
- Height Darken Top/Bottom/Clamp (preserved)
- All Water Lighting section (point/area light specular on water)
- Depth Reflection Feather (reflection suppression near shore)

## Design Choices

1. **Slope variance (Phase 2) vs constant roughness (Phase 1)**: Start with constant `Roughness` slider. Phase 2 adds computed slope variance that overrides it via a toggle. No user decision needed.

2. **Keep depthLutSampler binding**: Keep the descriptor binding, just stop sampling. Zero-cost, avoids pipeline layout changes. Clean up later if desired.

3. **3 separate extinction sliders vs single absorption strength**: Use 3 separate sliders. Defaults encode the physically-correct ratio. Artists can tune. Extra sliders cost nothing.

## Files Modified

- `Engine/Data/Shaders/Water/Water.frag` - Major rewrite of main()
- `Engine/Data/Shaders/ShaderLayoutsBase.h` - Add ~12 new floats to GlobalLayout
- `Engine/Source/Ui/WrapperBase.h` - 12 new Wrapper globals
- `Engine/Source/Ui/WrapperBase.cpp` - 12 new Wrapper definitions
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` - Register 12 sliders
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.h` - Add kWaterPBR to TweakSection enum + RenderWaterPBRSection()
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp` - Register new section in toggle bar
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterPBR.cpp` - New file
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` - Upload new uniforms
- `.vcxproj` / `.vcxproj.filters` - Register TweaksScreenWaterPBR.cpp
