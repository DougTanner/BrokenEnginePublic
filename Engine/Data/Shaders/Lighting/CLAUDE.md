# Lighting Shaders - Dynamic Light Rendering and Spreading

## Overview

Fragment and vertex shaders for rendering dynamic lights, plus compute shaders for the spreading pipeline. Two rendering paths: area/point lights write to MRT directional lighting textures (deposit phase) which are processed by the spreading pipeline, while visible lights render directly to the main framebuffer as additive billboards.

## Shaders

- **AreaLight.frag** - Renders oriented area lights into MRT color attachments with EWNS directional weighting and rectangular falloff
- **PointLight.frag** - Renders axis-aligned point lights into MRT color attachments with EWNS directional weighting
- **VisibleLight.vert / VisibleLight.frag** - Instanced billboard rendering for visible light effects with terrain intersection fading
- **LightOccupancyDilate.comp** - Compute pass that grows existing occupancy bits by a fixed dilation radius
- **FirstLightSpread.comp** - Initial spread from deposit textures with occupancy-based early-out and directional Gaussian convolution
- **LightSpread.comp** - Subsequent spread passes; gathers from the previous spread texture into an independent output texture per pass (no occupancy check)
- **LightAccumulate.comp** - Additive accumulate pass; reads spread texture arrays for all three color channels in a single dispatch (indexed by push constant) and blends into accumulate image targets
- **LightCombine.comp** - Tone maps accumulated float16 lighting to UNORM8 output for all three color channels in a single dispatch using exposure, linear-clamp blend, and power curve

## Architecture Notes

**Pipeline**: Deposit (MRT fragment) → Dilate Occupancy → FirstSpread (with occupancy early-out) → N-1 subsequent Spreads (no occupancy) → Accumulate (additive blend all spread textures) → Combine (tone map to UNORM).

Each spread pass writes to its own independent texture rather than ping-ponging. The accumulate phase then additively combines all spread results. Deposit fragment shaders write initial occupancy bits, and the dilation shader grows them before the first spread.

**Stable lighting area**: Uses a dedicated area with ceil'd dimensions and a texel-grid-snapped origin (computed in `GlobalUniforms.cpp`), independent of the visible area, eliminating per-frame flicker from sub-texel camera movement.

**Area vs. point directional deposit**: `AreaLight.frag` computes EWNS direction weights from interpolated world-space position relative to the quad center (passed as varyings from `QuadsVisibleArea.vert`); `PointLight.frag` uses texcoord-space offset from the quad center instead.
