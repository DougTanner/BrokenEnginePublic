# Managers - Vulkan Renderer Manager Singletons

## Overview

Singleton managers for the Vulkan renderer, each accessed via a `gp*` global. Created in strict dependency order during Graphics construction and destroyed in reverse via RAII. Support resource recreation for window resize and device-lost recovery.

## Conventions

- **Singleton wiring**: ctor assigns `gp*` global and wraps work in `ScopedBootTimer`; dtor nulls the global. Every Vulkan object is labeled via `VkName()`.
- **Two-phase resize**: paired `Destroy*/Create*` methods tear down and rebuild swapchain/screen-dependent resources without destroying the manager instance.
- **Manager vs. sub-object**: only classes with `gp*` globals are managers; owned-by-value structs are reached through their manager.
- **TextureUploadManager lifecycle**: `InitTransferResources` / `DestroyTransferResources` / `StartThread` are split from ctor/dtor so the transfer thread and persistent staging survive swapchain recreation.

## Managers

- **InstanceManager / DeviceManager** - Instance, physical/logical device, queues, VMA, descriptor pool, pipeline cache
- **SwapchainManager** - Swapchain, framebuffers, main render pass, async presentation worker
- **CommandBufferManager** - Record-once primary buffers (Global/Main/ImGui), submission workers, cross-queue semaphore sync
- **BufferManager** - GPU buffer lifecycle, auto-resize dynamic storage, per-frame skinning, hierarchical dispatch
- **TextureManager / TextureUploadManager** - Texture lifecycle, samplers, bindless descriptors, render targets; uploads run on the dedicated transfer queue with a fixed-byte staging budget and graphics-queue ownership acquire. Owns a permanent neutral programmatic placeholder bound at island slot 0 (format-matched uncompressed textures used as the fallback for every island mint and for evicted templates)
- **PipelineManager** - SPIR-V load and pipeline creation. Fixed `kPipeline*` enum for engine-owned passes; CRC-keyed per-collection pipelines live in `DynamicPipelines` and register at collection init
- **ParticleManager** - GPU particle compute
- **TextManager** - Font rendering and layout
- **ImGuiManager** - Split `Prepare()` / `Submit()`; registers opaque UI rects for depth pre-pass occlusion

## Architecture Notes

- Initialization order is strict; violations crash or trigger validation errors.
- Single descriptor pool serves all pipelines; global Set 0 is shared, Sets 1/2 are per-pipeline. Texture slots are monotonic and descriptor writes are deferred until `UpdateTextureArrayDescriptors()`.
- Semaphore chain: acquire -> Global -> Main -> ImGui -> present.
- **Dual pipeline-creation-site invariant**: `PipelineManager.cpp` builds each pipeline's descriptor-info list in two places (constructor and `RecreatePipelineGroups`); edits must be mirrored or drift surfaces as VUID-vkCmdDrawIndexed-None-08114.

## See Also

- [Graphics Pipeline diagram](../../../../Documents/Architecture/GraphicsPipeline.md)
</content>
</invoke>