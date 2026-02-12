# Engine/Data/Shaders/Shadow - Shadow Map Generation and Filtering

## Overview

Compute and fragment shaders for terrain shadow map generation, Gaussian blur filtering, and object shadow blur. These shaders produce the shadow data consumed by lighting and object rendering passes.

## Shaders

### Shadow (Compute)
Generates terrain shadow maps by ray-marching from each texel toward the sun across the elevation texture. Computes shadow intensity based on terrain angle vs. sun angle, distance falloff, and elevation-based height fade. Outputs a single-channel shadow texture.

### ShadowBlur (Compute)
Applies a 2D Gaussian blur to the terrain shadow texture using a sliding-window kernel approach. Precomputes an NxN Gaussian kernel from a configurable sigma, then processes each row by shifting pixel data through a rolling buffer to avoid redundant texture reads. Fills edge pixels by clamping to the nearest computed value.

### ObjectShadowsBlur (Fragment)
Blurs object shadow textures using a 9x9 box filter kernel. Samples a centered grid of texels with configurable distance-based offset spacing, averages the inverted shadow values, and outputs a blended shadow intensity.

## Architecture Notes

- Terrain shadow shaders use compute dispatch with `kiShadowTextureExecutionSize` controlling workgroup height
- Shadow generation ray-marches in the sun direction using signed step increments for sunrise/sunset handling
- Object shadow blur runs as a fullscreen fragment pass, distinct from the compute-based terrain shadow blur
