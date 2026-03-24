# Phase 4: Sun Glitter

## Context

Add sun glitter: bright specular pinpoints where the perturbed micro-normal aligns with the sun reflection vector. From directly above with the sun also above, this is very impactful - the ocean should sparkle. Works well with bloom post-processing.

## What Changes in Water.frag

### Glitter Pass (after Ward BRDF specular)

```glsl
// High-frequency normal jitter for micro-sparkles
vec2 f2GlitterUV = f2InInitialPosition * glitterNoiseScale;
vec3 f3GlitterNoise = texture(noiseTextureSampler, f2GlitterUV).xyz * 2.0 - 1.0;
vec3 f3GlitterNormal = normalize(f3SampledNormal + vec3(f3GlitterNoise.xy * glitterNoiseStrength, 0.0));

// Compute glitter from NdotH threshold
vec3 f3H = normalize(f3SunDir + f3ViewDir);
float fNdotH = dot(f3GlitterNormal, f3H);
float fGlitter = smoothstep(glitterThreshold, 1.0, fNdotH) * glitterIntensity;

// Add to specular (before shadow so shadows suppress glitter)
f3SunSpecular += fGlitter * globalLayout.f4SunColor.xyz;
```

The noise texture provides sub-pixel normal perturbation that the normal map octaves are too large to resolve, creating the characteristic sparkle field.

## New Tweaks Sliders

Section: **"Water Glitter"** (new TweakSection)

| Slider | Min | Max | Default | Purpose |
|--------|-----|-----|---------|---------|
| Glitter Enable | bool | -- | true | Master toggle |
| Glitter Threshold | 0.9 | 0.999 | 0.97 | NdotH threshold (higher = sparser sparkles) |
| Glitter Intensity | 0.0 | 10.0 | 3.0 | Brightness of glitter points |
| Glitter Noise Scale | 0.1 | 10.0 | 2.0 | UV scale for micro-normal jitter |
| Glitter Noise Strength | 0.0 | 0.5 | 0.1 | How much noise perturbs the normal |

## Design Choices

All resolved - no user decisions needed:
- World-space noise (stable on surface, twinkles as waves move - physically correct)
- Uses existing `noiseTextureSampler` at high-frequency UV

## Dependencies

Phase 1 (uses Ward BRDF half-vector math). Benefits from Phase 2 (better normals) but works without it.

## Files Modified

- `Engine/Data/Shaders/Water/Water.frag` - Add glitter block
- `Engine/Data/Shaders/ShaderLayoutsBase.h` - ~5 new floats
- `Engine/Source/Ui/WrapperBase.h/.cpp` - 5 new Wrapper globals
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterGlitter.cpp` - New file
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` - Register sliders
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` - Upload new uniforms
