# Graphics - Vulkan Rendering System

## Overview

Multi-pass deferred Vulkan renderer with lighting, shadows, GPU particles, and post-processing. Entirely client-only (`#ifdef BT_CLIENT`). Managers are initialized in strict dependency order and accessed via global pointers (e.g., `gpGraphics`, `gpTextureManager`).

## Key Classes

- **Graphics** (`gpGraphics`) - Central orchestrator owning all managers and coordinating the three-submission render loop (Global, Main, ImGui). Handles swapchain recreation and device-lost recovery via ordered resource cascade
- **CameraBase** (`gpCamera`) - Abstract base for view/projection matrices and visible-area culling. Games inherit and provide their own camera
- **Islands** (`gpIslands`) - GPU terrain quad rendering via storage buffer, synchronized with the active multi-frame grid. Terrain collision queries are in `/Frame/IslandTerrain` (shared by both builds)
- **AnimationData** - Runtime skeletal animation with zero-copy pack file loading, topological-order world matrix evaluation, and step/linear/cubicspline keyframe interpolation
- **OneShotCommandBuffer** - Immediate-mode GPU command utility with fence synchronization for one-time operations
- **Screenshot** - Async JPEG capture on a dedicated thread (conditional on `ENABLE_SCREENSHOTS`)
- **GraphicsUtils** - Vulkan error handling (`CHECK_VK`), debug naming (`VkName`), `DeviceLostException`, and shared rendering helpers

## Architecture Notes

- Managers are created in strict dependency order: InstanceManager, DeviceManager, SwapchainManager, CommandBufferManager, BufferManager, TextureManager, TextManager, Islands, PipelineManager, ParticleManager, ImGuiManager
- Three GPU submissions per frame synchronized via semaphores, with the fence signaled by the final submission
- Record-once command buffers resubmitted every frame; re-recorded only on resize or settings change
- Per-framebuffer descriptor sets prevent GPU conflicts across frames in flight
- Resource recreation uses cascading `DestroyType` levels (each includes all lower levels): `kCommandBuffers` (settings change) → `kSamplers` (anisotropy/mip bias) → `kPipelines` (wireframe/sample shading) → `kSwapchain` (window resize) → `kSurface` (device lost)
- All GPU memory allocated through VMA (VmaAllocator in DeviceManager)
- Lazy texture loading: white placeholder at startup, background disk load, transfer queue upload, deferred descriptor update
- `Render/` subdirectory contains GPU uniform buffer population split by subsystem (global, main, lighting, smoke, wind)

## See Also

- [Managers/CLAUDE.md](Managers/CLAUDE.md) - Individual manager implementations and Vulkan patterns
- [Objects/CLAUDE.md](Objects/CLAUDE.md) - RAII wrappers for Vulkan resources
