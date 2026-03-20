# Water/ - Ocean Water Shaders

Vertex-animated ocean surface with Gerstner wave simulation, Fresnel-based skybox reflections, and depth-based coloring.

## Overview

Renders the ocean surface as a tessellated quad covering the visible area. The vertex shader displaces vertices using summed Gerstner waves at two frequency bands, while the fragment shader composites depth-based color, skybox reflections, directional lighting, shadows, and specular highlights. All calculations operate in world space.

## Shaders

- **Water.vert** - Gerstner wave vertex animation with terrain-aware amplitude damping near shorelines and early culling for off-screen vertices.
- **Water.frag** - Multi-layer water surface shading combining depth LUT coloring, animated normals, Fresnel skybox reflections, specular highlights, directional sun lighting, shadow mapping, and smoke (shadow attenuation at world position, additive blending at base height via `BlendSmoke`). Lighting and smoke are sampled at base height (parallax-corrected) position rather than the displaced wave position, ensuring stability as waves animate.

## Architecture Notes

- Wave parameters are split between `GlobalLayout` (counts, steepness) and `MainLayout` (per-wave direction, frequency, amplitude, speed), set per-frame from `Render.cpp`
- Beach directional fade blends wave direction toward shore-perpendicular as terrain elevation increases, creating natural wave behavior near shorelines
