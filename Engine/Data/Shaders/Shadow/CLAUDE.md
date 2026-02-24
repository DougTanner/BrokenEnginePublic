# Engine/Data/Shaders/Shadow - Shadow Map Generation and Filtering

## Overview

Compute and fragment shaders for terrain shadow map generation, Gaussian blur filtering, and object shadow blur. These shaders produce the shadow data consumed by lighting and object rendering passes.

## Shaders

### Shadow (Compute)
Generates terrain shadow maps by ray-marching from each texel toward the sun across the elevation texture. Computes shadow intensity based on terrain angle vs. sun angle, distance falloff, and elevation-based height fade. Outputs a single-channel shadow texture.

### ShadowBlur (Compute)
Applies a 2D Gaussian blur to the terrain shadow texture. Uses a sliding-window approach that shifts pixel data through a rolling buffer to avoid redundant texture reads, with edge pixels clamped to the nearest computed value.

### ObjectShadowsBlur (Fragment)
Blurs object shadow textures using a box filter with configurable distance-based offset spacing and intensity scaling. Runs as a fullscreen fragment pass, distinct from the compute-based terrain blur.

## Architecture Notes

- Terrain shadow shaders use compute dispatch with `kiShadowTextureExecutionSize` controlling workgroup height
- Shadow generation ray-marches in the sun direction using signed step increments for sunrise/sunset handling
- Two separate blur pipelines: compute-based Gaussian blur for terrain shadows, fragment-based box blur for object shadows

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
