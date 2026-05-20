# Terrain/ - Terrain Rendering Shaders

Island terrain mesh rendering: a single elevation G-buffer prepass plus a final compositing pass that samples per-island color / normal / AO / material-mask bindless arrays directly.

## Overview

Renders terrain in two stages:

1. **Elevation prepass** — `QuadsAxisAlignedVisibleArea.vert` + `TerrainElevation.frag` fill the composite `mTerrainElevationTexture` (R32_SFLOAT) from the bindless per-island heightmap array. The composite elevation RTT is consumed by `Terrain.vert` (vertex Z), `Terrain.frag` (world Z), `Water.frag`, and `Shadow.comp`.
2. **Compositing pass** — `Terrain.vert` + `Terrain.frag` draw the per-island Gaea2-Mesher meshes. The vert transforms `vec2` island-local XY into world space via the per-instance `AxisAlignedQuadLayout` (center + rotation) and forwards two UVs to the frag: a world-derived visible-area UV (for elevation / shadow / object-shadow / lighting / smoke / ambient samplers) and an island-local UV (for the bindless color / normal / AO arrays). Color / normal / AO RTTs are no longer rendered — the frag samples the per-island bindless textures directly per-fragment.

## Shader Pipeline

### Elevation G-Buffer Prepass
- **QuadsAxisAlignedVisibleArea.vert** - Shared vertex shader for the axis-aligned-rect blit into the composite elevation RTT.
- **TerrainElevation.frag** - Samples the R32_SFLOAT heightmap directly into the world-space elevation G-buffer. DataPacker has pre-scaled and offset Gaea's output so pixel 0 == sea level / beach, negative == water, positive == land; the shader does no further conversion.

### Final Compositing
- **Terrain.vert** - Transforms per-vertex `vec2 f2InPosition` (island-local XY, post-DataPacker re-centering, Z stripped) into world space; re-derives world Z from the composite elevation G-buffer (`textureLod` at the world-XY-derived visible-area UV). Forwards three additional varyings for the frag: per-island texture UV (`location 1`, computed from `f2InPosition / f4VertexRect.zw + 0.5` — rotation-independent because the per-island textures live in island-local axes), bindless texture slot (`location 2`, `flat uint`), and per-instance rotation `(cos, sin)` (`location 3`, `flat vec2`).
- **Terrain.frag** - Samples per-island color / normal / AO bindless arrays directly via `nonuniformEXT(uiInTextureSlot)` at the island-local UV; BC5 normal RG is decoded with `2x-1`, rotated by the per-instance `(cos, sin)`, and Z is reconstructed via `sqrt(clamp(1 - dot(rot, rot), 0, 1))`. AO inlines the previous prepass `globalLayout.fIslandAmbientOcclusion * (1 - raw)` weighting. Combines with sun lighting, shadow mapping, smoke, and material detail blending (rock, sand, snow).

## Architecture Notes

- **Bindless arrays in the compositing pass**: `Terrain.frag` declares per-island `sampler2D[kiMaxIslands]` arrays for color / normal / AO at consecutive set=1 bindings, plus the material-mask array appended after the `Terrain.vert` SSBO so existing bindings stay put. The slot index is supplied per-instance through the `AxisAlignedQuadLayout::uiTextureSlot` SSBO field, forwarded from `Terrain.vert` as `uiOutTextureSlot` — not derived from `gl_InstanceIndex` — so a single cell can mix island templates without per-instance pipeline state. Per-slot descriptor writes live in `IslandTerrain.cpp:RegisterTextureBinding` against `kPipelineTerrain` (the elevation array is still patched on `kPipelineTerrainElevation` and `kPipelineShadowElevation`).
- **Island rotation**: textures are baked in island-local axes; `fRotation` rotates the footprint in world space only. The per-island UV in `Terrain.vert` is rotation-independent by construction. The normal tangent's `(cos, sin)` rotation moves into `Terrain.frag` (formerly in the deleted `TerrainNormal.frag`); Z is rotation-invariant.
- **Material boundaries**: rock / sand / snow blending in `Terrain.frag` is driven by a per-island BC7 RGBA mask (R=rock, G=sand, B=snow, A=flow reserved) authored in Gaea and packed by `ExportIsland`. The procedural elevation / color-distance heuristic that used to gate these blends is gone; the mask sample feeds the existing rock-normal, sand-normal, and sun-normal-snow paths directly.
- **Lighting pipeline**: `Terrain.frag` applies sun lighting and shadows, then adds `DirectionalLighting()` (3-sample EWNS at world texcoord) and `AmbientLightingPrecomputed()` (single sample of `mAmbientCombineTexture` at the base-height-projected texcoord) — both with hue-preserving pow via power mode — scaled by `fLightingTerrain` with a multiplicative/additive blend controlled by `fLightingAddTerrain`, finishing with additive smoke blending. `BlendSmokePrecomputed()` reuses the same single ambient sample (multiplied by 4 to recover the un-averaged sum), so the base-height lighting path is one fetch total.
