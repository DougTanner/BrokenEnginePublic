# Water Caustics

## Context

Animated caustic light patterns on the sea floor in shallow water — bright rippling cells near islands that reinforce depth perception. Very visible and attractive from RTS altitude.

Salvaged from the retired ocean-shader-rewrite phase plans (was "Phase 5"), retargeted as an add-on: the original applied caustics to a Beer-Lambert floor-albedo term that is not being built; the current shader's depth-LUT color (`f3DepthColor`) is the equivalent "light that reached the floor" term and takes the addition directly.

## Design

All in `Water.frag`, between the `depthLutSampler` fetch and the `f3PreLightingColor` mix:

```glsl
// Shallow-water window from terrain depth
float fCausticStrength = smoothstep(globalLayout.fWaterCausticsDepthMax, globalLayout.fWaterCausticsDepthMin, -fTerrainElevation);
if (fCausticStrength > 0.0f)
{
    // Two noise layers at different scales/velocities; min() gives a cheap cellular look
    // (true Voronoi rejected: ~20 distance calcs/pixel/layer, invisible difference from altitude)
    float fCausticOne = /* noiseTextureSampler fetch, scale 1, velocity 1 */;
    float fCausticTwo = /* noiseTextureSampler fetch, scale 2, velocity 2 */;
    float fCaustic = pow(min(fCausticOne, fCausticTwo), globalLayout.fWaterCausticsSharpness);
    f3DepthColor += fCaustic * fCausticStrength * globalLayout.fWaterCausticsIntensity * f3SunOrMoon;
}
```

- Adding into `f3DepthColor` (pre-mix) means caustics fade out with wave height/depth exactly like the rest of the floor color, and inherit directional lighting, height darken, and the shadow split downstream — physically sensible (light hits floor, travels back up).
- Ordering note: `f3SunOrMoon` is currently computed after the depth-LUT block — hoist it (pure reorder, no logic change) so the caustics tint can use the sun/moon `max`-combine (moonlit caustics at night).
- UV: caustics live on the sea floor, so sample at the *un-displaced* position (`f2InInitialPosition`-based), NOT the wave-displaced one. MUST follow the precision-safe sampling contract (`fract()`-wrapped UV + `textureGrad`, CPU-reduced origin/time per layer supplied like the color-noise path in `WaterUniforms.cpp`).
- Branch is warp-coherent in deep water regions (`fCausticStrength` varies smoothly with terrain), acceptable.

### New uniforms / sliders

~8 new `GlobalLayout` floats: `fWaterCausticsDepthMin/Max`, `fWaterCausticsScaleOne/Two`, `fWaterCausticsSpeedOne/Two`, `fWaterCausticsIntensity`, `fWaterCausticsSharpness`. Wrappers in `WaterWrappersBase.{h,cpp}`, a "Caustics" slider block in `RenderWaterSection()` (`TweaksScreenWater.cpp`), uploads + reduced origins/times in `WaterUniforms.cpp`. Intensity 0 disables (no separate toggle).

## Critical files

- `Engine/Data/Shaders/Water/Water.frag` — caustics block before the `f3PreLightingColor` mix; hoist `f3SunOrMoon`
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — new `GlobalLayout` floats
- `Engine/Source/Ui/WaterWrappersBase.{h,cpp}` — wrappers
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp` — sliders
- `Engine/Source/Graphics/Render/WaterUniforms.cpp` — uploads + caustics-noise reduced origins/times

## Out of scope

- Dedicated pre-baked Voronoi caustics texture (reuse `noiseTextureSampler`; a dedicated texture is a small follow-up if the cell quality disappoints — could share one asset with foam)
- Projecting caustics onto the terrain shader itself (`Terrain.frag` renders the beach above water; caustics here affect only the water fragment's floor color)
- Any change to the depth-LUT / water-color model

## Notes

- Client-only rendering path; no determinism/CRC exposure.
- Open decision for `/external-grill-plan`: whether the caustics window should also modulate by `fCaustic`'s effect on `f4OutColor.w` (transparency) — recommendation: no, keep alpha untouched.
