# Refactor: Simplifications

Source: /external-refactor-clean on Engine/Source/Graphics/Objects/PipelineCreator.cpp, Texture.cpp

## Changes

### Engine/Source/Graphics/Objects/PipelineCreator.cpp
- Extract shared pipeline layout creation boilerplate into a static `CreateSingleSetPipelineLayout()` helper — the single-set descriptor layout + pipeline layout creation at lines 386-402 (graphics fallback path) and lines 563-581 (compute path) are near-identical; both filter bindings, call ConfigureUpdateAfterBind, create descriptor set layout, set up push constant range, and create pipeline layout. Helper needs a `VkShaderStageFlags` parameter since graphics uses `VERTEX_BIT | FRAGMENT_BIT` and compute uses `COMPUTE_BIT` for the push constant range [~20m]
- Extract indirect buffer command buffer count computation (lines 238-240 and 266-268) into a local variable before the if/else — same 3-line `gpSwapchainManager->mFramebuffers.size()` / `std::max(count, 3)` / `sizeof(VkDrawIndexedIndirectCommand)` pattern duplicated between kIndirectHostVisible and kIndirectDeviceLocal branches [~5m]

### Engine/Source/Graphics/Objects/Texture.h
- Replace `bool bDepth, bool bMultisampling, bool bClear` parameters on static `RecordBeginRenderPass` (line 71) with a `common::Flags<RenderPassBeginFlags>` enum — engine pattern prefers flags over multiple bool parameters. Define `enum class RenderPassBeginFlags : uint8_t { kDepth = 0x01, kMultisampling = 0x02, kClear = 0x04 }` [~15m]

### Engine/Source/Graphics/Objects/Texture.cpp
- Update static `RecordBeginRenderPass` implementation (line 9) and instance method caller (line 622) to use the new flags type [~5m]

### Engine/Source/Graphics/Managers/CommandBufferManager.cpp
- Update the one caller of `Texture::RecordBeginRenderPass` static at line 608 to pass flags instead of bools [~10m]

## Verification Notes
- All file paths and line numbers verified against source as of 2026-03-18.
- The `CreateSingleSetPipelineLayout()` extraction has a key difference: the graphics fallback path (lines 386-402) reuses a pre-existing `vkPushConstantRange` declared at line 308 with `VERTEX_BIT | FRAGMENT_BIT`, while the compute path (lines 575-578) creates its own with `COMPUTE_BIT`. The helper must accept `VkShaderStageFlags` for the push constant stage.
- The compute path at line 563 also differs slightly: it uses its own `uniformTextureVkDescriptorSetLayoutCreateInfo` local variable (line 520) vs the graphics one (line 48). These are structurally identical, so the helper can take the binding array and count as parameters.
- For `RecordBeginRenderPass` flags: only two call sites exist — one static call at CommandBufferManager.cpp:608 and one instance method at Texture.cpp:622 that delegates to the static. The instance method at line 622 constructs flags from `mInfo.textureFlags & kDepth`, `mInfo.textureFlags & kMultisampling && gMultisampling.Get<bool>()`, and `mInfo.renderPassVkAttachmentLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR`, which maps cleanly to the new flags type.
