# TextureManager

Global: `gpTextureManager`

Owns loaded textures, global texture descriptors, cached generated textures, and renderer targets. Lazy textures begin on a white placeholder and enter the bounded per-frame adoption path after a load request. Non-indirect pipelines request at creation, indirect pipelines defer until their first positive instance write, and priority textures request at boot. Bindless arrays use fixed backing storage: their addresses and slot indices are texture-descriptor registry identity.

## Resource Boundaries

- Swapchain recreation selectively rebuilds screen-dependent targets and descriptor sets while preserving loaded textures.
- Sampler variants preserve format-specific filtering and addressing contracts: R16_SFLOAT smoke and elevation use linear filtering, while model material, water-normal, and general sampling biases and anisotropy remain separate.
- Light-type textures consume a fixed reservation of bindless slots for pre-blurred results. Grow that reservation if the registered lighting-texture count exceeds its capacity.
- A lighting texture's pre-blurred result is found under its own CRC salted with `kBlurSalt`, so the blur write site and every read site agree without a second lookup table. `ReblurAllLightingTextures` re-runs the blur for every ready lighting texture after a change that invalidates the results.
- Pack-backed cubemaps and texture headers are trust boundaries; validate sizes before allocation or copy. Invalid deferred data preserves the placeholder rather than creating a replacement image.

## Sub-Objects

Three owned-by-value units, each its own source file with no separate document:

- `mTextureDescriptors` (`TextureDescriptors`) - global descriptor Set 0, the bindless texture array, per-pipeline binding tracking, and deferred descriptor updates.
- `mTextureCache` (`TextureCache`) - GPU-to-CPU image readback, the on-disk cache of generated textures, and PBR BRDF lookup-table generation.
- `mRenderTargetTextures` (`RenderTargetTextures`) - creation and sizing of every effect render target.

The on-disk cache is versioned: a cached file is reused only when its magic number, version, format, extent, mip count, layer count, and source CRC all match what the caller asks for, and the version is stamped on write. A newly generated texture — another lookup table, a prefiltered environment map — reuses this cache instead of regenerating every boot or adding a second caching path.

Water's variance-table consumption is documented in Water shaders (`../../../Data/Shaders/Water/AGENTS.md`). Renderer-wide recreation and descriptor-patch timing belong to Graphics.
