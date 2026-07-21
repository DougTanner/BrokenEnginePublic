# Lighting Shaders - Dynamic Light Deposit and Spread

Dynamic lights follow `deposit -> spread -> combine -> temporal`. Deposit shaders write three EWNS targets; visible-light billboards instead render directly into the main framebuffer. Hex shields provide an additional surface-normal depositor from [Objects](../Objects/AGENTS.md).

## Pipeline Contracts

- Area and point deposits fade against the padded lighting-texture boundary, not the visible-area edge. Rotation can move point-light texture coordinates outside `[0,1]`, so that path uses clamped sampling.
- Each spread pass carries an accumulation chain for the next pass and emits a separate pre-accumulation snapshot weighted by the combine curve. Combine sums the snapshots; substituting accumulated outputs changes the intended multi-pass weighting.
- Spread and combine restrict work to the visible window plus the required sampling margin while preserving the accumulation chain outside the gather window.
- Spread must emit identically zero on all of its outputs for an all-zero deposit. The renderer skips the spread draws on frames that deposit no light, letting the render-pass clear stand in for the gathered result, so any deposit-independent term — an ambient floor, an elevation- or global-layout-derived contribution, a nonzero bias in the hue-preserving compression — would silently disappear on exactly those frames while lit scenes still look correct.
- Combine normalizes the snapshots, tone maps them into the scene-lighting targets, and writes the precomputed ambient target. Temporal then reprojects world positions into the previous lighting area and blends history; first-frame and out-of-area samples use current data only.
- Light-type textures are pre-blurred for deposits. Visible sprites continue sampling the original textures.

The renderer's [Graphics documentation](../../../Source/Graphics/AGENTS.md) owns lighting-area headroom, world-sized texels, zoom rescaling, and recreation behavior.
