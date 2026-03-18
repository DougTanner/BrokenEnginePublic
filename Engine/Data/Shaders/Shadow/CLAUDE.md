# Engine/Data/Shaders/Shadow - Shadow Map Generation and Filtering

## Overview

Compute shaders for terrain and object shadow map generation and blur filtering. These shaders produce shadow data consumed by lighting and object rendering passes.

## Shaders

- **Shadow.comp** - Generates terrain shadow maps by ray-marching from each texel toward the sun across the elevation texture, computing shadow intensity based on terrain angle, distance falloff, and elevation-based height fade
- **ShadowBlurH.comp** - Horizontal pass of separable Gaussian blur on the terrain shadow texture, writing to an intermediate texture
- **ShadowBlurV.comp** - Vertical pass of separable Gaussian blur on terrain shadows, reading the intermediate texture and writing the final blurred result
- **ObjectShadowsBlurH.comp** - Horizontal pass of separable Gaussian blur (radius 5, sigma-controlled) on the object shadows texture, writing to an R16_UNORM intermediate texture
- **ObjectShadowsBlurV.comp** - Vertical pass of separable Gaussian blur on object shadows, reading the R16_UNORM intermediate texture and writing the final blurred result

## Architecture Notes

- Both terrain and object shadow blur use the same separable two-pass Gaussian blur pattern (radius 5) with an intermediate texture
- Shadow generation ray-marches in the sun direction with signed step increments for sunrise/sunset handling

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
