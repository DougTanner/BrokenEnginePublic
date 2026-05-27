# Managers - Vulkan Renderer Manager Singletons

## Overview

Singleton managers for the Vulkan renderer, each accessed via a `gp*` global. Created in strict dependency order during Graphics construction and destroyed in reverse via RAII. Support resource recreation for window resize and device-lost recovery.

## Conventions

- **Singleton wiring**: ctor assigns `gp*` global and wraps work in `ScopedBootTimer`; dtor nulls the global. Every Vulkan object is labeled via `VkName()`.
- **Two-phase resize**: paired `Destroy*/Create*` methods tear down and rebuild swapchain/screen-dependent resources without destroying the manager instance.
- **Manager vs. sub-object**: only classes with `gp*` globals are managers; owned-by-value structs are reached through their manager.
- **TextureUploadManager lifecycle**: `InitTransferResources` / `DestroyTransferResources` / `StartThread` are split from ctor/dtor so the transfer thread and persistent staging survive swapchain recreation.

## Managers

- **InstanceManager / DeviceManager** - Instance, physical/logical device, queues, VMA, descriptor pool, pipeline cache. Enabled features include `drawIndirectFirstInstance` (terrain emits one indirect draw per island template, each with a distinct `firstInstance`).
- **SwapchainManager** - Swapchain, framebuffers, main render pass, async presentation worker
- **CommandBufferManager** - Record-once primary buffers (Global/Main/ImGui), submission workers, cross-queue semaphore sync
- **BufferManager** - GPU buffer lifecycle, auto-resize dynamic storage, per-frame skinning, hierarchical dispatch
- **TextureManager / TextureUploadManager** - Texture lifecycle, samplers, bindless descriptors, render targets; uploads run on the dedicated transfer queue with a fixed-byte staging budget and graphics-queue ownership acquire. Owns a permanent neutral programmatic placeholder bound at island slot 0 (format-matched uncompressed textures used as the fallback for every island mint and for evicted templates). Samplers that bind R32_SFLOAT views (heightmaps, smoke ping-pong) are built at creation time with their filter mode downgraded to NEAREST when the device does not advertise `SAMPLED_IMAGE_FILTER_LINEAR_BIT` for the format, so pipelines do not branch on device support. `TextureDescriptors::UpdateArrayBindingsForKey` is the array-only sibling of the CRC-keyed update path, used by `IslandTerrain` to patch elevation array slots whose Texture is template-owned (lives on `IslandTemplate::mElevationTexture`, with no `mTextureMap` entry).
- **PipelineManager** - SPIR-V load and pipeline creation. Fixed `kPipeline*` enum for engine-owned passes; CRC-keyed per-collection pipelines live in `DynamicPipelines` and register at collection init
- **ParticleManager** - GPU particle compute
- **TextManager** - Font rendering and layout
- **ImGuiManager** - Split `Prepare()` / `Submit()`; registers opaque UI rects for depth pre-pass occlusion

## Architecture Notes

- Initialization order is strict; violations crash or trigger validation errors.
- Single descriptor pool serves all pipelines (graphics and compute); global Set 0 is shared across both, with per-pipeline sets above it (graphics: Sets 1/2; compute: optional Set 1). Island texture slots are reclaimed on eviction via a free-list and reused by the next mint (bounded by `kiMaxIslands`), no longer strictly monotonic; descriptor writes are deferred until `UpdateTextureArrayDescriptors()`. **Eviction symmetry invariant**: every slot teardown that frees a slot's GPU `VkImageView`s must also rewrite the per-pipeline Set-1 array element at that index back to the slot-0 placeholder (re-reading the live array pointer across all consumers of that bindless array) — not just the Set-0 `mImageInfos` entry and the slot pointers. Otherwise a recycled slot keeps the prior occupant's destroyed view live in Set 1 and samples it before its next occupant patches real data, a GPU use-after-free.
- Semaphore chain: acquire -> Global -> Main -> ImGui -> present.
- **Pipeline recreation invariant**: pipeline-tier destroy events (`meDestroyType >= kPipelines`) drop and reconstruct the entire `PipelineManager` via `mpPipelineManager.reset()`; the constructor is the single canonical pipeline-build site. Static pipelines rebuild in the constructor; dynamic pipelines repopulate lazily through collection per-frame render code. Descriptor staleness across recreate events is asserted at command-buffer record time by `PipelineManager::VerifyAllDescriptorGenerations` walking `TextureBindingEntry` snapshot generations. Per-island-slot bindings are verified only at their single owned array element (the one the slot writer keeps current); neighbouring elements of the same array legitimately go stale as slots evict (`Texture::Destroy` nulls `mVkImage` without bumping the generation), so verifying them is a false positive. Full-array bindings still verify every element.

## See Also

- [Graphics Pipeline diagram](../../../../Documents/Architecture/GraphicsPipeline.md)
</content>
</invoke>