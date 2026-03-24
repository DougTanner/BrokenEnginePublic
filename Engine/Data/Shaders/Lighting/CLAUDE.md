# Lighting Shaders - Dynamic Light Rendering and Spreading

## Overview

Fragment and vertex shaders for rendering dynamic lights, plus compute shaders for the 4-phase cascaded light spreading pipeline. Two rendering paths: area/point lights write to MRT directional lighting textures (deposit phase) which are then processed by the compute spreading pipeline, while visible lights render directly to the main framebuffer as additive billboards.

## Shaders

- **AreaLight.frag** - Renders oriented area lights into MRT color attachments with EWNS directional weighting and rectangular falloff; marks occupancy bits for the light spreading pipeline
- **PointLight.frag** - Renders axis-aligned point lights into MRT color attachments with EWNS directional weighting; marks occupancy bits
- **VisibleLight.vert / VisibleLight.frag** - Instanced billboard rendering for visible light effects with terrain intersection fading
- **LightFirstSpread.comp** - Phase 2: Gaussian gather from deposit texture into first-spread textures at their own pixel density; uses occupancy-based early-out and world-space locked kernel
- **LightScatter.comp** - Phase 3: Gaussian gather from first-spread texture into progressively downscaled cascade textures; unconditional (no occupancy check), world-space locked kernel
- **LightOccupancyDilate.comp** - Dilates occupancy masks so the first-spread phase covers neighboring tiles
- **LightAccumulate.comp** - Phase 4: Combines cascade levels (highest to lowest) then first spread into the final accumulate texture with per-level decay weighting

## Architecture Notes

**4-phase pipeline**: Deposit (fragment shaders) → First Spread (`LightFirstSpread.comp`, occupancy-gated) → Cascade Scatter (`LightScatter.comp`, unconditional) → Accumulate (`LightAccumulate.comp`). First spread and cascade scatter both use world-space locked kernels so spread radius is independent of texture resolution.

**Stable lighting area**: The lighting spread system uses a dedicated area with ceil'd dimensions and a texel-grid-snapped origin (computed in `GlobalUniforms.cpp`), independent of the visible area, eliminating per-frame flicker from sub-texel camera movement.

**Occupancy-driven early-out**: Area/point light fragment shaders write occupancy bits to a storage buffer. The first-spread phase checks occupancy to skip empty regions; cascade scatter is unconditional.
