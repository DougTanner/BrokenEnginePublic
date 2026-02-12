# Water/ - Ocean Water Shaders

Vertex-animated ocean surface with Gerstner wave simulation, Fresnel-based skybox reflections, and depth-based coloring.

## Overview

Renders the ocean surface as a tessellated quad covering the visible area. The vertex shader displaces vertices using summed Gerstner waves at two frequency bands (low and medium), while the fragment shader composites depth-based color, skybox reflections, directional lighting, shadows, and specular highlights.

## Shaders

- **Water.vert** - Gerstner wave vertex animation with terrain-aware amplitude damping. Positions vertices across the visible area using texcoord interpolation, samples terrain elevation for early culling (off-screen vertices) and wave reduction near shorelines via beach directional fade. Outputs displaced position, wave normal, and initial (pre-displacement) position.

- **Water.frag** - Multi-layer water surface shading combining depth LUT coloring, animated normal map sampling, Schlick Fresnel skybox reflections with multi-term specular highlights, directional sun lighting, shadow mapping (terrain + object + smoke shadows), and four-channel directional specular lighting. Uses terrain elevation for water transparency and early discard of above-water fragments.

## Architecture Notes

- Wave parameters (count, direction, wavelength, amplitude, speed, steepness) are set per-frame in `Render.cpp` via `MainLayout` uniform arrays (`pf4LowWavesOne/Two`, `pf4MediumWavesOne/Two`)
- Global water parameters (terrain fade, noise, depth feathering, Fresnel, color) come from `GlobalLayout` fields (`f4WaterOne` through `f4WaterSeven`, `i4Water`)
- Skybox and specular lighting parameters are per-frame in `MainLayout` (water skybox and water specular fields)
- Beach directional fade blends wave direction toward shore-perpendicular as terrain elevation increases
