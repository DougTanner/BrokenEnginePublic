# Water/ - Ocean Water Shaders

Vertex-animated ocean surface with Gerstner wave simulation, Fresnel-based skybox reflections, and depth-based coloring.

## Overview

Renders the ocean surface as a tessellated quad covering the visible area. The vertex shader displaces vertices using summed Gerstner waves at two frequency bands, while the fragment shader composites depth-based color, skybox reflections, directional lighting, shadows, and specular highlights. All calculations operate in world space.

## Shaders

- **Water.vert** - Gerstner wave vertex animation with terrain-aware amplitude damping near shorelines and early culling for off-screen vertices.
- **Water.frag** - Water surface shading: depth LUT color blended with noise-modulated water color, directional sunlight and moonlight color, ambient color, height-darken attenuation, skybox cubemap reflection via `Specular()` with Fresnel and sun bias, shadow and smoke-shadow, `WaterLighting()` sampling the radial spread lighting texture at both the world position and base-height projected position, and additive smoke blending via `BlendSmoke()`.

## Architecture Notes

- Wave parameters are split between `GlobalLayout` (counts, steepness) and `MainLayout` (per-wave direction, frequency, amplitude, speed), set per-frame from `Render.cpp`
- Beach directional fade blends wave direction toward shore-perpendicular as terrain elevation increases, creating natural wave behavior near shorelines
- Lighting and base-height texture lookups use the undisplaced grid position (`f2InInitialPosition`) rather than the wave-displaced world position, ensuring stable lighting that does not swim with wave animation
