# Engine/Data/Shaders/Shadow - Shadow Map Generation and Filtering

## Overview

Compute and fragment shaders for terrain shadow map generation and blur filtering. These shaders produce shadow data consumed by lighting and object rendering passes.

## Shaders

- **Shadow.comp** - Generates terrain shadow maps by ray-marching from each texel toward the sun across the elevation texture, computing shadow intensity based on terrain angle, distance falloff, and elevation-based height fade
- **ShadowBlurH.comp** - Horizontal pass of separable Gaussian blur on the terrain shadow texture, writing to an intermediate texture
- **ShadowBlurV.comp** - Vertical pass of separable Gaussian blur, reading the intermediate texture and writing the final blurred shadow
- **ObjectShadowsBlur.frag** - Blurs object shadow textures using a box filter with distance-based offset spacing, running as a fullscreen fragment pass

## Architecture Notes

- Two separate blur pipelines: compute-based Gaussian blur for terrain shadows, fragment-based box blur for object shadows
- Shadow generation ray-marches in the sun direction with signed step increments for sunrise/sunset handling

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
