# Phase 7: Legacy Technique Recovery

## Context

The existing Water.frag has several effective custom techniques that should be re-integrated into the new PBR shader. These were developed through iteration and some work very well from the top-down perspective, particularly the anti-repetition color zones.

## Techniques to Re-Integrate

### 1. Noise-Based Color Zones (HIGH PRIORITY)

**What it does (lines 75-77):** Samples the noise texture at two different frequencies and directions (`2.0 * freq * position.xy` and `freq * -position.xy`), subtracts them, and uses the result to blend between two water colors. This creates large-scale color variation zones across the ocean that break up the visual monotony of uniform deep water.

**Why it's effective:** From kilometers up, uniform Beer-Lambert absorption makes deep ocean a flat dark blue. The noise color zones add organic-looking spatial variation (like currents, temperature zones, or sediment). This is purely artistic but very effective for top-down.

**Re-integration approach:** Add as a post-Beer-Lambert modulation:
```glsl
float fZone1 = texture(noiseTextureSampler, colorZoneFreq * f3InPosition.xy).x;
float fZone2 = texture(noiseTextureSampler, colorZoneFreq * 0.5 * -f3InPosition.xy).x;
float fZoneMix = clamp(colorZoneAmount * (fZone1 - fZone2) + 0.5, 0.0, 1.0);
f3SeaColor = mix(f3SeaColor, f3SeaColor * colorZoneTint, fZoneMix);
```

### 2. Depth LUT (MEDIUM PRIORITY)

**What it does (line 79):** Samples a 1D color LUT texture based on terrain elevation depth. This allows precise artist-controlled color at each depth level (beach sand color -> shallow turquoise -> reef blue -> deep navy).

**Why it's effective:** Beer-Lambert gives physically correct but potentially monotonous depth gradients. The LUT adds art-directed breakpoints (e.g., a distinct reef shelf color at medium depth).

**Re-integration approach:** Blend between Beer-Lambert result and LUT result based on a slider:
```glsl
vec3 f3BeerColor = BeerLambert(depth, extinction);
vec3 f3LutColor = texture(depthLutSampler, vec2(lutFeather * depth, 0.0)).xyz;
f3SeaColor = mix(f3BeerColor, f3LutColor, depthLutBlend);
```
At `depthLutBlend = 0.0` it's pure PBR. At `1.0` it's pure artist LUT. The interesting zone is `0.3-0.5` where Beer-Lambert provides the base and the LUT adds character.

### 3. Height-Based Wave Darkening (ALREADY PRESERVED)

**What it does (line 107):** Darkens wave troughs relative to crests using `fWaterHeightDarkenTop/Bottom/Clamp`. This is already preserved in Phase 1 since it's a post-lighting modulation.

### 4. Ambient Floor (LOW PRIORITY)

**What it does (line 114):** `max(f4OutColor.xyz, 0.5 * ambientColor * skyboxColor)` ensures water never goes completely black even in full shadow. This prevents the water from looking like a void in shadowed areas.

**Re-integration approach:** Keep as a simple floor after shadow multiplication:
```glsl
f4OutColor.xyz = max(f4OutColor.xyz, ambientFloor * globalLayout.f4AmbientColor.xyz * f3SkyIrradiance);
```

### 5. Skybox Color Sampling for Sky Irradiance (ALREADY INTEGRATED)

**What it does (line 94):** Samples the cubemap skybox for reflection color. In the new PBR shader, this becomes the sky irradiance term in the radiance equation. Already used by Phase 1.

## New Tweaks Sliders

Section: **"Water Legacy"** or integrate into **"Water PBR"** section

| Slider | Min | Max | Default | Purpose |
|--------|-----|-----|---------|---------|
| Color Zone Enable | bool | -- | true | Toggle noise color zones |
| Color Zone Frequency | 0.0001 | 0.01 | 0.002 | Noise frequency (lower = larger zones) |
| Color Zone Amount | 0.0 | 2.0 | 0.5 | Strength of zone variation |
| Color Zone Tint R | 0.5 | 1.5 | 0.8 | Zone color modulation, red |
| Color Zone Tint G | 0.5 | 1.5 | 1.1 | Zone color modulation, green |
| Color Zone Tint B | 0.5 | 1.5 | 1.2 | Zone color modulation, blue |
| Depth LUT Blend | 0.0 | 1.0 | 0.0 | Blend between Beer-Lambert (0) and depth LUT (1) |
| Depth LUT Feather | 0.01 | 5.0 | 1.0 | LUT sampling rate |
| Ambient Floor | 0.0 | 1.0 | 0.3 | Minimum brightness in shadow |

## Design Choices

All resolved via Tweaks sliders:
- Color zones on/off via bool toggle + amount slider at 0.0 disables
- Depth LUT is a blend slider: 0 = pure PBR, 1 = pure LUT, anything between is a hybrid
- Ambient floor is a simple float slider

## Dependencies

Phase 1 (modifies the PBR color pipeline). Independent of Phases 2-6.

## Files Modified

- `Engine/Data/Shaders/Water/Water.frag` - Add color zone and LUT blend blocks
- `Engine/Data/Shaders/ShaderLayoutsBase.h` - ~9 new floats
- `Engine/Source/Ui/WrapperBase.h/.cpp` - 9 new Wrapper globals
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` - Register sliders
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` - Upload new uniforms
