# Lighting Shaders - Dynamic Light Rendering and Post-Processing

## Overview

Fragment and vertex shaders for rendering dynamic lights and post-processing the lighting buffer. Two rendering paths: area/point lights write to MRT directional lighting textures for later consumption by terrain/object shaders, while visible lights render directly to the main framebuffer as additive billboards.

## Shaders

- **AreaLight.frag** - Renders oriented area lights into MRT color attachments with omnidirectional (non-directional) output
- **PointLight.frag** - Renders axis-aligned point lights into MRT color attachments with EWNS directional weighting
- **VisibleLight.vert / VisibleLight.frag** - Instanced billboard rendering for visible light effects (e.g., glowing sources) with terrain intersection fading via elevation sampling
- **LightingBlur.frag** - Polar blur filter for directional lighting textures with randomized jitter to break banding artifacts
- **LightingCombine.frag** - Sums multiple blur levels into a final lighting texture using time-of-day-controlled exponential decay weights

## Architecture Notes

**Two light categories**: Area/point lights write to MRT directional lighting textures that go through blur and combine post-processing before consumption by terrain/object shaders. Visible lights bypass this pipeline and render directly to the main framebuffer as additive screen-space billboards.

**Alpha-as-contribution model**: Both light categories use alpha as an independent contribution multiplier rather than transparency, scaling additive RGB output. Visible lights additionally apply terrain intersection fading to the alpha channel.
