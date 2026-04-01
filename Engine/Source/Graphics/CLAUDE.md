# Graphics - Vulkan Rendering System

## Overview

Multi-pass deferred Vulkan renderer with lighting, shadows, GPU particles, and post-processing. Entirely client-only (`#ifdef BT_CLIENT`). Managers are initialized in strict dependency order and accessed via global pointers (e.g., `gpGraphics`, `gpTextureManager`).

## Key Classes

- **Graphics** (`gpGraphics`) - Central orchestrator owning all managers and coordinating the three-submission render loop (Global, Main, ImGui). Handles swapchain recreation and device-lost recovery via ordered resource cascade
- **CameraBase** - Abstract base for view/projection matrices and visible-area culling. Holds engine-level camera state (eye position/normal, shake intensity, frame counter, sun angle) and exposes a virtual `SunAngle()` for game-specific overrides. Games inherit and provide their own camera (e.g., `game::gpCamera`)
- **Islands** (`gpIslands`) - GPU terrain quad rendering via storage buffer, synchronized with the active multi-frame grid. Terrain collision queries are in `/Frame/IslandTerrain` (shared by both builds)
- **AnimationData** - Runtime skeletal animation with zero-copy pack file loading, topological-order world matrix evaluation, and step/linear/cubicspline keyframe interpolation
- **OneShotCommandBuffer** - Immediate-mode GPU command utility with fence synchronization for one-time operations
- **Screenshot** - `SaveScreenshot` free function for async JPEG capture from the swapchain
- **GraphicsUtils** - Vulkan error handling (`CHECK_VK`), debug naming (`VkName`), `DeviceLostException`, and shared collection rendering helpers (visibility testing, projection, quad building)

## Architecture Notes

- Managers are created in strict dependency order: InstanceManager, DeviceManager, SwapchainManager, CommandBufferManager, BufferManager, Islands, TextureManager, TextManager, PipelineManager, ParticleManager, ImGuiManager
- Three GPU submissions per frame synchronized via semaphores, with the fence signaled by the final submission
- Record-once command buffers resubmitted every frame; re-recorded only on resize or settings change
- Per-framebuffer descriptor sets prevent GPU conflicts across frames in flight
- Resource recreation uses cascading `DestroyType` levels (each includes all lower levels): `kCommandBuffers` → `kSamplers` → `kPipelines` → `kSwapchain` → `kSurface`. Fine-grained per-subsystem recreation uses `DestroyFlags_t` bitmask (terrain, shadows, lighting, smoke, water, etc.)
- All GPU memory allocated through VMA (VmaAllocator in DeviceManager)
- Lazy texture loading: white placeholder at startup, background disk load, transfer queue upload, deferred descriptor update
- `Render/` subdirectory contains GPU uniform buffer population split by subsystem (global, main, lighting, smoke, wind)

## Logging

Graphics subsystem logging uses the `kLogGraphics` category. Use `kVerbose` for per-frame events, `kWarning` for recoverable issues, and `kError` for failures.

## See Also

- [Managers/CLAUDE.md](Managers/CLAUDE.md) - Individual manager implementations and Vulkan patterns
- [Objects/CLAUDE.md](Objects/CLAUDE.md) - RAII wrappers for Vulkan resources
- [Render/CLAUDE.md](Render/CLAUDE.md) - Per-subsystem GPU uniform buffer population
