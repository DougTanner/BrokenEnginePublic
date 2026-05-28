# Lighting Shaders - Dynamic Light Rendering and Spreading

## Overview

Fragment and vertex shaders for rendering dynamic lights, plus compute shaders for the spreading pipeline. Two rendering paths: area/point lights write to MRT EWNS directional-lighting textures (deposit phase) which are processed by the spreading pipeline; visible lights render directly to the main framebuffer as additive billboards.

(The hub `../CLAUDE.md` owns the shared deposit conventions — scalar block layout, bindless textures, multi-set descriptors, four-channel EWNS, and the world-space cos^2 directional-deposit weighting with epsilon omnidirectional fallback. Not repeated here.)

## Shaders

- **AreaLight.frag / PointLight.frag** - MRT deposit of oriented (area) and axis-aligned (point) lights. Both write per-channel EWNS deposit, apply a screen-space deposit edge-fade, and atomically OR a tile-occupancy bitmask (compute-tile granularity) so downstream passes can skip empty tiles. PointLight additionally rotates its texture sample about center.
- **VisibleLight.vert/.frag** - Instanced billboards for visible light effects with terrain-elevation fading above base height.
- **LightingSpread.frag** - Radial gather pass run N times (runtime pass count); each pass reads the previous pass's textures. Per pass, all spread parameters interpolate from Start to End across the pass sequence. Rings with increasing per-ring direction counts, per-ring angular jitter, and per-fragment polar sample jitter (clustering bias). Applies directionality (EWNS-weighted vs omnidirectional mix), distance falloff, decay, and terrain-elevation height attenuation above base height. Pass 0 reads the deposit texture and runs a hue-preserving per-EWNS Reinhard compress; every pass runs a second per-pass output compress. Carries accumulated state forward via an accumulation-decay add, and emits a separate pre-accumulation snapshot weighted by a per-pass combine-curve point for LightCombine.
- **LightCombine.comp** - Sums the per-pass spread snapshots across all three channels, applies pass-count normalization and exposure scaling, then tone maps float16 to UNORM8 via the Uchimura curve. `fCombineHuePreserve` blends per-channel tone mapping against luminance-preserving tone mapping (with a fast path at the extremes). Also writes a precomputed ambient texture (`combineImageAmbient`, RGBA8 UNORM) holding `0.25 * (E+W+N+S)` of the tone-mapped per-direction values, sampled by Terrain/Water to replace three EWNS samples with one.
- **LightingBlurH.comp / LightingBlurV.comp** - Separable Gaussian pre-blur of light-type textures. Radius/sigma packed into push constants; the vertical pass additionally applies a radial edge attenuation whose falloff exponent is packed in the push-constant fractional part.

## Architecture Notes

**Pipeline**: Deposit (MRT fragment + occupancy bitmask) -> Spread x N (MRT fragment, parameters interpolated per pass, snapshots emitted) -> Combine (compute: sum snapshots, normalize, Uchimura tone map to UNORM + ambient).

**Pre-blur**: After light-type textures load, H/V blur produces 2x-size RGBA8 render targets registered in the bindless array under a salted CRC (`originalCrc ^ "BLUR"`). Deposit shaders sample the blurred version; visible light sprites keep the original. Parameter changes trigger re-blur.

**Stable lighting area**: Dedicated area sized from the camera's continuous `f4RenderVisibleArea` with a texel-grid-snapped origin (see `GlobalUniforms.cpp`), independent of visible area, eliminating sub-texel flicker on Z motion.
