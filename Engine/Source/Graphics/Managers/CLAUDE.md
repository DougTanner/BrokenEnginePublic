# Managers - Vulkan Renderer Manager Singletons

## Overview

Singleton managers for the Vulkan renderer, each accessed via a `gp*` global. Created in strict dependency order during Graphics construction and destroyed in reverse via RAII. Support resource recreation for window resize and device-lost recovery.

## Conventions

- **Singleton wiring**: ctor assigns the `gp*` global (most wrap their work in `ScopedBootTimer`); dtor nulls it. Every Vulkan object is labeled via `VkName()`.
- **Two-phase resize**: paired `Destroy*/Create*` methods tear down and rebuild swapchain/screen-dependent resources without destroying the manager instance.
- **Manager vs. sub-object**: only classes with `gp*` globals are managers; owned-by-value structs are reached through their manager.
- **TextureUploadManager lifecycle**: `InitTransferResources` / `DestroyTransferResources` / `StartThread` are split from ctor/dtor so the transfer thread and persistent staging survive swapchain recreation.

## Managers

- **InstanceManager / DeviceManager** - Instance, physical/logical device, queues, VMA, descriptor pool, pipeline cache. Enabled features include `drawIndirectFirstInstance` (terrain emits one indirect draw per island template, each with a distinct `firstInstance`). Boot fails loud (ASSERT + error log) if the device does not advertise `COLOR_ATTACHMENT_BLEND_BIT` for the special-format RTTs the MAX/ADD-blended prepasses (elevation/lighting/smoke/wind) target — a hard device dependency, unlike the TextureManager linear-filter probe which downgrades gracefully. A new blended prepass on a novel format must extend that guard.
- **SwapchainManager** - Swapchain, framebuffers, main render pass, async presentation worker
- **CommandBufferManager** - Record-once Global/Main primary buffers and their submission workers; per-pass record bodies live in the `CommandBufferRecordGlobal` / `CommandBufferRecordMain` helper structs. The particle semaphore is cross-frame on the single graphics queue (Main submit signals, the next frame's Global submit waits); the only cross-queue sync in this directory is TextureUploadManager's transfer→graphics ownership transfer
- **BufferManager** - GPU buffer lifecycle, auto-resize dynamic storage, per-frame skinning, hierarchical dispatch
- **TextureManager / TextureUploadManager** - Texture lifecycle, samplers, bindless descriptors, render targets; uploads run on the transfer queue (a transfer-only family when available, else the graphics family) with a fixed-byte staging budget and graphics-queue ownership acquire. Per-island data is exposed as five parallel bindless arrays (elevation / color / normals / AO / masks), every slot initially pointing at the slot-0 placeholder (see [Graphics/CLAUDE.md](../CLAUDE.md)); each array's `.data()` pointer keys the bindless-consumer registry, so the island `Texture*` vectors are never resized after boot. Samplers that bind R32_SFLOAT views (heightmaps, smoke ping-pong) are built at creation time with their filter mode downgraded to NEAREST when the device does not advertise `SAMPLED_IMAGE_FILTER_LINEAR_BIT` for the format, so pipelines do not branch on device support. Delegates to three owned-by-value sub-objects (descriptor management, file cache / readback, render-target textures) reached through the manager, not via their own globals.
- **PipelineManager** - SPIR-V load and pipeline creation. Fixed `kPipeline*` enum for engine-owned passes; CRC-keyed per-collection pipelines live in `DynamicPipelines` and register at collection init. **Trust-boundary validation**: the shader-load loop guards descriptor-binding and vertex-attribute counts against `ShaderHeader::kiMax*` structural maxima and bounds section extents against `ChunkHeader::iSize`; out-of-range throws `common::CorruptStreamException`.
- **ParticleManager** - CPU staging for GPU particles: thread-safe `Spawn()` with visible-area culling into member staging layouts (one per particle type), copied into the per-framebuffer spawn buffers later in `RenderGlobal`; the particle compute/render pipelines live in PipelineManager and are recorded by the CommandBufferRecord structs
- **TextManager** - Font rendering and layout
- **ImGuiManager** - Split `Prepare()` / `Submit()`; `Submit` re-records the ImGui CB every frame (the renderer's only per-frame-recorded CB) and signals the per-framebuffer fence that the Main submit reset — that reset/signal pairing must stay intact. Registers opaque UI rects for depth pre-pass occlusion

## Architecture Notes

- Initialization order is strict; violations crash or trigger validation errors.
- Single descriptor pool serves all pipelines (graphics and compute); global Set 0 is shared across both, with per-pipeline sets above it (graphics: Sets 1/2; compute: optional Set 1). Island texture slots are reclaimed on eviction via a free-list and reused by the next mint (bounded by `kiMaxIslands`); descriptor writes are deferred until `UpdateTextureArrayDescriptors()`. **Eviction symmetry invariant**: every slot teardown that frees a slot's GPU `VkImageView`s must also rewrite the per-pipeline Set-1 array element at that index back to the slot-0 placeholder (re-reading the live array pointer across all consumers of that bindless array) — not just the Set-0 `mImageInfos` entry and the slot pointers. Otherwise a recycled slot keeps the prior occupant's destroyed view live in Set 1 and samples it before its next occupant patches real data, a GPU use-after-free.
- Submission chain: Global -> Main -> ImGui -> present. The image-available (acquire) semaphore is waited by the Main submit, not Global.
- **Bindless-index assignment is lock-free by phase exclusion, not by a mutex.** `TextureDescriptors::CrcToIndex` (and the map it mutates) has two writer phases that never overlap: the worker `ParticleManager::Spawn` path runs inside `RunFrameTick`'s `Dispatch()` fan-out (which fully joins before render), and the main-thread render-path callers run after. The render-path callers assert they are outside frame-tick so a future caller that moves into frame-tick code fails loud rather than silently racing.
- **Descriptor staleness verification** (pipeline-tier recreation itself: [Graphics/CLAUDE.md](../CLAUDE.md) Destroy/Refresh): `PipelineManager::VerifyAllDescriptorGenerations` runs at the top of Global record, walking `TextureBinding` snapshot generations. Per-island-slot bindings are verified only at their single owned array element (the one the slot writer keeps current); neighbouring elements of the same array legitimately go stale as slots evict (`Texture::Destroy` nulls `mVkImage` without bumping the generation), so verifying them is a false positive. Full-array bindings still verify every element.

## See Also

Per-manager deep dives (one leaf `*.CLAUDE.md` per manager in this directory):

- [InstanceManager](InstanceManager.CLAUDE.md) / [DeviceManager](DeviceManager.CLAUDE.md) / [SwapchainManager](SwapchainManager.CLAUDE.md)
- [CommandBufferManager](CommandBufferManager.CLAUDE.md) / [BufferManager](BufferManager.CLAUDE.md)
- [TextureManager](TextureManager.CLAUDE.md) / [TextureUploadManager](TextureUploadManager.CLAUDE.md) / [PipelineManager](PipelineManager.CLAUDE.md)
- [ParticleManager](ParticleManager.CLAUDE.md) / [TextManager](TextManager.CLAUDE.md) / [ImGuiManager](ImGuiManager.CLAUDE.md)