# Water Refraction

## Context

Water draws alpha-blended over the already-rendered terrain in the main swapchain pass (`RecordMainCommandBuffer`, `CommandBufferRecordMain.cpp`); the fixed-function blend reveals the scene beneath the surface *undistorted*. Real refraction — the underwater scene shimmering with the waves — requires sampling the scene color at wave-distorted UVs, which requires a copy of the framebuffer color taken before the water draw (Vulkan cannot sample an attachment being written in the same render pass).

Split out of the retired `WaterFoamAndRefraction.txt` (foam went to `WaterFoam.md`), updated for the current renderer.

## Design

### Scene color copy

- New `Texture mSceneColorCopyTexture` in `RenderTargetTextures.{h,cpp}` — framebuffer-sized, format matching the swapchain, `VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`, `kShaderReadOnly` steady-state layout.
- In `RecordMainCommandBuffer`: after the terrain draw and before the water draw — end the render pass, resolve (MSAA) or copy the color attachment into the copy texture with the appropriate layout transitions, re-begin the render pass with `VK_ATTACHMENT_LOAD_OP_LOAD`. The mid-pass split keeps water in the same depth-buffer context as terrain.

### Water shader (`Water.frag`)

- New sampler at the next free set-1 binding (12, after `ambientLightingSampler` at 11); descriptor added to the water pipeline's infos in `PipelineManager.cpp`.
- Screen UV from `gl_FragCoord.xy / textureSize(...)`; offset by `f3SampledNormal.xy * fWaterRefractionStrength`, scaled down toward zero in deep water (terrain-depth clamp) so only shallow water distorts visibly.
- Blend the refracted scene sample into the shallow end of the existing depth-color path (where `f3DepthColor` currently dominates via the `fHeight * fWaterDepthColorFeather` mix), and raise `f4OutColor.w` toward opaque where the refracted sample takes over — the refracted color *replaces* what alpha blending would have revealed; leaving both active double-exposes the terrain beneath.

### New uniforms / sliders

~2 new `GlobalLayout` floats: `fWaterRefractionStrength`, `fWaterRefractionDepthClamp`. Wrappers in `WaterWrappersBase.{h,cpp}`, sliders in `RenderWaterSection()` (`TweaksScreenWater.cpp`), uploads in `WaterUniforms.cpp`. Strength 0 disables.

## Critical files

- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — render-pass split + copy between terrain and water draws
- `Engine/Source/Graphics/Managers/RenderTargetTextures.{h,cpp}` — `mSceneColorCopyTexture`
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — water pipeline descriptor for the new sampler
- `Engine/Data/Shaders/Water/Water.frag` — refraction sampling + blend + alpha interaction
- `Engine/Data/Shaders/ShaderLayoutsBase.h`, `Engine/Source/Ui/WaterWrappersBase.{h,cpp}`, `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp`, `Engine/Source/Graphics/Render/WaterUniforms.cpp` — uniforms/sliders

## Out of scope

- Foam (`WaterFoam.md`)
- Chromatic aberration / per-channel refraction offsets
- Refracting objects *under* the water surface differently from terrain (single scene sample; no depth-aware ray march)
- HDR intermediate (see Notes)

## Coordination

- `Documents/Features/Graphics/HeatDistortionAndShockwave.txt`: mandatory reciprocal scene-color-copy coordination. Either plan may land first; the first creates the shared `RenderTargetTextures` resource from `gpSwapchainManager->mHdrTexture` (F16, pre-resolve), and the later plan reuses it and verifies both copy points.

## Notes

- Client-only rendering path; no determinism/CRC exposure.
- **The render-pass split is the risk center**: MSAA resolve-vs-copy path, image layout transitions on the swapchain/resolve image, and any assumptions elsewhere that the main pass is a single begin/end. Pre-staged decision for `/external-grill-plan`: verify the current MSAA configuration and pick `vkCmdResolveImage` vs `vkCmdCopyImage` accordingly.
- **Shared infrastructure**: `HeatDistortionAndShockwave.txt` also needs a scene-color copy. Whichever lands first creates `mSceneColorCopyTexture`; the other reuses it (different copy points, so confirm both copy sites can share one texture or need two).
- If `HdrResolveAndColorGrading.txt` lands first, the copy should target the HDR intermediate format (`R16G16B16A16_SFLOAT`) instead of the swapchain format.
