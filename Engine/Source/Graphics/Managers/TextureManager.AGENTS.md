# TextureManager

Global: `gpTextureManager`

Owns loaded textures, global texture descriptors, cached generated textures, and renderer targets. Lazy textures begin on a white placeholder and enter the bounded per-frame adoption path after a load request. Non-indirect pipelines request at creation, indirect pipelines defer until their first positive instance write, and priority textures request at boot. Bindless arrays use fixed backing storage: their addresses and slot indices are texture-descriptor registry identity.

## Resource Boundaries

- Swapchain recreation selectively rebuilds screen-dependent targets and descriptor sets while preserving loaded textures.
- Sampler variants preserve format-specific filtering and addressing contracts: R16_SFLOAT smoke and elevation use linear filtering, while model material, water-normal, and general sampling biases and anisotropy remain separate.
- Light-type textures consume a fixed reservation of bindless slots for pre-blurred results. Grow that reservation if the registered lighting-texture count exceeds its capacity.
- Pack-backed cubemaps and texture headers are trust boundaries; validate sizes before allocation or copy. Invalid deferred data preserves the placeholder rather than creating a replacement image.

Water's variance-table consumption is documented in Water shaders (`../../../Data/Shaders/Water/AGENTS.md`). Renderer-wide recreation and descriptor-patch timing belong to Graphics (`../AGENTS.md`).
