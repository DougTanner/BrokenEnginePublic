# Tech Debt: Dead Code

Source: /external-tech-debt on Engine/Source/Graphics/Objects

## Changes

### Engine/Source/Graphics/Objects/Texture.h
- Remove unused `vkRenderPass` parameter from static `RecordEndRenderPass(VkCommandBuffer, VkRenderPass)` at line 72 — parameter is `[[maybe_unused]]` and never read; function just calls `vkCmdEndRenderPass(vkCommandBuffer)` [~5m]

### Engine/Source/Graphics/Objects/Texture.cpp
- Update static `RecordEndRenderPass` signature at line 45 to remove the `vkRenderPass` parameter [~5m]
- Update instance method `RecordEndRenderPass` at line 625-627 to call the static version without passing `mVkRenderPass` [~2m]

### Engine/Source/Graphics/Managers/CommandBufferManager.cpp
- Update the one caller of the static `Texture::RecordEndRenderPass` at line 669 to remove the `gpSwapchainManager->mVkRenderPass` argument [~2m]

### Engine/Source/Graphics/Objects/Pipeline.h
- Remove `kSamplerNearestBorder = 0x0200` from `DescriptorFlags` enum at line 20 — flag is defined and handled in TextureManager::GetSampler but never used in any pipeline descriptor setup across Engine or Projects [~5m]

### Engine/Source/Graphics/Managers/TextureManager.h
- Remove `VkSampler mVkSamplerNearestBorder` member at line 53 [~2m]

### Engine/Source/Graphics/Managers/TextureManager.cpp
- Remove sampler create/destroy for `mVkSamplerNearestBorder` at lines 284-285, 383-384 [~2m]
- Remove `kSamplerNearestBorder` branch from `GetSampler` at lines 413-415 [~2m]

## Verification Notes
- All file paths and line numbers verified against source as of 2026-03-18.
- `kSamplerNearestBorder` confirmed unused: searched Engine/Source/ and Projects/ for any descriptor setup referencing this flag, zero matches found.
- The static `RecordEndRenderPass` is called in two places: the instance method at Texture.cpp:627 and CommandBufferManager.cpp:669. The instance-method callers (e.g. `mShadowElevationTexture.RecordEndRenderPass(...)`) go through the instance method, not the static, so they are unaffected.
