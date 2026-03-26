# Lighting Shaders - Dynamic Light Rendering and Spreading

## Overview

Fragment and vertex shaders for rendering dynamic lights, plus compute shaders for the spreading pipeline. Two rendering paths: area/point lights write to MRT directional lighting textures (deposit phase) which are processed by the spreading pipeline, while visible lights render directly to the main framebuffer as additive billboards.

## Shaders

- **AreaLight.frag** - Renders oriented area lights into MRT color attachments with EWNS directional weighting
- **PointLight.frag** - Renders axis-aligned point lights into MRT color attachments with EWNS directional weighting
- **VisibleLight.vert / VisibleLight.frag** - Instanced billboard rendering for visible light effects with terrain intersection fading
- **LightingSpread.frag** - Radial directional spread fragment shader with MRT output; samples all three color channels simultaneously across 20 evenly-spaced radial directions and configurable ring count, with optional per-texel jitter, directional weighting (EWNS), and decay. Runs as N passes (configurable via a runtime slider), each reading the previous pass's output and writing to its own set of spread textures via a dedicated spread render pass
- **LightCombine.comp** - Tone maps accumulated float16 lighting to UNORM8 output for all three color channels in a single dispatch using exposure, linear-clamp blend, and power curve. Reads sampler arrays (one entry per spread pass per color channel) and sums all spread pass outputs before tone mapping

## Architecture Notes

**Pipeline**: Deposit (MRT fragment) → Spread×N (fragment MRT → spread textures) → Combine (tone map to UNORM).

The spread phase uses a fragment shader render pass with MRT so all three color channels are spread in a single draw per pass. Multiple spread passes run sequentially, each reading the previous pass's output and writing to its own set of spread textures. The number of passes is configurable at runtime via a slider. The combine pass sums all spread pass outputs.

**Stable lighting area**: Uses a dedicated area with ceil'd dimensions and a texel-grid-snapped origin (computed in `GlobalUniforms.cpp`), independent of the visible area, eliminating per-frame flicker from sub-texel camera movement.

**Area vs. point directional deposit**: `AreaLight.frag` computes EWNS direction weights from interpolated world-space position relative to the quad center (passed as varyings from `QuadsVisibleArea.vert`); `PointLight.frag` uses texcoord-space offset from the quad center instead. Both use a tunable blend between Euclidean (raw component magnitudes) and normalized (sum-to-one) energy distribution, controlled by a global uniform.
