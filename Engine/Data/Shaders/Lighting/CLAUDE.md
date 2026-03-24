# Lighting Shaders - Dynamic Light Rendering and Spreading

## Overview

Fragment and vertex shaders for rendering dynamic lights, plus compute shaders for the spreading pipeline. Two rendering paths: area/point lights write to MRT directional lighting textures (deposit phase) which are processed by the spreading pipeline, while visible lights render directly to the main framebuffer as additive billboards.

## Shaders

- **AreaLight.frag** - Renders oriented area lights into MRT color attachments with EWNS directional weighting and rectangular falloff; marks the center tile's occupancy bit
- **PointLight.frag** - Renders axis-aligned point lights into MRT color attachments with EWNS directional weighting; marks the center tile's occupancy bit
- **VisibleLight.vert / VisibleLight.frag** - Instanced billboard rendering for visible light effects with terrain intersection fading
- **LightOccupancyDilate.comp** - Compute pass dispatched after the deposit render pass; dilates the center-only occupancy marks so the first-spread phase covers neighboring tiles
- **LightFirstSpread.comp** - Gaussian gather from deposit texture into first-spread textures; uses occupancy-based early-out and world-space locked kernel
- **LightingBlur.frag** - Fragment MRT blur pass: samples first-spread (or previous blur level) in 20 directions at configurable distance, writing RGB directional channels; supports jitter and directionality blending. Input texture size drives per-level downscaling
- **LightingCombine.frag** - Fragment combine pass: additively blends all blur levels with exponential decay weighting plus an optional first-spread layer (compile-time `ENABLE_COMBINE_FIRST_SPREAD`); outputs final lighting texture. Uses a descriptor array of blur textures with `nonuniformEXT` indexing

## Architecture Notes

**4-phase pipeline**: Deposit (MRT fragment) → Occupancy Dilate + First Spread (compute, occupancy-gated) → Blur MRT (fragment, hierarchical downscale over N levels) → Combine (fragment, additive).

**Blur MRT**: Each level renders a full-resolution MRT quad sampling from the previous level (or the first-spread texture at level 0) with a 20-direction radial kernel. Level count and downscale factor are runtime-tunable via Wrapper globals.

**Stable lighting area**: Uses a dedicated area with ceil'd dimensions and a texel-grid-snapped origin (computed in `GlobalUniforms.cpp`), independent of the visible area, eliminating per-frame flicker from sub-texel camera movement.
