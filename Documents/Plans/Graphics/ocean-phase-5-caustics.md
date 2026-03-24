# Phase 5: Caustics

## Context

Add animated caustic patterns projected onto the sea floor in shallow water. Bright rippling light patterns near islands reinforce depth perception and make shallow water feel alive. Very visible and beautiful from above.

## What Changes in Water.frag

### Caustics Block (after Beer-Lambert, before final composition)

```glsl
// Only in shallow water
float fCausticDepth = -fTerrainElevation;
float fCausticStrength = smoothstep(causticsDepthMax, causticsDepthMin, fCausticDepth);

if (fCausticStrength > 0.0)
{
    // Two overlapping noise layers at different scales/speeds, take min for cellular look
    vec2 f2CausticUV = f2InInitialPosition; // world-space, NOT wave-displaced (caustics are on floor)
    float fC1 = texture(noiseTextureSampler, f2CausticUV * causticsScale1 + globalLayout.fElapsedTime * causticsSpeed1 * vec2(0.7, 0.3)).x;
    float fC2 = texture(noiseTextureSampler, f2CausticUV * causticsScale2 + globalLayout.fElapsedTime * causticsSpeed2 * vec2(-0.4, 0.6)).x;
    float fCaustic = pow(min(fC1, fC2), causticsSharpness);

    // Caustics modulated by sun shadow and depth
    // Goes on floor albedo (light hitting floor, traveling back through absorption)
    f3SeaColor += fCaustic * fCausticStrength * causticsIntensity * globalLayout.f4SunColor.xyz;
}
```

Caustics go on top of the floor albedo contribution but under surface specular (physically correct: light hits floor, travels back up through water absorption).

## New Tweaks Sliders

Section: **"Water Caustics"** (new TweakSection)

| Slider | Min | Max | Default | Purpose |
|--------|-----|-----|---------|---------|
| Caustics Enable | bool | -- | true | Master toggle |
| Caustics Depth Min | 0.0 | 5.0 | 0.5 | Shallowest depth where caustics appear |
| Caustics Depth Max | 1.0 | 20.0 | 8.0 | Deepest depth where caustics fade |
| Caustics Scale 1 | 0.001 | 0.1 | 0.015 | UV scale for first layer |
| Caustics Scale 2 | 0.001 | 0.1 | 0.025 | UV scale for second layer |
| Caustics Speed 1 | 0.0 | 2.0 | 0.4 | Scroll speed, layer 1 |
| Caustics Speed 2 | 0.0 | 2.0 | 0.3 | Scroll speed, layer 2 |
| Caustics Intensity | 0.0 | 5.0 | 1.5 | Overall brightness |
| Caustics Sharpness | 1.0 | 10.0 | 3.0 | Power applied to pattern (higher = sharper lines) |

## Design Choices

1. **Voronoi computed in shader vs noise texture min() trick**
   - *Resolved:* Use noise texture `min()` approach. True Voronoi is too expensive (~20 distance calcs per pixel per layer). The `min()` trick produces convincing cellular patterns at 2 texture fetches. Invisible difference from kilometers up.

2. **Dedicated caustics texture vs reuse noiseTextureSampler**
   - *Needs user decision.* Same trade-off as Phase 3 foam.
   - **Pro dedicated:** Much more convincing cellular pattern (pre-baked Voronoi). Could pack foam in RGB, caustics in A of one texture.
   - **Pro reuse:** Zero pipeline work.
   - **Recommendation:** Start with noise reuse. If Phase 3 adds a dedicated texture, pack caustics into it.

## Dependencies

Phase 1 only (needs Beer-Lambert depth color). Independent of Phases 2-4, 6-7.

## Files Modified

- `Engine/Data/Shaders/Water/Water.frag` - Add caustics block
- `Engine/Data/Shaders/ShaderLayoutsBase.h` - ~9 new floats
- `Engine/Source/Ui/WrapperBase.h/.cpp` - 9 new Wrapper globals
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterCaustics.cpp` - New file
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` - Register sliders
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` - Upload new uniforms
