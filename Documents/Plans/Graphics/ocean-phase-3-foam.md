# Phase 3: Foam and Whitecaps

## Context

Add foam rendering driven by wave height, surface steepness (Jacobian proxy), and shoreline proximity. From kilometers up, foam is one of the most visible ocean features - whitecaps convey wind and wave energy, shoreline foam creates natural coastlines.

## What Changes in Water.frag

### Foam Factor Computation (3 sources, combined with max)

```glsl
// 1. Height-based foam (wave crests)
float fHeightFoam = smoothstep(foamHeightMin, foamHeightMax, f3InPosition.z);

// 2. Shoreline foam (shallow water)
float fShoreFoam = smoothstep(foamShoreMax, foamShoreMin, -fTerrainElevation);

// 3. Slope-based foam (Jacobian proxy - steep slopes = breaking waves)
float fSlopeFoam = smoothstep(foamSlopeThreshold, 0.0, dot(f3InNormal, vec3(0,0,1)));

float fFoamFactor = max(max(fHeightFoam, fShoreFoam), fSlopeFoam);
```

### Foam Texture and Blending

```glsl
// Sample noise as foam pattern at foam-specific scale
float fFoamPattern = texture(noiseTextureSampler,
    f2InInitialPosition * foamTexScale + globalLayout.fElapsedTime * foamScrollSpeed * vec2(0.3, 0.7)).x;
fFoamPattern = smoothstep(0.3, 0.7, fFoamPattern); // sharpen pattern

// Final foam amount
float fFoam = fFoamFactor * fFoamPattern * foamIntensity;

// Blend foam over water color (before shadow)
// Foam is diffuse white, tinted by sun
vec3 f3FoamColor = vec3(foamTint) * (globalLayout.f4SunColor.xyz + globalLayout.f4AmbientColor.xyz);
f3WaterRadiance = mix(f3WaterRadiance, f3FoamColor, clamp(fFoam, 0.0, 1.0));
```

Foam goes AFTER PBR radiance computation but BEFORE shadow multiplication. Foam receives shadows but not specular/Fresnel (it's diffuse).

## New Tweaks Sliders

Section: **"Water Foam"** (new TweakSection)

| Slider | Min | Max | Default | Purpose |
|--------|-----|-----|---------|---------|
| Foam Enable | bool | -- | true | Master toggle |
| Foam Height Min | -0.5 | 1.0 | 0.05 | Wave height where foam starts |
| Foam Height Max | 0.0 | 2.0 | 0.15 | Wave height where foam is full |
| Foam Shore Min | 0.0 | 5.0 | 0.0 | Terrain depth where shore foam starts |
| Foam Shore Max | 0.0 | 10.0 | 2.0 | Terrain depth where shore foam ends |
| Foam Slope Threshold | 0.0 | 1.0 | 0.4 | Normal-Z below which slope foam appears |
| Foam Intensity | 0.0 | 3.0 | 1.0 | Overall brightness multiplier |
| Foam Tex Scale | 0.001 | 0.1 | 0.02 | UV scale for foam noise pattern |
| Foam Scroll Speed | 0.0 | 2.0 | 0.3 | Foam texture scroll speed |
| Foam Tint | 0.5 | 1.0 | 0.9 | How white the foam is |

## Design Choices

1. **Dedicated foam texture vs reuse noiseTextureSampler**
   - *Needs user decision.*
   - **Pro dedicated texture:** More convincing foam breakup (Worley/cellular noise), easy to swap artistically
   - **Pro reuse noise:** Zero asset pipeline work, zero descriptor changes, ship faster
   - **Recommendation:** Start with noise reuse. Adding a dedicated foam texture later is a small change (one new sampler binding)

2. **Jacobian from vertex shader vs slope proxy**
   - *Resolved:* Use slope proxy (`1 - normal.z`) since vertex shader is not being modified. Correlates well with Gerstner wave steepness.

## Dependencies

Phase 1 only (foam blends into the PBR color pipeline). Independent of Phases 2, 4-7.

## Files Modified

- `Engine/Data/Shaders/Water/Water.frag` - Add foam block
- `Engine/Data/Shaders/ShaderLayoutsBase.h` - ~10 new floats
- `Engine/Source/Ui/WrapperBase.h/.cpp` - 10 new Wrapper globals
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterFoam.cpp` - New file
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` - Register sliders
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` - Upload new uniforms
