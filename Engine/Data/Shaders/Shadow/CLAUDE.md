# Engine/Data/Shaders/Shadow - Shadow Map Generation and Filtering

## Overview

Compute and fragment shaders for terrain shadow map generation and blur filtering. These shaders produce shadow data consumed by lighting and object rendering passes.

## Shaders

- **Shadow.comp** - Generates terrain shadow maps by ray-marching from each texel toward the sun across the elevation texture, computing shadow intensity based on terrain angle, distance falloff, and elevation-based height fade
- **ShadowBlur.comp** - Applies Gaussian blur to the terrain shadow texture using a sliding-window approach that shifts pixel data through a rolling buffer to minimize redundant texture reads
- **ObjectShadowsBlur.frag** - Blurs object shadow textures using a box filter with distance-based offset spacing, running as a fullscreen fragment pass

## Architecture Notes

- Two separate blur pipelines: compute-based Gaussian blur for terrain shadows, fragment-based box blur for object shadows
- Shadow generation ray-marches in the sun direction with signed step increments for sunrise/sunset handling

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
