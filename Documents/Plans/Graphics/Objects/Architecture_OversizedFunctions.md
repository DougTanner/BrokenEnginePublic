# Architecture: Oversized Functions

Source: /external-architecture-review on Engine/Source/Graphics/Objects

## Changes

### Engine/Source/Graphics/Objects/PipelineCreator.cpp (594 lines)
- `CreateGraphicsPipeline()` is 473 lines (lines 43-516). Split into helper functions within the same file:
  - Extract indirect buffer setup (lines 235-270) into a static `SetupIndirectBuffer()` helper [~15m]
  - Extract descriptor set layout creation from shader reflection (lines 272-402) into a static `CreateDescriptorSetLayouts()` helper [~30m]
  - Extract render state configuration (lines 404-516) into a static `ConfigureGraphicsState()` helper [~20m]
  - Keep `CreateGraphicsPipeline()` as a ~80-line orchestrator [~10m]

### Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp (581 lines)
- `Write()` is 231 lines (lines 264-495) with a large per-descriptor-type if-else tree
  - Extract the inner descriptor processing loop (lines 324-478) into a static `WriteDescriptorForBinding()` helper [~30m]
  - This keeps `Write()` as a ~60-line orchestrator handling framebuffer iteration and allocation [~10m]

### Engine/Source/Graphics/Objects/Texture.cpp (630 lines)
- `Create()` is 262 lines (lines 125-387) bundling three responsibilities:
  - Extract staging buffer data upload (lines 193-258) into a private `UploadImageData()` helper, reusable by `UpdateData()` which has near-identical logic (lines 396-454) [~30m]
  - Extract render target creation (lines 260-378) into a private `CreateRenderTarget()` helper [~20m]
  - Keep `Create()` as a ~60-line orchestrator [~10m]
- `TransitionImageLayout()` is 135 lines (lines 483-618) — two large switch statements mapping `TextureLayout` to Vulkan flags. Could be table-driven with a static lookup array, reducing to ~40 lines [~30m]

## Verification Notes
- All file paths and line numbers verified against source as of 2026-03-18.
- PipelineCreator.cpp is 594 lines total (593 code + namespace close), confirmed over the 500-line guideline.
- PipelineDescriptorWriter.cpp is 581 lines total, also over the 500-line guideline.
- Texture.cpp is 630 lines total, also over the 500-line guideline.
- The `UploadImageData()` extraction also resolves the staging buffer duplication identified in TechDebt_Duplication. Key difference between `Create` and `UpdateData` upload paths: `Create` transitions from `kUndefined` to `kTransferDestination` then conditionally to `mInfo.eTextureLayout`, while `UpdateData` transitions from `kShaderReadOnly` to `kTransferDestination` then back to `kShaderReadOnly`. The helper should accept old/new layout parameters.
- `TransitionImageLayout` table-driven refactor: each `TextureLayout` enum value maps to a fixed triple of `(VkImageLayout, VkAccessFlags, VkPipelineStageFlags)`. A `constexpr` lookup array indexed by the enum would eliminate both switch statements. Note `kGeneral` only appears in the destination switch — the table entry for source would need a sentinel or assert.
