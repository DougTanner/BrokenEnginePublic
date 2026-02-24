# TextureManager

**Global**: `gpTextureManager`

Comprehensive texture and sampler management with lazy loading and deferred descriptor updates.

## Lazy Loading

Deferred textures start with white placeholder VkImageView. Background disk loading via FileManager, GPU upload via TextureUploadManager, then adoption into rendering pipeline. Adoption rate-limited (4 GPU-uploaded and 1 fallback per frame) to prevent frame spikes.

## Global Descriptor Set 0

Shared descriptor set layout and per-framebuffer sets containing uniform buffers, repeat/clamp samplers, and an unsized bindless texture array with PARTIALLY_BOUND and UPDATE_AFTER_BIND flags. All non-compute graphics pipelines reference this as their external Set 0.

## Deferred Descriptor Updates

Pipelines register texture bindings during descriptor set creation. When textures load, new VkImageViews are propagated to all registered bindings. Sampler-only recreation supported without pipeline rebuild via `RewriteSamplerDescriptors()`.

## Demand-Driven Loading

Texture chunk loads are deferred until a pipeline first renders with a non-zero instance count, triggered by WriteIndirectBuffer().

## Screen-Dependent Resources

Partial teardown/rebuild of render targets (lighting, shadow, smoke, wind, terrain detail) and global descriptor sets during swapchain recreation while preserving all loaded textures.

## Render Targets

MRT lighting (3 R/G/B textures), lighting blur chains, shadow/shadow blur, smoke ping-pong, wind ping-pong, object shadow, and terrain detail textures.

## Samplers & IBL

Eight sampler types with runtime-configurable anisotropy. Three IBL cubemaps (irradiance, two pre-filtered radiance) loaded from pre-baked pack data via WaitForTextures().
