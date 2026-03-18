# Tech Debt: Code Duplication

Source: /external-tech-debt on Engine/Source/Graphics/Managers

## Changes

### Engine/Source/Graphics/Managers/DynamicPipelines.cpp
- Lines 256-462: Six smoke/wind pipeline creation functions (CreatePipelineSmokeAxisAligned, CreatePipelineSmoke, CreatePipelineWindDepositA/B, CreatePipelineWindDepositAxisAlignedA/B) are near-identical, differing only in shader CRC, texture target, and buffer index. Extract a shared helper that accepts these as parameters. Note: B variants skip CreateDynamicBuffer, so the helper needs an optional buffer creation parameter [~1h]

### Engine/Source/Graphics/Managers/BufferManager.cpp
- Constructor (lines 73-123) and CreateSwapchainDependentBuffers() (lines 283-397) duplicate buffer initialization for mGlobalLayoutUniformBuffers, mMainLayoutUniformBuffers, mTextStorageBuffers, MeshData, and JointMatrices. Extract shared initialization into helper methods called from both paths [~30m]

### Engine/Source/Graphics/Managers/TextureManager.cpp
- Lines 304-385: Six consecutive vkCreateSampler() calls with nearly identical VkSamplerCreateInfo, varying only in address modes and filter types. Extract a sampler creation helper that takes the varying parameters [~15m]

### Engine/Source/Graphics/Managers/RenderTargetTextures.cpp
- Lines 61-80, 200-225, 430-496: TextureInfo structs created with mostly identical fields (mipLevels=1, arrayLayers=1, samples=1, etc.) differing only in extent and format. Extract a factory helper for common defaults [~15m]

### Engine/Source/Graphics/Managers/PipelineManager.cpp
- Lines 245-295: Shadow blur pipeline pairs (ShadowBlurH/V, ObjectShadowsBlurH/V) have near-identical descriptor arrays with only texture/buffer references changing. Consolidate into a parameterized shadow blur pipeline creator [~30m]

## Verification Notes
- DynamicPipelines.cpp: B variants skip CreateDynamicBuffer — helper needs optional buffer param
- TextureManager.cpp sampler creation: Code already partially deduplicates via struct mutation; savings are ~20 lines. Lowest priority item
- RenderTargetTextures.cpp TextureInfo: Designated initializer reuse is idiomatic Vulkan; a factory helper may add more complexity than it saves. Consider skipping if the helper signature becomes unwieldy
- BufferManager.cpp duplication: Do this BEFORE the file split in Architecture_FileSplits.md — extracted helpers will define natural split boundaries
