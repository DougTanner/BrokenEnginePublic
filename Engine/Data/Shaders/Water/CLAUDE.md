# Water/ - Ocean Water Shaders

Vertex-animated ocean surface with Gerstner wave simulation, Fresnel-based skybox reflections, and depth-based coloring.

## Overview

Renders the ocean surface as a tessellated quad covering the visible area. The vertex shader displaces vertices using summed Gerstner waves at two frequency bands (low and medium), while the fragment shader composites depth-based color, skybox reflections, directional lighting, shadows, and specular highlights.

## Shaders

- **Water.vert** - Gerstner wave vertex animation with terrain-aware amplitude damping. Positions vertices across the visible area using texcoord interpolation, samples terrain elevation for early culling (off-screen vertices) and wave reduction near shorelines via beach directional fade. Uses local positions (world position minus wave origin) for wave calculations to maintain float precision at large world coordinates; per-wave phase offsets precomputed in double precision on the CPU (stored in `.w` of wave parameter vectors) compensate for the coordinate shift. Outputs displaced position and initial position in wave-origin-relative (local) coordinates for fragment shader interpolation precision, while using world-space positions for clip-space transform and visible area texcoord computation.

- **Water.frag** - Multi-layer water surface shading combining depth LUT coloring, animated normal map sampling, Schlick Fresnel skybox reflections with multi-term specular highlights, directional sun lighting, shadow mapping (terrain + object + smoke shadows), and four-channel directional specular lighting. Receives position varyings in wave-origin-relative local coordinates and reconstructs world position from the wave origin uniform for operations that require it (WorldToVisibleArea, SmokeShadow, BaseHeightPosition, SpecularLighting). Normal map and noise texture UVs use local position with wave origin offsets to produce world-space-equivalent sampling. Eye direction is computed in local space (eye position minus wave origin) for float precision. Uses terrain elevation for water transparency and early discard of above-water fragments.

## Architecture Notes

- Wave parameters are split between `GlobalLayout` (counts, steepness, wave origin) and `MainLayout` (per-wave direction, frequency, amplitude, speed, phase offset), set per-frame from `Render.cpp`. The wave origin (visible area center) and per-wave double-precision phase offsets are computed on the CPU to enable local-space wave calculations on the GPU without float precision loss
- Position varyings (`f3OutPosition.xy`, `f2OutInitialPosition`) are converted to wave-origin-relative local coordinates after clip-space transform, ensuring fragment shader interpolation maintains float precision at large world coordinates. The fragment shader adds the wave origin back only for functions requiring world-space positions
- Skybox reflection and specular lighting parameters come from `MainLayout` per-frame fields
- Beach directional fade blends wave direction toward shore-perpendicular as terrain elevation increases, creating natural wave behavior near shorelines
- The fragment shader samples lighting at base height (parallax-corrected) position rather than the displaced wave position, ensuring lighting stays stable as waves animate
