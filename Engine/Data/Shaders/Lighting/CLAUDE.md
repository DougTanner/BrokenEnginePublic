# Lighting Shaders - Dynamic Light Rendering and Spreading

## Overview

Fragment and vertex shaders for rendering dynamic lights, plus compute shaders for the spreading pipeline. Two rendering paths: area/point lights write to MRT directional lighting textures (deposit phase) which are processed by the spreading pipeline, while visible lights render directly to the main framebuffer as additive billboards.

## Shaders

- **AreaLight.frag / PointLight.frag** - MRT deposit of oriented (area) and axis-aligned (point) lights with cos^2 EWNS lobe weighting from world-offset to light center (epsilon fallback near center)
- **VisibleLight.vert/.frag** - Instanced billboards for visible light effects with terrain intersection fading
- **LightingSpread.frag** - Radial MRT spread pass. Rings with increasing direction counts and per-ring angular jitter; EWNS weighting, distance falloff, decay, and terrain-elevation height attenuation. Runs N passes (runtime slider); each pass reads the previous
- **LightCombine.comp** - Sums spread pass outputs across all three channels and tone maps float16 → UNORM8 using Uchimura curve. `fCombineHuePreserve` blends per-channel vs luminance-preserving modes. Also writes a precomputed ambient texture (`mAmbientCombineTexture`, RGBA8 UNORM, `0.25 * (E+W+N+S)` of the tone-mapped per-direction values) sampled by Terrain/Water in the ambient path to replace three EWNS samples with one
- **LightingBlurH.comp / LightingBlurV.comp** - Separable Gaussian pre-blur of light type textures; vertical pass applies a box vignette using packed sample-count/falloff push constants

## Architecture Notes

**Pipeline**: Deposit (MRT fragment) → Spread×N (MRT fragment) → Combine (compute tone map to UNORM).

**Pre-blur**: After light-type textures load, H/V blur produces 2x-size RGBA8 render targets registered in the bindless array under a salted CRC (`originalCrc ^ "BLUR"`). Deposit shaders sample the blurred version; visible light sprites keep the original. Parameter changes trigger re-blur.

**Stable lighting area**: Dedicated area with ceil'd dimensions and a texel-grid-snapped origin (see `GlobalUniforms.cpp`), independent of visible area, eliminating sub-texel flicker.
