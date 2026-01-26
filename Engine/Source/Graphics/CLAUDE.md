# `/Engine/Source/Graphics/`

Vulkan-based rendering system orchestrating graphics resources through specialized manager classes.

## Architecture Overview

**Vulkan Function Loading**: Volk meta-loader provides direct driver access
**Rendering**: Multi-pass deferred renderer with lighting, shadows, and post-processing
**Synchronization**: Multiple frames in flight with per-framebuffer command buffers and fences
**Resource Management**: RAII wrappers for all Vulkan objects with automatic cleanup

## Core Classes

### Graphics
**Global**: `gpGraphics`

Central orchestrator that owns all graphics managers and coordinates the render loop.

**Vulkan Initialization**: Constructor calls `volkInitialize()` then verifies Vulkan 1.2 support via `vkEnumerateInstanceVersion()` before any manager creation. Displays MessageBox and throws if Vulkan 1.2 unavailable.

**Manager Initialization**: Creates managers in strict dependency order required by Vulkan resource hierarchy (see Manager Initialization Order below).

**Render Loop** (Async Pipeline):
- `RenderGlobal()`: Wait for fence, process pending texture loads, submit global command buffer (shadows, particles)
- `RenderMainPresentAcquire()`: Update camera, render scene and UI, submit main command buffer, present to screen, acquire next image. Launched asynchronously via `std::future` stored in `mRenderFuture`
- `WaitForRender()`: Blocks until the async render operation completes. Called before starting the next frame's rendering

The Graphics class owns the interpolated frame state (`mFrameInterpolate`) used for smooth rendering between physics ticks.

**Resource Recreation**: Settings changes set `DestroyType` enum and `DestroyFlags` bitflags. `Destroy()` waits for device idle once, then `RecreateResources()` rebuilds only flagged resources to minimize GPU synchronization.

**Frame Tracking**: `miFrameCounter` monotonically increases for VMA memory budget tracking (not cycling framebuffer index).

### CameraBase
Abstract base camera providing view/projection matrix calculation and frustum culling. Game implementations inherit from CameraBase. Global `gpCamera` pointer initialized in Main.cpp points to game-specific camera instance.

### Islands
**Global**: `gpIslands`

Island-based terrain system with CPU heightmaps for collision and GPU textures for rendering. `GlobalElevation()` transforms world position to island-local UV, samples normalized float heightmap (0-1), applies beach/height scaling. `GlobalNormal()` samples 4 surrounding heightmap points via finite differences.

### GltfAnimationData
Runtime skeletal animation system for glTF models. Loads skeleton and animation data from pack files into `gAnimationDataMap` global registry keyed by glTF CRC. `Evaluate()` computes per-joint matrices by interpolating keyframes (linear for translation/scale, slerp for rotation quaternions), building world matrices through parent hierarchy, and applying inverse bind matrices. Joint matrices are uploaded to GPU storage buffer for vertex shader skinning.

### OneShotCommandBuffer
Immediate-mode GPU command utility for one-time operations. Allocates command pool/buffer, records commands, submits with fence synchronization. Used for texture uploads, layout transitions, and initialization operations.

### Screenshot
**Conditional**: `ENABLE_SCREENSHOTS` define

Asynchronous screenshot capture to JPEG. Copies swapchain image to host memory, launches async thread to encode JPEG in Windows temp directory.

### GraphicsUtils
Utility functions for Vulkan development and debugging.

**CheckVk()**: Inline function wrapping Vulkan calls with error handling. Uses `std::source_location` to capture call site information automatically. On failure, calls `CheckVkFailed()` which handles device lost and swapchain recreation by setting `gpGraphics->meDestroyType`, avoiding immediate crashes for recoverable errors.

**VkName()**: Sets debug names on Vulkan objects for identification in validation layers and GPU debugging tools. Uses `if constexpr (kbEnableVulkanDebugLayers)` for compile-time elimination when debug layers are disabled. Names are stored in Graphics::mDebugNames to ensure pointer lifetime for Vulkan's retained reference.

**DeviceLostException**: Exception class thrown when Vulkan device is lost and cannot be recovered.

## Manager Initialization Order

Strict dependency order required for Vulkan resource creation (violating crashes or causes validation errors):

1. **InstanceManager** - VkInstance and physical device selection
2. **DeviceManager** - VkDevice, VkQueue, VkDescriptorPool, VmaAllocator
3. **ShaderManager** - VkShaderModule objects from SPIR-V chunks
4. **SwapchainManager** - VkSwapchainKHR, framebuffers, depth textures
5. **CommandBufferManager** - Command pools and buffers (Global/Main types)
6. **BufferManager** - Vertex/index/uniform/storage buffers
7. **Islands** - Terrain heightmaps and storage buffer
8. **TextureManager** - Textures, samplers, render targets with lazy loading
9. **TextManager** - Font rendering and text layout
10. **ImGuiManager** - ImGui-based user interface rendering
11. **PipelineManager** - Graphics and compute pipelines (~60 total)
12. **ParticleManager** - GPU particle system with compute shaders

All managers accessed via global pointers (e.g., `gpTextureManager`). Only destroyed during Graphics destruction.

## Key Patterns

**Single Header Include**: External consumers include only `Graphics/Graphics.h`, which provides access to all managers, Islands, OneShotCommandBuffer, and core types.

**Fence Wait Before Updates**: GPU resources updated only after fence wait to avoid modifying in-use resources.

**Lazy Texture Loading**: TextureManager creates empty textures at startup (correct size/format/mips from ChunkHeader), background thread loads data, `ProcessPendingTextures()` updates in-place after fence wait. VkImageView unchanged, so no descriptor set updates needed.

**Command Buffer Recording**: Recorded once at startup, resubmitted every frame without re-recording. Only re-recorded when manager recreated (resize, settings change).

**Descriptor Sets**: Per-framebuffer allocation prevents GPU conflicts. Recreated when swap chain resize changes framebuffer count.

**VMA Integration**: All GPU memory allocation handled through VmaAllocator in DeviceManager.

## See Also
- [Managers/CLAUDE.md](Managers/CLAUDE.md) - Individual manager details and Vulkan patterns
- [Objects/CLAUDE.md](Objects/CLAUDE.md) - RAII wrappers for Vulkan resources
