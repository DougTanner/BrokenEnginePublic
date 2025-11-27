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

**Render Loop**:
- `RenderGlobal()`: Wait for fence, process pending texture loads, submit global command buffer (shadows, particles)
- `RenderMainImagePresentAcquire()`: Render scene and UI, check for deferred command buffer re-recording requests, submit main command buffer, present to screen, acquire next image

**Resource Recreation**: Settings changes set `DestroyType` enum and `DestroyFlags` bitflags. `Destroy()` waits for device idle once, then `RecreateResources()` rebuilds only flagged resources to minimize GPU synchronization.

**Frame Tracking**: `miFrameCounter` monotonically increases for VMA memory budget tracking (not cycling framebuffer index).

### CameraBase
Abstract base camera providing view/projection matrix calculation and frustum culling.

**Key Responsibilities**:
- Calculates view and projection matrices from eye and target positions
- Computes visible area bounds in world space for culling
- Converts screen coordinates to world space via ray-plane intersection
- Provides visibility testing for positions and axis-aligned bounding boxes

**Integration**: Game implementations inherit from CameraBase. Global `gpCamera` pointer initialized in Main.cpp points to game-specific camera instance.

### Islands
**Global**: `gpIslands`

Island-based terrain system with CPU heightmaps for collision and GPU textures for rendering.

**Per-Island Data**: `mIslands` vector contains quad geometry, heightmap pointer, and dimensions for each island.

**Heightmap Loading**: Constructor collects island CRCs. `WaitForElevationMaps()` called from Main.cpp waits for lazy chunk loading and initializes heightmap pointers (no data copy).

**Height Queries**: `GlobalElevation()` transforms world position to island-local UV with flip transformations, samples normalized float heightmap (0-1), applies beach/height scaling. Returns sea floor for positions outside islands.

**Normal Calculation**: `GlobalNormal()` samples 4 surrounding heightmap points via finite differences, clamped to island bounds.

### OneShotCommandBuffer
Immediate-mode GPU command utility for one-time operations.

Allocates command pool/buffer, records commands, submits with fence synchronization. Used for texture uploads, layout transitions, and initialization operations.

### Screenshot
**Conditional**: `ENABLE_SCREENSHOTS` define

Asynchronous screenshot capture to JPEG.

Waits for framebuffer fence, copies swapchain image to host memory via `TextureManager::CopyImageToHostMemory()`, launches async thread to convert ARGB→RGBA and encode to JPEG in Windows temp directory.

## Manager Initialization Order

Strict dependency order required for Vulkan resource creation (violating crashes or causes validation errors):

1. **InstanceManager** - VkInstance and physical device selection
2. **DeviceManager** - VkDevice, VkQueue, VkDescriptorPool, VmaAllocator (all subsequent resources require these)
3. **SwapchainManager** - VkSwapchainKHR, framebuffers, depth textures
4. **ShaderManager** - VkShaderModule objects from SPIR-V chunks
5. **TextureManager** - Textures, samplers, render targets with lazy loading
6. **BufferManager** - Vertex/index/uniform/storage buffers
7. **PipelineManager** - Graphics and compute pipelines (~60 total)
8. **CommandBufferManager** - Command pools and buffers (Global/Image types)
9. **ParticleManager** - GPU particle system with compute shaders
10. **TextManager** - Font rendering and text layout

All managers accessed via global pointers (e.g., `gpTextureManager`). Only destroyed during Graphics destruction.

## Key Patterns

**Fence Wait Before Updates**: GPU resources updated only after fence wait to avoid modifying in-use resources.

**Lazy Texture Loading**: TextureManager creates empty textures at startup (correct size/format/mips from ChunkHeader), background thread loads data, `ProcessPendingTextures()` updates in-place after fence wait. VkImageView unchanged, so no descriptor set updates needed.

**Command Buffer Recording**: Recorded once at startup, resubmitted every frame without re-recording. Only re-recorded when manager recreated (resize, settings change).

**Descriptor Sets**: Per-framebuffer allocation prevents GPU conflicts. Recreated when swap chain resize changes framebuffer count.

**VMA Integration**: All GPU memory allocation handled through VmaAllocator in DeviceManager.

## Vulkan SDK Location

Likely at `C:/SDK/VulkanSDK/*` or `/mnt/c/SDK/VulkanSDK/*`

## See Also
- [Managers/CLAUDE.md](Managers/CLAUDE.md) - Individual manager details and Vulkan patterns
- [Objects/CLAUDE.md](Objects/CLAUDE.md) - RAII wrappers for Vulkan resources