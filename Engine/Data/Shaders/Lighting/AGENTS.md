# Lighting Shaders - Dynamic Light Deposit and Spread

Dynamic lights follow `deposit -> spread -> combine -> temporal`. Deposit shaders write three EWNS targets (EWNS is defined in the shader hub, `../AGENTS.md`); visible-light billboards instead render directly into the main framebuffer. Hex shields provide an additional surface-normal depositor from Objects (`../Objects/AGENTS.md`).

## Pipeline Contracts

- Area and point deposits fade against the padded lighting-texture boundary, not the visible-area edge. Rotation can move point-light texture coordinates outside `[0,1]`, so that path uses clamped sampling. A bounded indirect clear resets the deposit rectangle every frame; raster then uses attachment loads and fragment rectangle rejection so texels outside it remain untouched.
- Each spread pass carries an accumulation chain for the next pass and emits a separate pre-accumulation snapshot weighted by the combine curve. Combine sums the snapshots; substituting accumulated outputs changes the intended multi-pass weighting.
- Spread rectangles are backward-closed across mixed extents, gather reach, and linear-filter footprints. Each pass loads its attachments and draws only its published UV window, so both the accumulation chain and each combine snapshot have the source coverage their downstream consumers require.
- Combine normalizes the snapshots, tone maps them into the scene-lighting targets, and writes the precomputed ambient target. Combine, temporal, and history copy use rectangle-offset indirect dispatch; temporal accepts history only when its complete bilinear footprint is valid. Reset uses current data only, then a separate bounded history copy publishes the resolved result.
- Light-type textures are pre-blurred for deposits. Visible sprites continue sampling the original textures.

The renderer's Graphics documentation (`../../../Source/Graphics/AGENTS.md`) owns lighting-area headroom, world-sized texels, zoom rescaling, and recreation behavior.
