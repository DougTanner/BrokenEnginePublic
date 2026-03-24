# Phase 6: Subsurface Scattering

## Context

Add view-dependent color variation between wave crests and troughs. Crests appear brighter/more turquoise (light passing through thin water), troughs appear darker/deeper blue. Subtle from top-down but adds important depth cue and color richness.

## What Changes in Water.frag

### SSS Block (after Beer-Lambert, before final composition)

```glsl
// Height-based SSS: crests = thin water = more light through
float fSSSHeight = clamp((f3InPosition.z - sssHeightBase) * sssHeightScale, 0.0, 1.0);

// View-dependent: strongest when looking toward sun through wave crest
float fSSSSun = pow(max(0.0, 1.0 - max(0.0, dot(f3ViewDir, f3SunDir))), sssSunPower);

// Combined SSS
float fSSS = fSSSHeight * (fSSSSun + 0.2) * sssIntensity; // 0.2 ambient floor so height alone has effect

// Add turquoise/green tint scaled by SSS and sun
f3WaterRadiance += fSSS * f3SSSColor * globalLayout.f4SunColor.xyz;
```

Purely additive. The `0.2` ambient floor ensures wave crests get some color shift even from directly above where the view-dependent sun term is weak.

## New Tweaks Sliders

Section: **"Water SSS"** (new TweakSection)

| Slider | Min | Max | Default | Purpose |
|--------|-----|-----|---------|---------|
| SSS Enable | bool | -- | true | Master toggle |
| SSS Intensity | 0.0 | 3.0 | 0.8 | Overall brightness |
| SSS Height Base | -0.5 | 0.5 | 0.0 | Wave height where SSS starts |
| SSS Height Scale | 0.5 | 20.0 | 5.0 | How quickly SSS ramps with height |
| SSS Sun Power | 1.0 | 10.0 | 4.0 | Sharpness of view-dependent sun alignment |
| SSS Color R | 0.0 | 0.5 | 0.05 | SSS tint red |
| SSS Color G | 0.0 | 0.5 | 0.3 | SSS tint green |
| SSS Color B | 0.0 | 0.5 | 0.2 | SSS tint blue |

## Design Choices

All resolved - no user decisions needed:
- SSS Sun Power slider controls visibility at top-down angles. Low power (1-2) = visible at moderate angles. High power (8-10) = only when looking into sun.
- Height-based component works regardless of view angle, so crests always get color shift.

## Dependencies

Phase 1 only. Independent of Phases 2-5, 7.

## Files Modified

- `Engine/Data/Shaders/Water/Water.frag` - Add SSS block
- `Engine/Data/Shaders/ShaderLayoutsBase.h` - ~8 new floats
- `Engine/Source/Ui/WrapperBase.h/.cpp` - 8 new Wrapper globals
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterSSS.cpp` - New file
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` - Register sliders
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` - Upload new uniforms
