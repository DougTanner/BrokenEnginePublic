# Terrain/ - Terrain Rendering Shaders

Island terrain mesh rendering using a deferred G-buffer pipeline with final compositing.

## Overview

Renders terrain as a visible-area-covering mesh with elevation displacement. A shared vertex shader (`Terrain.vert`) positions vertices across the visible area with heightmap-based displacement. Multiple G-buffer generation fragment shaders produce intermediate textures (color, normal, elevation, ambient occlusion) per island using bindless texture arrays. The final compositing shader (`Terrain.frag`) combines all G-buffer outputs with lighting, shadows, smoke, and detail textures into the final terrain color.

## Shader Pipeline

### G-Buffer Generation (per-island bindless textures)
- **Terrain.vert** - Shared vertex shader for all G-buffer passes
- **TerrainColor.frag** - Per-island color texture sampling
- **TerrainElevation.frag** - Heightmap to world-space elevation conversion
- **TerrainNormal.frag** - Per-island normal map sampling with flip support
- **TerrainAmbientOcclusion.frag** - Per-island AO texture sampling

### Final Compositing
- **Terrain.frag** - Combines G-buffer outputs with sun lighting, shadow mapping, smoke, and material detail blending (rock, sand, snow)

## Architecture Notes

- G-buffer passes use bindless per-island texture arrays with `nonuniformEXT` dynamic indexing, decoupling per-island rendering from the lighting pipeline
- Material boundaries (rock, sand, snow) are determined by elevation and color heuristics rather than explicit material maps
- `Terrain.frag` applies sun lighting and shadows, then adds the result of `Lighting()` (directional + ambient) scaled by `fLightingTerrain` with a multiplicative/additive blend controlled by `fLightingAddTerrain`, finishing with additive smoke blending
