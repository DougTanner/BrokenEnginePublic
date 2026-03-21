# Lighting Shaders - Dynamic Light Rendering and Post-Processing

## Overview

Fragment and vertex shaders for rendering dynamic lights and post-processing the lighting buffer. Two rendering paths: area/point lights write to MRT directional lighting textures for later consumption by terrain/object shaders, while visible lights render directly to the main framebuffer as additive billboards.

## Shaders

- **AreaLight.frag** - Renders oriented area lights into MRT color attachments with omnidirectional (non-directional) output
- **PointLight.frag** - Renders axis-aligned point lights into MRT color attachments with EWNS directional weighting
- **VisibleLight.vert / VisibleLight.frag** - Instanced billboard rendering for visible light effects (e.g., glowing sources) with terrain intersection fading via elevation sampling
- **LightingBlur.frag** - MRT polar blur filter: reads R/G/B lighting via 3 separate samplers and writes to 3 color outputs simultaneously. Uses pre-computed direction vectors and a push-constant-driven sample count for progressive resolution reduction across blur levels. Randomized jitter breaks banding artifacts
- **LightingCombine.frag** - Sums multiple blur levels into a final lighting texture using time-of-day-controlled exponential decay weights

## Architecture Notes

**Two light categories**: Area/point lights write to MRT directional lighting textures that go through blur and combine post-processing before consumption by terrain/object shaders. Visible lights bypass this pipeline and render directly to the main framebuffer as additive screen-space billboards.

**Stable lighting area**: The lighting spread system uses a dedicated lighting area (stable, ceil'd dimensions with a texel-grid-snapped origin) independent of the camera visible area. This follows the same pattern as smoke and wind areas, eliminating per-frame flicker caused by sub-texel camera movement.

**Blur MRT pass**: A single LightingBlur draw per blur level processes all three color channels simultaneously (3 inputs, 3 outputs). The sample distance count is passed via push constants, allowing higher blur levels to use fewer samples for a progressive quality/cost tradeoff.

**Alpha-as-contribution model**: Both light categories use alpha as an independent contribution multiplier rather than transparency, scaling additive RGB output. Visible lights additionally apply terrain intersection fading to the alpha channel.
