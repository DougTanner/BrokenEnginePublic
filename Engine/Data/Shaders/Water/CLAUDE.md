# Water/ - Ocean Water Shaders

Vertex-animated ocean surface with Gerstner wave simulation, Fresnel-based skybox reflections, and depth-based coloring.

## Overview

Renders the ocean surface as a tessellated quad covering the visible area. The vertex shader displaces vertices using summed Gerstner waves at two frequency bands (low and medium), while the fragment shader composites depth-based color, skybox reflections, directional lighting, shadows, and specular highlights. All calculations operate in world space.

## Shaders

- **Water.vert** - Gerstner wave vertex animation with terrain-aware amplitude damping. Positions vertices across the visible area using texcoord interpolation, samples terrain elevation for early culling (off-screen vertices) and wave reduction near shorelines via beach directional fade. Outputs displaced world position and initial (pre-displacement) world position for fragment shader use.

- **Water.frag** - Multi-layer water surface shading combining depth LUT coloring, animated normal map sampling, Schlick Fresnel skybox reflections with multi-term specular highlights, directional sun lighting, shadow mapping (terrain + object + smoke shadows), and four-channel directional specular lighting. Uses terrain elevation for water transparency and early discard of above-water fragments. Lighting is sampled at base height (parallax-corrected) position rather than the displaced wave position, ensuring lighting stays stable as waves animate.

## Architecture Notes

- Wave parameters are split between `GlobalLayout` (counts, steepness) and `MainLayout` (per-wave direction, frequency, amplitude, speed), set per-frame from `Render.cpp`
- Skybox reflection and specular lighting parameters come from `MainLayout` per-frame fields
- Beach directional fade blends wave direction toward shore-perpendicular as terrain elevation increases, creating natural wave behavior near shorelines
- The fragment shader samples lighting at base height (parallax-corrected) position rather than the displaced wave position, ensuring lighting stays stable as waves animate
