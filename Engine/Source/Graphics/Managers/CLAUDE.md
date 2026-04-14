# Managers - Vulkan Renderer Manager Singletons

## Overview

Singleton manager classes for the Vulkan renderer, each accessed via a global pointer (e.g., `gpTextureManager`). All managers are created in strict dependency order during Graphics construction and destroyed in reverse order via RAII. They support resource recreation for window resize and device-lost recovery.

See also: [Graphics Pipeline](../../../../Documents/Architecture/GraphicsPipeline.md)

## Key Classes

- **InstanceManager** (`gpInstanceManager`) - Vulkan instance creation and physical device selection
- **DeviceManager** (`gpDeviceManager`) - Logical device, queues, descriptor pool, VmaAllocator, pipeline cache (loaded from disk on startup, saved on shutdown with device-UUID validation), and shared one-shot command pool/fence used by `OneShotCommandBuffer`
- **SwapchainManager** (`gpSwapchainManager`) - Swapchain, framebuffers, depth/multisampling textures, main render pass, image acquisition/presentation synchronization (semaphores, fences), and `PersistentWorker` for async presentation
- **CommandBufferManager** (`gpCommandBufferManager`) - Command pool and buffer management (Global/Main/ImGui), record-once command buffers, submission via `PersistentWorker` threads, and particle compute synchronization via semaphore. The lighting section dispatches N radial spread fragment passes (each reading the previous pass's output into its own set of spread textures), then a single combine dispatch that sums all spread pass outputs
- **BufferManager** (`gpBufferManager`) - GPU buffer lifecycle (vertex, index, uniform, storage), dynamic storage buffers with auto-resize per collection, per-frame skinning allocations (mesh data + joint matrices with auto-grow), and hierarchical dispatch buffers for smoke/wind/lighting-spread compute
- **TextureManager** (`gpTextureManager`) - Texture lifecycle, samplers, and delegates to sub-objects: TextureDescriptors (bindless descriptor Set 0), TextureCache (GPU readback, file caching, BRDF LUT), RenderTargetTextures (shadows, smoke, wind, terrain in `RenderTargetTextures.cpp`; lighting MRT deposit framebuffer, per-pass spread texture sets (one set of 3 textures per spread pass with sizes linearly interpolated between start and end multiplier values across the pass count), per-pass spread framebuffers, and a debug texture pointer array in `RenderTargetTexturesLighting.cpp`)
- **TextureUploadManager** (`gpTextureUploadManager`) - Background GPU texture uploads on a dedicated transfer queue
- **TextManager** (`gpTextManager`) - Font rendering and text layout
- **PipelineManager** (`gpPipelineManager`) - SPIR-V shader loading, graphics/compute pipeline creation; delegates dynamic per-collection pipelines to `DynamicPipelines` sub-object. Maintains pipeline arrays for the lighting pipeline phases: an array of radial spread fragment pipelines (one per spread pass, each MRT to its own set of spread textures), and a combine pipeline (tone map all 3 channels in one dispatch, summing all spread pass outputs). `DynamicPipelines::UpdateAllModelPipelineDescriptors` fans out descriptor updates across all model and shadow pipeline maps, called by `BufferManager` after skinning buffer growth. The debug texture pipeline uses a descriptor array of all lighting textures; texture selection is driven by a uniform index so command buffers need not be re-recorded when cycling between textures. The debug circle pipeline (`kPipelineDebugCircle`) uses a dedicated billboard vertex shader so circles always face the camera; all other debug primitives share the world-space debug vertex shader. `kPipelineUiDepthPrepass` is a depth-only pipeline (`kDepthTest | kDepthWrite | kNoColorWrite`) that draws procedural quads (no vertex buffer) at z=0 for opaque UI occlusion culling
- **ParticleManager** (`gpParticleManager`) - GPU particle system with compute shaders
- **ImGuiManager** (`gpImGuiManager`) - Dear ImGui UI rendering (menus, dialogs, HUD). Frame work is split into `Prepare()` (runs ImGui logic and collects draw data, called before the main render pass) and `Submit()` (records and submits the ImGui command buffer). Screens call `RegisterOpaqueRect()` to register UI window bounds for the depth pre-pass; these are uploaded via a host-visible storage buffer and drawn via `vkCmdDrawIndirect` using the `kPipelineUiDepthPrepass` pipeline at the start of the main pass when `gOpaqueUi` is enabled. The indirect buffer `instanceCount` is set to 0 when disabled, incurring zero GPU overhead

## Architecture Notes

- Initialization order is strict: Instance, Device, Swapchain, CommandBuffer, Buffer, Islands, Texture, Text, Pipeline, Particle, ImGui. Violating this crashes or causes validation errors
- Fence wait required before all GPU resource updates to avoid modifying in-use resources
- Single descriptor pool in DeviceManager serves all pipelines; global Set 0 owned by TextureDescriptors, per-pipeline Sets 1 and 2
- Per-framebuffer resource duplication enables parallel frame processing
- Semaphore chain: Image acquisition, Global, Main, ImGui, Presentation
- **Dual pipeline-creation-site invariant**: `PipelineManager.cpp` builds each pipeline's descriptor-info list in two places (constructor + `RecreatePipelineGroups`); edits to one MUST be mirrored in the other or drift surfaces as VUID-vkCmdDrawIndexed-None-08114

## See Also

- Individual manager docs: [BufferManager](BufferManager.CLAUDE.md) | [CommandBufferManager](CommandBufferManager.CLAUDE.md) | [DeviceManager](DeviceManager.CLAUDE.md) | [ImGuiManager](ImGuiManager.CLAUDE.md) | [InstanceManager](InstanceManager.CLAUDE.md) | [ParticleManager](ParticleManager.CLAUDE.md) | [PipelineManager](PipelineManager.CLAUDE.md) | [SwapchainManager](SwapchainManager.CLAUDE.md) | [TextManager](TextManager.CLAUDE.md) | [TextureManager](TextureManager.CLAUDE.md) | [TextureUploadManager](TextureUploadManager.CLAUDE.md)
