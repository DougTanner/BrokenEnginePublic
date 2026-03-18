# Tech Debt: Duplication

Source: /external-tech-debt on Engine/Source/Graphics/Objects

## Changes

### Engine/Source/Graphics/Objects/Buffer.cpp
- Refactor move constructor (lines 142-160) and move assignment operator (lines 162-188) to share the member-nulling logic via `std::exchange` in both paths, eliminating the duplicated 8-line nulling block [~10m]

### Engine/Source/Graphics/Objects/Pipeline.cpp
- Extract a helper for push constants setup (creating `PushConstantsLayout`, calling `vkCmdPushConstants`) — currently duplicated at lines 175-181, 203-209, 237-243, 256-262, 274-280 across `RecordDraw`, `RecordDrawIndirect`, `RecordDrawIndirectSet2`, `RecordCompute`, `RecordComputeIndirect`. Helper needs a `VkShaderStageFlags` parameter since graphics uses `VERTEX_BIT | FRAGMENT_BIT` while compute uses `COMPUTE_BIT` [~15m]
- Extract a helper for descriptor set binding logic (check `mVkExternalDescriptorSetLayout`, bind 1 or 2 sets) — duplicated between `RecordDraw` (lines 183-193) and `RecordDrawIndirect` (lines 211-220) [~10m]

### Engine/Source/Graphics/Objects/Texture.cpp
- Extract a helper for VkImageView creation with BC4 component swizzle — duplicated between `AdoptTransferredImage` (lines 77-94) and `Create` (lines 174-191) [~15m]

### Engine/Source/Graphics/Objects/PipelineCreator.cpp
- Name the depth bias magic numbers `-3.0f` at lines 470, 472 as named constants (e.g. `kfDepthBiasConstantFactor`, `kfDepthBiasSlopeFactor`) [~5m]

## Verification Notes
- All file paths and line numbers verified against source as of 2026-03-18.
- Removed "staging buffer mip-level upload" item from this plan because it duplicates the Architecture_OversizedFunctions plan's `UploadImageData()` extraction (same refactoring, described there with more context).
- Push constants helper caveat: graphics and compute paths use different `VkShaderStageFlags`, so the helper must accept a stage parameter.
- Buffer move semantics: the move constructor can use `std::exchange` in its member initializer list (matching Texture's existing pattern at Texture.h:79-85), while the move assignment body can similarly use `std::exchange` to eliminate the separate nulling block.
