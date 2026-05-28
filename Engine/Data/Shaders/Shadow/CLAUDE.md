# Engine/Data/Shaders/Shadow - Shadow Map Generation and Filtering

## Overview

Compute shaders for terrain and object shadow map generation and blur filtering. These shaders produce shadow data consumed by lighting and object rendering passes.

## Shaders

- **Shadow.comp** - Generates terrain shadow maps by ray-marching from each texel toward the sun across the elevation texture, computing shadow intensity based on terrain angle, distance falloff, and elevation-based height fade
- **ShadowBlurH.comp** - Horizontal pass of separable Gaussian blur on the terrain shadow texture, writing to an intermediate texture
- **ShadowBlurV.comp** - Vertical pass of separable Gaussian blur on terrain shadows, reading the intermediate texture and writing the final blurred result
- **ShadowTemporal.comp** - Temporal-accumulation pass run after ShadowBlurV: blends the freshly-blurred shadow with the previous frame's result (reprojected into the current texel grid by world position) as an exponential moving average to remove the per-frame flicker the texel ramp introduces while rescaling the grid
- **ObjectShadowsBlurH.comp** - Horizontal pass of separable Gaussian blur on the object shadows texture, writing to an intermediate texture; inverts the sampled coverage (`1 - sample`) so the blur accumulates occlusion
- **ObjectShadowsBlurV.comp** - Vertical pass of separable Gaussian blur on object shadows, reading the intermediate texture and writing the final result as `1 - intensity * blurred`

## Architecture Notes

- **Rate-limited texel scale + centered visible window**: the terrain shadow texel world size is set CPU-side from the camera's rate-limited reference height (fixed at a settled eye height, so the grid is snap-stable under pan; rescaled only on the slow crawl while tracking a zoom). The shaders read it through the area uniforms and are otherwise agnostic to whether it moves. Shadow.comp and the terrain blur passes early-out outside a centered visible sub-window (the `iShadowVisible*` uniforms) expanded by a `kiShadowWindowMargin` border; only that window is ray-marched/blurred each frame (zoomed-out coarser texels shrink it back toward the default-height count), and out-of-window texels write the unshadowed lit value so blur edge reads stay consistent. The margin guarantees the consumer's bilinear edge reads land on current-frame, fully-blurred texels
- Both terrain and object shadow blur use the same separable two-pass Gaussian blur pattern with an intermediate texture; object-shadow blur radius/sigma are runtime uniforms, terrain blur radius is fixed (5) with a runtime sigma
- Terrain shadow generation marches along the elevation row in the sun's signed x-direction (sunrise/sunset reverse the step), accumulating the maximum shadow from taller texels weighted by terrain angle, distance falloff, and an elevation height-fade band; output is `(1 - shadow)` scaled by a moon multiplier
- Object-shadow coverage is inverted on the horizontal pass and the final intensity scale is applied on the vertical pass, so the two passes are not interchangeable
- **Temporal de-flicker**: the texel ramp resamples the shadow grid every frame while tracking a zoom, which makes high-contrast edges flicker. The temporal pass reprojects each texel's world position into the previous frame's area (current area vs. `f4ShadowAreaPrevious`) to fetch history, then EMA-blends with weight `fShadowTemporalBlend` (1.0 = disabled, history a no-op once the grid settles). Reprojected UVs outside [0,1] are disocclusions (newly revealed texels) and fall back to current; the first frame forces blend = 1.0 so the uninitialized history never shows. It runs in place on the blur texture over the same visible window (+ margin) as the blur passes; the blended result is then copied into a persistent history texture for the next frame. Mirrors the smoke/wind previous-area reprojection

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
