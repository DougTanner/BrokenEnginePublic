# Terrain/ - Terrain Rendering Shaders

Island terrain mesh rendering using a deferred G-buffer pipeline with final compositing.

## Overview

Renders terrain in two stages: per-island G-buffer prepasses fill composite visible-area textures (color, normal, elevation, ambient occlusion) from bindless per-island heightmap/color/normal/AO assets, then a final compositing pass draws per-island Gaea2-Mesher-baked meshes and samples those composites for shading. `Terrain.vert` (compositing) takes float2 XY mesh positions (Z stripped at pack time), transforms them into world space via the per-instance `AxisAlignedQuadLayout` (center+rotation), and re-derives world Z by sampling the elevation G-buffer at the world-XY-derived visible-area UV; `Terrain.frag` reads the composite G-buffers using the same UV. `QuadsAxisAlignedVisibleArea.vert` still drives the four G-buffer prepasses (axis-aligned-rect blits into the composite RTTs).

## Shader Pipeline

### G-Buffer Generation (per-island bindless textures)
- **Terrain.vert** - Shared vertex shader for all G-buffer passes
- **TerrainColor.frag** - Per-island color texture sampling
- **TerrainElevation.frag** - Samples the R32_SFLOAT heightmap directly into the world-space elevation G-buffer. DataPacker has pre-scaled and offset Gaea's output so pixel 0 == sea level / beach, negative == water, positive == land; the shader does no further conversion.
- **TerrainNormal.frag** - Per-island normal map sampling; BC5 source, Z reconstructed as `sqrt(saturate(1 - x*x - y*y))`. Tangent (X,Y) is rotated by the per-island `(cos, sin)` forwarded from the quad vertex shader (Z is rotation-invariant)
- **TerrainAmbientOcclusion.frag** - Per-island AO texture sampling

### Final Compositing
- **Terrain.frag** - Combines G-buffer outputs with sun lighting, shadow mapping, smoke, and material detail blending (rock, sand, snow)

## Architecture Notes

- G-buffer passes use bindless per-island texture arrays with `nonuniformEXT` dynamic indexing. The slot index is supplied per-instance through the axis-aligned quad layout (forwarded from the vertex shader), not derived from `gl_InstanceIndex`, so a single cell can mix island templates without per-instance pipeline state
- Material boundaries (rock, sand, snow) are determined by elevation and color heuristics rather than explicit material maps
- `Terrain.frag` applies sun lighting and shadows, then adds `DirectionalLighting()` (3-sample EWNS at world texcoord) and `AmbientLightingPrecomputed()` (single sample of `mAmbientCombineTexture` at the base-height-projected texcoord) results — both with hue-preserving pow via power mode — scaled by `fLightingTerrain` with a multiplicative/additive blend controlled by `fLightingAddTerrain`, finishing with additive smoke blending. `BlendSmokePrecomputed()` reuses the same single ambient sample (multiplied by 4 to recover the un-averaged sum), so the base-height lighting path is one fetch total
