# Water/ - Ocean Water Shaders

Vertex-animated ocean surface with Gerstner wave simulation, Fresnel-based skybox reflections, and depth-based coloring.

## Overview

Renders the ocean surface as a tessellated quad covering the visible area. The vertex shader displaces vertices using summed Gerstner waves at two frequency bands (low and medium), while the fragment shader composites depth-based color, skybox reflections, directional lighting, shadows, and specular highlights.

## Shaders

- **Water.vert** - Gerstner wave vertex animation with terrain-aware amplitude damping. Positions vertices across the visible area using texcoord interpolation, samples terrain elevation for early culling (off-screen vertices) and wave reduction near shorelines via beach directional fade. Uses local positions (world position minus wave origin) for wave calculations to maintain float precision at large world coordinates; per-wave phase offsets precomputed in double precision on the CPU (stored in `.w` of wave parameter vectors) compensate for the coordinate shift. Outputs displaced position, wave normal, and initial (pre-displacement) position.

- **Water.frag** - Multi-layer water surface shading combining depth LUT coloring, animated normal map sampling, Schlick Fresnel skybox reflections with multi-term specular highlights, directional sun lighting, shadow mapping (terrain + object + smoke shadows), and four-channel directional specular lighting. Computes eye direction vectors in local coordinates (world position minus wave origin) for float precision at large world coordinates, matching the vertex shader's local-space approach. Samples normal maps and noise textures using world-space positions (texture hardware handles large coordinates without precision loss, unlike sin/cos). Uses terrain elevation for water transparency and early discard of above-water fragments.

## Architecture Notes

- Wave parameters are split between `GlobalLayout` (counts, steepness, wave origin) and `MainLayout` (per-wave direction, frequency, amplitude, speed, phase offset), set per-frame from `Render.cpp`. The wave origin (visible area center) and per-wave double-precision phase offsets are computed on the CPU to enable local-space wave calculations on the GPU without float precision loss
- Skybox reflection and specular lighting parameters come from `MainLayout` per-frame fields
- Beach directional fade blends wave direction toward shore-perpendicular as terrain elevation increases, creating natural wave behavior near shorelines
- The fragment shader samples lighting at base height (parallax-corrected) position rather than the displaced wave position, ensuring lighting stays stable as waves animate
