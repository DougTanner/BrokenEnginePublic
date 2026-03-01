# Terrain/ - Terrain Rendering Shaders

Island terrain mesh rendering with G-buffer generation passes and final compositing.

## Overview

Renders terrain as a visible-area-covering mesh with elevation displacement. The rendering pipeline uses multiple passes to generate intermediate G-buffer textures (color, normal, elevation, ambient occlusion), which the final compositing fragment shader combines with sun lighting, shadows, smoke, and detail textures (rock and sand normal maps).

## Shaders

### G-Buffer Generation
- **Terrain.vert** - Shared vertex shader positioning vertices across the visible area with elevation-based Z displacement from the elevation texture. Outputs visible area texcoords for fragment shaders
- **TerrainColor.frag** - Samples per-island color textures into the color G-buffer using bindless indexing
- **TerrainElevation.frag** - Converts raw heightmap values to world-space elevation, scaling above-water terrain by island height and below-water terrain by water depth
- **TerrainNormal.frag** - Samples per-island normal maps with per-island X/Y flip support for seamless tiling across mirrored grid cells
- **TerrainAmbientOcclusion.frag** - Generates ambient occlusion from per-island AO textures with a global intensity multiplier

### Final Compositing
- **Terrain.frag** - Composites all G-buffer passes into final terrain color. Applies rock detail textures (normal maps and albedo) on elevated rocky areas, sand detail textures on beaches, and snow detection on white regions. Combines sun lighting, terrain and object shadow mapping, smoke shadows, four-channel directional lighting at parallax-corrected base height, and smoke fog

## Architecture Notes

- G-buffer passes use bindless per-island texture arrays with `nonuniformEXT` dynamic indexing, allowing each island instance to sample its own texture set
- The final compositing pass reads from the generated G-buffer textures rather than raw island data, decoupling the per-island rendering from the lighting pipeline
- Rock and sand detail blending use elevation and color-based heuristics to determine material boundaries without explicit material maps
