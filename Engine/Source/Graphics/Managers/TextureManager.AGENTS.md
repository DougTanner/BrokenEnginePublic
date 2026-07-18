# TextureManager

**Global**: `gpTextureManager`

Comprehensive texture and sampler management with lazy loading and deferred descriptor updates. Delegates descriptor management, caching, and render targets to three sub-objects.

## Sub-Objects

| Member | Type | Purpose |
|--------|------|---------|
| `mTextureDescriptors` | `TextureDescriptors` | Global descriptor Set 0, bindless texture array, per-pipeline binding tracking, deferred descriptor updates |
| `mTextureCache` | `TextureCache` | GPU-to-CPU image readback, file-based texture caching, PBR BRDF LUT generation |
| `mRenderTargetTextures` | `RenderTargetTextures` | All effect render targets: lighting (MRT + blur chains), shadows, smoke, wind, object shadows, terrain |

## Pre-Blur Lighting Textures

When a light type texture (AreaLights, PointLights) finishes loading, `BlurLightingTexture()` runs a separable Gaussian blur via the `kPipelineLightingBlurH`/`kPipelineLightingBlurV` compute pipelines into a 2x-size RGBA8 result texture. The blurred result is registered in the bindless array under a salted CRC (`originalCrc ^ "BLUR"`) so deposit shaders can look it up via `CrcToBlurredIndex()`. `ReblurAllLightingTextures()` re-runs all blurs when sigma changes at runtime. CRCs to blur are tracked in `mLightingTextureCrcs`, populated at startup via `RegisterLightingTextureCrc()` called from `TypeRegistry::RegisterType()`. The bindless array reserves a fixed block of blur slots appended past the loaded-texture count; the count of registered light type textures is bounded by that reservation (asserted at registration). Exceeding it requires growing the reserved block.

## Lazy Loading

Deferred textures start with white placeholder VkImageView. Background disk loading via FileManager, GPU upload via TextureUploadManager, then adoption into rendering pipeline. Adoption rate-limited (4 GPU-uploaded and 1 fallback per frame) to prevent frame spikes.

## Demand-Driven Loading

Texture chunk loads are deferred until a pipeline first renders with a non-zero instance count, triggered by WriteIndirectBuffer().

## Screen-Dependent Resources

Partial teardown/rebuild of render targets (in `mRenderTargetTextures`) and global descriptor sets (in `mTextureDescriptors`) during swapchain recreation while preserving all loaded textures.

## Samplers & IBL

Ten sampler types; applicable texture samplers have runtime-configurable anisotropy. Model normal and metallic-roughness data use a dedicated direct mip-bias slider (negative sharpens, positive blurs); water normals retain their own zero-default bias because the shader's analytic-LOD variance lookup must track the hardware fetch LOD; remaining applicable samplers use the global sharpen bias. The ctor chunk walk also copies each water normal map's header-baked per-mip Toksvig variance table (chunk headers are resident at startup; the lazy pixel data is not) for the per-frame uniform upload — see [Water/AGENTS.md](../../../Data/Shaders/Water/AGENTS.md) for the consuming technique. Three IBL cubemaps (irradiance, two pre-filtered radiance) loaded from pre-baked pack data via WaitForTextures().

## TextureDescriptors

Shared descriptor set layout and per-framebuffer sets containing uniform buffers, repeat/clamp samplers, and an unsized bindless texture array with PARTIALLY_BOUND and UPDATE_AFTER_BIND flags. All non-compute graphics pipelines reference this as their external Set 0. Pipelines register texture bindings during descriptor set creation; when textures load, new VkImageViews are propagated to all registered bindings. Sampler-only recreation supported without pipeline rebuild via `RewriteSamplerDescriptors()`. `CrcToBlurredIndex()` looks up the salted CRC (`crc ^ "BLUR"`) in the bindless map, falling back to the original CRC if no blurred version exists.

## TextureCache

GPU-to-CPU image readback via `CopyImageToHostMemory()` (static). File-based texture caching with versioned headers for pre-computed textures (PBR BRDF LUT, irradiance/radiance cubemaps). `GeneratePbrLutBrdf()` generates the PBR BRDF lookup texture, using the file cache to avoid regeneration.

## RenderTargetTextures

Owns all effect render target textures: MRT lighting (3 R/G/B textures + blur chains, plus directional/ambient temporal-history textures), shadow/shadow blur, smoke ping-pong, wind ping-pong, object shadow/blur, terrain (elevation, color, normal, ambient occlusion), and log texture. Created/destroyed during swapchain recreation. Lighting deposit/spread/combine targets are pre-sized for headroom via `TextureManager::LightingDetailTextureSize` (the lighting counterpart of `DetailTextureSize`/`WaterDetailTextureSize`), so the world-sized-texel grid has room to slide under pan / coarsen under zoom-out before cropping.
