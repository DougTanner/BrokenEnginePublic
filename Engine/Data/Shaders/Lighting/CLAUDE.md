# Lighting Shaders - Dynamic Light Rendering and Post-Processing

## Overview

Fragment and vertex shaders for rendering dynamic lights (area lights, point lights, visible lights) and post-processing the lighting buffer (blur, combine). Two rendering paths: area/point lights write to MRT directional lighting textures for later consumption by terrain/object shaders, while visible lights render directly to the main framebuffer as additive billboards.

## Shaders

### AreaLight.frag
Renders oriented area lights into three MRT color attachments (R/G/B). Produces non-directional output where all four EWNS channels receive the same value, representing omnidirectional light contribution.

### PointLight.frag
Renders axis-aligned point lights into three MRT color attachments (R/G/B). Unlike area lights, applies EWNS directional weighting so light spreads preferentially in its source direction. Supports texture coordinate rotation from quad parameters.

### VisibleLight.vert / VisibleLight.frag
Instanced billboard rendering for visible light effects (e.g., glowing sources). The vertex shader reads per-quad data from a storage buffer and transforms world positions to clip space using ViewProjection perspective; the fragment shader samples terrain elevation to fade lights that intersect terrain, and supports per-billboard texture rotation and intensity scaling via bindless textures.

### LightingBlur.frag
Polar blur filter (20 directions x 4 distances) for directional lighting textures. Applies directional weighting so light spreads preferentially in its source direction. Uses Marsaglia MWC random number generator for jitter to break banding artifacts.

### LightingCombine.frag
Sums multiple blur levels into a final lighting texture using exponentially decaying weights controlled by time-of-day multiplier and decay factor.

## Architecture Notes

**Two light categories**: Area/point lights write to MRT directional lighting textures that go through blur and combine post-processing before consumption by terrain/object shaders. Visible lights bypass this pipeline and render directly to the main framebuffer as additive screen-space billboards.

**Alpha-as-contribution model**: Both light categories use alpha as an independent contribution multiplier rather than transparency. For area/point lights, color alpha and texture alpha combine to scale the additive RGB output. For visible lights, alpha-modulated additive blending controls how much RGB contributes to the framebuffer, with terrain intersection fading applied to the alpha channel.
