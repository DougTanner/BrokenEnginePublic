# Lighting Shaders - Dynamic Light Rendering and Post-Processing

## Overview

Fragment and vertex shaders for rendering dynamic lights (area lights, point lights, visible lights) and post-processing the lighting buffer (blur, combine). Area and point lights write to MRT (multi-render-target) R/G/B lighting textures with four-channel directional weights per channel. Visible lights render directly to the main framebuffer using alpha-modulated additive blending.

## Shaders

### AreaLight.frag
Renders oriented area lights into three MRT color attachments (R/G/B). Unpacks color via `unpackUnorm4x8`, computes `fAlpha` from color and texture alpha as a contribution multiplier that scales the per-channel lighting output. RGB defines light color/brightness while alpha independently controls contribution strength.

### PointLight.frag
Renders axis-aligned point lights into three MRT color attachments (R/G/B). Same alpha-as-contribution pattern as AreaLight. Applies texture coordinate rotation from quad parameters and uses `CalculateDirectionalLight()` for EWNS directional weighting.

### VisibleLight.vert
Instanced vertex shader reading per-quad vertices, texcoords, and packed colors from a `VisibleLightQuadLayout` storage buffer. Outputs world position, texcoords, unpacked color, and instance index to the fragment stage.

### VisibleLight.frag
Renders visible light billboards directly to the main framebuffer using `kAddAlpha` pipeline blending (srcColor=SRC_ALPHA, dstColor=ONE). Terrain elevation sampling fades lights that intersect terrain via `fHeightPercent`. Intensity multiplies only RGB output; alpha is computed independently from height percent, controlling how much the light's RGB additively contributes to the scene. Uses the global bindless `pTextures[]` array with `nonuniformEXT()` for per-billboard texture sampling.

### LightingBlur.frag
Circular blur filter for the four-channel directional lighting textures. Applies directional weighting to blur samples so light spreads preferentially in its source direction. Uses Marsaglia MWC random number generator for jitter to break banding artifacts.

### LightingCombine.frag
Combines multiple blur levels into a final lighting texture by summing blur mip samples with exponentially decaying weights controlled by time-of-day multiplier and decay factor.

## Architecture Notes

**Two light categories**: Area/point lights write to MRT directional lighting textures (processed by blur and combine passes before consumption by terrain/object shaders). Visible lights render directly to the main framebuffer as screen-space billboards.

**Alpha-as-contribution model**: Area and point lights use `fAlpha = f4Color.a * f4Texture.a` as a contribution multiplier that scales the additive RGB output. Visible lights use `kAddAlpha` pipeline blending (srcColor=SRC_ALPHA, dstColor=ONE) where the shader's output alpha controls how much the RGB value additively contributes to the framebuffer.
