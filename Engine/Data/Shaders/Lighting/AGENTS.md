# Lighting Shaders - Dynamic Light Deposit and Spread

Dynamic lights follow `deposit -> spread -> combine -> temporal`. Deposit shaders write three EWNS targets (EWNS is defined in the shader hub); visible-light billboards instead render directly into the main framebuffer. Hex shields provide an additional surface-normal depositor from Objects (`../Objects/AGENTS.md`).

## Pipeline Contracts

- Area and point deposits fade against the padded lighting-texture boundary, not the visible-area edge. Rotation can move point-light texture coordinates outside `[0,1]`, so that path uses clamped sampling. The deposit render pass clears its attachments through its load operation every frame.
- Each spread pass carries an accumulation chain for the next pass and emits a separate pre-accumulation snapshot weighted by the combine curve. Combine sums the snapshots; substituting accumulated outputs changes the intended multi-pass weighting.
- Combine normalizes the snapshots, tone maps them into the scene-lighting targets, and writes the precomputed ambient target. That target holds `0.25 * (E + W + N + S)` of the tone-mapped per-direction values — the average of the four EWNS channels — so a consumer does one texture sample instead of three. Consumers that need the un-averaged sum multiply by 4 — `../Water/Water.frag` and `../Terrain/Terrain.frag`, both feeding the smoke blend. Storing a differently scaled value here without changing those two puts smoke on water and terrain off by 4x, with nothing to catch it but the artwork looking wrong. Combine, temporal, and history copy dispatch over the whole combine texture; temporal rejects history whose reprojected coordinate leaves `[0, 1]`. Reset uses current data only, then a separate history copy publishes the resolved result.
- Light-type textures are pre-blurred for deposits. Visible sprites continue sampling the original textures.

The lighting textures are deliberately larger than the visible area by a headroom multiplier (spare margin around the visible area — `game::Camera::kfLightingHeadroomMultiplier`), so a fast zoom-out still finds lit texels while the area catches up. Consumers sample them through the border sampler, whose outside-the-texture color is transparent black, so a sample past the texture edge reads as no light. Switching that sampler to clamp-to-edge — the more common default — would instead smear the outermost lit texels across the whole off-texture margin.

The renderer's Graphics documentation (`../../../Source/Graphics/AGENTS.md`) owns the camera-height texel references, world-sized texels, zoom rescaling, and recreation behavior.
