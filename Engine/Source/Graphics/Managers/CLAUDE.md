# Managers - Vulkan Renderer Manager Singletons

## Overview

Singleton manager classes for the Vulkan renderer, each accessed via a global pointer (e.g., `gpTextureManager`). All managers are created in strict dependency order during Graphics construction and destroyed in reverse order via RAII. They support resource recreation for window resize and device-lost recovery.

See also: [Graphics Pipeline](../../../../Documents/Architecture/GraphicsPipeline.md)

## Key Classes

- **InstanceManager** (`gpInstanceManager`) - Vulkan instance creation and physical device selection
- **DeviceManager** (`gpDeviceManager`) - Logical device, queues, descriptor pool, VmaAllocator, pipeline cache (loaded from disk on startup, saved on shutdown with device-UUID validation), and shared one-shot command pool/fence used by `OneShotCommandBuffer`
- **SwapchainManager** (`gpSwapchainManager`) - Swapchain, framebuffers, depth/multisampling textures, main render pass, image acquisition/presentation synchronization (semaphores, fences), and `PersistentWorker` for async presentation
- **CommandBufferManager** (`gpCommandBufferManager`) - Command pool and buffer management (Global/Main/ImGui), record-once command buffers, submission via `PersistentWorker` threads, and particle compute synchronization via semaphore. The lighting spread section dispatches per-pass-per-color pipelines in a nested loop, then a single accumulate pipeline loop (per spread index, push-constant-driven), then a single combine dispatch
- **BufferManager** (`gpBufferManager`) - GPU buffer lifecycle (vertex, index, uniform, storage), dynamic storage buffers with auto-resize per collection, per-frame skinning allocations (mesh data + joint matrices with auto-grow), and hierarchical dispatch buffers for smoke/wind/lighting-spread compute
- **TextureManager** (`gpTextureManager`) - Texture lifecycle, samplers, and delegates to sub-objects: TextureDescriptors (bindless descriptor Set 0), TextureCache (GPU readback, file caching, BRDF LUT), RenderTargetTextures (shadows, smoke, wind, terrain in `RenderTargetTextures.cpp`; lighting MRT framebuffer, per-pass spread textures, accumulate textures, and a debug texture pointer array in `RenderTargetTexturesLighting.cpp`)
- **TextureUploadManager** (`gpTextureUploadManager`) - Background GPU texture uploads on a dedicated transfer queue
- **TextManager** (`gpTextManager`) - Font rendering and text layout
- **PipelineManager** (`gpPipelineManager`) - SPIR-V shader loading, graphics/compute pipeline creation; delegates dynamic per-collection pipelines to `DynamicPipelines` sub-object. Maintains pipeline arrays for the lighting pipeline phases: single occupancy dilation pipeline, per-pass-per-color spread pipelines (source sampler → independent spread texture), and a single accumulate pipeline covering all 3 color channels (spread texture arrays bound as descriptor arrays, spread index passed via push constant). The combine pipeline also handles all 3 color channels in one dispatch. `DynamicPipelines::UpdateAllModelPipelineDescriptors` fans out descriptor updates across all model and shadow pipeline maps, called by `BufferManager` after skinning buffer growth. The debug texture pipeline uses a descriptor array of all lighting textures; texture selection is driven by a uniform index so command buffers need not be re-recorded when cycling between textures
- **ParticleManager** (`gpParticleManager`) - GPU particle system with compute shaders
- **ImGuiManager** (`gpImGuiManager`) - Dear ImGui UI rendering (menus, dialogs, HUD)

## Architecture Notes

- Initialization order is strict: Instance, Device, Swapchain, CommandBuffer, Buffer, Islands, Texture, Text, Pipeline, Particle, ImGui. Violating this crashes or causes validation errors
- Fence wait required before all GPU resource updates to avoid modifying in-use resources
- Single descriptor pool in DeviceManager serves all pipelines; global Set 0 owned by TextureDescriptors, per-pipeline Sets 1 and 2
- Per-framebuffer resource duplication enables parallel frame processing
- Semaphore chain: Image acquisition, Global, Main, ImGui, Presentation

## See Also

- Individual manager docs: [BufferManager](BufferManager.CLAUDE.md) | [CommandBufferManager](CommandBufferManager.CLAUDE.md) | [DeviceManager](DeviceManager.CLAUDE.md) | [ImGuiManager](ImGuiManager.CLAUDE.md) | [InstanceManager](InstanceManager.CLAUDE.md) | [ParticleManager](ParticleManager.CLAUDE.md) | [PipelineManager](PipelineManager.CLAUDE.md) | [SwapchainManager](SwapchainManager.CLAUDE.md) | [TextManager](TextManager.CLAUDE.md) | [TextureManager](TextureManager.CLAUDE.md) | [TextureUploadManager](TextureUploadManager.CLAUDE.md)
