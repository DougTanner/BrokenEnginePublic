# Managers - Vulkan Renderer Manager Singletons

## Overview

Singleton manager classes for the Vulkan renderer, each accessed via a global pointer (e.g., `gpTextureManager`). All managers are created in strict dependency order during Graphics construction and destroyed in reverse order via RAII. They support resource recreation for window resize and device-lost recovery.

See also: [Graphics Pipeline](../../../../Documents/Architecture/GraphicsPipeline.md)

## Key Classes

- **InstanceManager** (`gpInstanceManager`) - Vulkan instance creation and physical device selection
- **DeviceManager** (`gpDeviceManager`) - Logical device, queues, descriptor pool, VmaAllocator, and pipeline cache (loaded from disk on startup, saved on shutdown with device-UUID validation)
- **SwapchainManager** (`gpSwapchainManager`) - Swapchain, framebuffers, depth textures, synchronization
- **CommandBufferManager** (`gpCommandBufferManager`) - Command pool and buffer management (Global/Main types)
- **BufferManager** (`gpBufferManager`) - GPU buffer creation (vertex, index, uniform, storage)
- **TextureManager** (`gpTextureManager`) - Texture lifecycle, samplers, and delegates to sub-objects: TextureDescriptors (bindless descriptor Set 0), TextureCache (GPU readback, file caching, BRDF LUT), RenderTargetTextures (shadows, lighting, smoke, wind)
- **TextureUploadManager** (`gpTextureUploadManager`) - Background GPU texture uploads on a dedicated transfer queue
- **TextManager** (`gpTextManager`) - Font rendering and text layout
- **PipelineManager** (`gpPipelineManager`) - SPIR-V shader loading, graphics/compute pipeline creation; delegates dynamic per-collection pipelines to DynamicPipelines sub-object
- **ParticleManager** (`gpParticleManager`) - GPU particle system with compute shaders
- **ImGuiManager** (`gpImGuiManager`) - Dear ImGui UI rendering (menus, dialogs, HUD)

## Architecture Notes

- Initialization order is strict: Instance, Device, Swapchain, CommandBuffer, Buffer, Texture, Text, Islands, Pipeline, Particle, ImGui. Violating this crashes or causes validation errors
- Fence wait required before all GPU resource updates to avoid modifying in-use resources
- Single descriptor pool in DeviceManager serves all pipelines; global Set 0 owned by TextureDescriptors, per-pipeline Sets 1 and 2
- Per-framebuffer resource duplication enables parallel frame processing
- Semaphore chain: Image acquisition, Global, Main, ImGui, Presentation

## See Also

- Individual manager docs: [BufferManager](BufferManager.CLAUDE.md) | [CommandBufferManager](CommandBufferManager.CLAUDE.md) | [DeviceManager](DeviceManager.CLAUDE.md) | [ImGuiManager](ImGuiManager.CLAUDE.md) | [InstanceManager](InstanceManager.CLAUDE.md) | [ParticleManager](ParticleManager.CLAUDE.md) | [PipelineManager](PipelineManager.CLAUDE.md) | [SwapchainManager](SwapchainManager.CLAUDE.md) | [TextManager](TextManager.CLAUDE.md) | [TextureManager](TextureManager.CLAUDE.md) | [TextureUploadManager](TextureUploadManager.CLAUDE.md)
