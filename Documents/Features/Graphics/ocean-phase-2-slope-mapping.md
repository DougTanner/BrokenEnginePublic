# Phase 2: Multi-Octave Slope Mapping with Anti-Tiling

## Context

Replace the current 6-octave normal sampling with a structured 4-octave slope cascade at WoWs scales (1077m, 154m, 22m, 3.1m). Add slope variance computation for distance-dependent roughness. Add anti-tiling via noise-based UV offset. This is the second highest impact technique for top-down viewing.

## What Changes in Water.frag

### Replace Normal Sampling (lines 64-72)

Replace the 6 `SampleNormal()` calls with 4 slope texture samples at cascade scales:

```glsl
// Four octaves at WoWs cascade scales
vec2 f2Slopes = vec2(0.0);
float fVariance = 0.0;

// Per-octave: sample slope texture, accumulate slopes and variance
for each octave i (4 total):
    vec2 uv = f2InInitialPosition / octaveScale[i] + time * scrollDir[i] * scrollSpeed[i];
    // Anti-tiling: offset UV by noise at a much larger scale
    uv += antiTileStrength * texture(noiseTextureSampler, f2InInitialPosition * antiTileScale).xy;
    vec2 slope = texture(normalmapTextureSampler, uv).xy * slopeScale[i];
    f2Slopes += slope;
    fVariance += dot(slope, slope);  // accumulate squared slopes for variance
```

The resulting normal: `normalize(vec3(f2Slopes, 1.0))`

### Add Slope Variance -> Roughness

```glsl
float fSlopeVariance = fVariance - dot(f2Slopes, f2Slopes);  // variance = E[x^2] - E[x]^2
fSlopeVariance = max(fSlopeVariance, slopeVarianceBias);
fSlopeVariance *= slopeVarianceScale;
// Override Phase 1's constant roughness:
float fInvSlopeVar = 1.0 / fSlopeVariance;
```

When `Slope Variance Enable` is off, falls back to Phase 1's constant roughness slider.

### Existing Sliders Repurposed

- `Sampled Normals Size` -> becomes overall scale multiplier for all 4 octaves
- `Sampled Normals Speed` -> becomes overall speed multiplier
- `Sampled Normals Size Mod` -> height-based scale modulation (unchanged semantics)

## New Tweaks Sliders

Section: **"Water Normals"** (new TweakSection, or extend existing Water Specular normals subsection)

| Slider | Min | Max | Default | Purpose |
|--------|-----|-----|---------|---------|
| Octave 1 Scale | 500.0 | 2000.0 | 1077.0 | Largest octave world-space meters |
| Octave 2 Scale | 50.0 | 500.0 | 154.0 | Second octave |
| Octave 3 Scale | 5.0 | 100.0 | 22.0 | Third octave |
| Octave 4 Scale | 0.5 | 20.0 | 3.1 | Smallest octave |
| Octave Speed Ratio | 0.5 | 3.0 | 1.2 | Speed multiplier per successive octave |
| Slope Variance Enable | bool | -- | true | Use computed variance for roughness |
| Slope Variance Bias | 0.0 | 0.5 | 0.05 | Minimum roughness floor |
| Slope Variance Scale | 0.1 | 5.0 | 1.0 | Multiplier on computed variance |
| Anti-Tile Strength | 0.0 | 1.0 | 0.3 | UV offset amount to break tiling |
| Anti-Tile Scale | 0.001 | 0.1 | 0.01 | Noise frequency for anti-tiling |

## Design Choices

1. **Anti-tiling method: hex-tiling vs noise UV offset**
   - *Resolved via Tweaks slider:* Start with noise-based UV offset (simpler, cheaper, sufficient from kilometers up). `Anti-Tile Strength = 0.0` disables it. Hex-tiling is a future upgrade if needed.

2. **Number of octaves: 4 (WoWs) vs keep current 6**
   - *Needs user decision.*
   - **Pro 4 octaves:** Matches WoWs reference, cleaner cascade math, slope variance well-defined at known scales
   - **Pro 6 octaves (with 2 textures):** Already tuned, two-texture variety breaks repetition further
   - **Recommendation:** 4 octaves from a single texture, but sample both normal map textures at the 2 smallest scales for variety (4 cascade levels, 6 total samples, similar cost to current)

## Dependencies

Phase 1 (the Ward BRDF must exist to consume slope variance roughness).

## Files Modified

- `Engine/Data/Shaders/Water/Water.frag` - Replace normal sampling block, add slope variance
- `Engine/Data/Shaders/ShaderLayoutsBase.h` - ~10 new floats
- `Engine/Source/Ui/WrapperBase.h/.cpp` - 10 new Wrapper globals
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` - Register sliders
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterNormals.cpp` - New file (or extend WaterSpecular)
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` - Upload new uniforms
