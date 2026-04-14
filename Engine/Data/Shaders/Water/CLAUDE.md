# Water/ - Ocean Water Shaders

Vertex-animated ocean surface with Gerstner wave simulation, Fresnel-based skybox reflections, and depth-based coloring.

## Overview

Renders the ocean surface as a tessellated quad covering the visible area. The vertex shader displaces vertices using summed Gerstner waves at two frequency bands, while the fragment shader composites depth-based color, skybox reflections, directional lighting, shadows, and specular highlights. Normal map and noise UV computation uses camera-relative positions with CPU-side double-precision reduction to avoid floating-point precision loss far from the world origin.

## Shaders

- **Water.vert** - Gerstner wave vertex animation with terrain-aware amplitude damping near shorelines and early culling for off-screen vertices.
- **Water.frag** - Water surface shading: depth LUT color blended with noise-modulated water color, directional sunlight and moonlight color, ambient color, height-darken attenuation, skybox cubemap reflection via `Specular()` with Fresnel and sun bias, shadow and smoke-shadow, hue-preserving EWNS pre-pow (per-direction luminance/average scalar ratio controlled by `fWaterAmbientPowerMode`), `WaterLighting()` sampling the radial spread lighting texture at both the world position and base-height projected position with a power mode param for luminance-vs-average blend, and additive smoke blending via `BlendSmoke()`.

## Architecture Notes

- Wave parameters are split between `GlobalLayout` (counts, steepness, debug offsets) and `MainLayout` (per-wave direction, frequency, amplitude, speed), set per-frame from `Render.cpp`
- Beach directional fade blends wave direction toward shore-perpendicular as terrain elevation increases, creating natural wave behavior near shorelines
- Lighting and base-height texture lookups use the undisplaced grid position rather than the wave-displaced world position, ensuring stable lighting that does not swim with wave animation
- The vertex shader outputs a camera-relative position (vertex minus camera center) used for UV computation in the fragment shader; world position is reconstructed where needed
- `GlobalLayout` exposes five scalar debug offset fields (low wave, medium wave, normal one, normal two, noise) used to shift texture/wave sampling coordinates independently, allowing precision testing at large world coordinates
- Normal map and noise texture UV sampling use camera-relative coordinates combined with CPU-computed double-precision reduced offsets passed as uniforms, eliminating precision artifacts at large world coordinates
- `fLightingTimeOfDayMultiplier` is applied as a linear scalar on the final `f3WaterLightingScaled` result, after `WaterLighting()`'s pow() calls, so time-of-day intensity scales linearly rather than being subject to exponentiation. The CPU populates this uniform by lerping `gLightingDayFinalMultiplier` and `gLightingNightFinalMultiplier` by `fDayPercent`
