# TextureManager

**Global**: `gpTextureManager`

Comprehensive texture and sampler management with lazy loading and deferred descriptor updates. Delegates descriptor management, caching, and render targets to three sub-objects.

## Sub-Objects

| Member | Type | Purpose |
|--------|------|---------|
| `mTextureDescriptors` | `TextureDescriptors` | Global descriptor Set 0, bindless texture array, per-pipeline binding tracking, deferred descriptor updates |
| `mTextureCache` | `TextureCache` | GPU-to-CPU image readback, file-based texture caching, PBR BRDF LUT generation |
| `mRenderTargetTextures` | `RenderTargetTextures` | All effect render targets: lighting (MRT + blur chains), shadows, smoke, wind, object shadows, terrain |

## Lazy Loading

Deferred textures start with white placeholder VkImageView. Background disk loading via FileManager, GPU upload via TextureUploadManager, then adoption into rendering pipeline. Adoption rate-limited (4 GPU-uploaded and 1 fallback per frame) to prevent frame spikes.

## Demand-Driven Loading

Texture chunk loads are deferred until a pipeline first renders with a non-zero instance count, triggered by WriteIndirectBuffer().

## Screen-Dependent Resources

Partial teardown/rebuild of render targets (in `mRenderTargetTextures`) and global descriptor sets (in `mTextureDescriptors`) during swapchain recreation while preserving all loaded textures.

## Samplers & IBL

Eight sampler types with runtime-configurable anisotropy. Three IBL cubemaps (irradiance, two pre-filtered radiance) loaded from pre-baked pack data via WaitForTextures().

## TextureDescriptors

Shared descriptor set layout and per-framebuffer sets containing uniform buffers, repeat/clamp samplers, and an unsized bindless texture array with PARTIALLY_BOUND and UPDATE_AFTER_BIND flags. All non-compute graphics pipelines reference this as their external Set 0. Pipelines register texture bindings during descriptor set creation; when textures load, new VkImageViews are propagated to all registered bindings. Sampler-only recreation supported without pipeline rebuild via `RewriteSamplerDescriptors()`.

## TextureCache

GPU-to-CPU image readback via `CopyImageToHostMemory()` (static). File-based texture caching with versioned headers for pre-computed textures (PBR BRDF LUT, irradiance/radiance cubemaps). `GeneratePbrLutBrdf()` generates the PBR BRDF lookup texture, using the file cache to avoid regeneration.

## RenderTargetTextures

Owns all effect render target textures: MRT lighting (3 R/G/B textures + blur chains), shadow/shadow blur, smoke ping-pong, wind ping-pong, object shadow/blur, terrain (elevation, color, normal, ambient occlusion), and log texture. Created/destroyed during swapchain recreation.
