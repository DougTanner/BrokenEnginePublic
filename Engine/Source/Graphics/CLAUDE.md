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

**Manager Initialization**: Creates managers in strict dependency order required by Vulkan resource hierarchy (see Manager Initialization Order below). After DeviceManager creation, initializes TextureUploadManager's transfer queue resources for background GPU texture uploads. Transfer resources are destroyed before DeviceManager during shutdown.

**Render Loop** (Async Pipeline):
- `RenderGlobal()`: Wait for fence, process pending texture loads, submit global command buffer (shadows, particles)
- `RenderMainPresentAcquire()`: Update camera, render scene, submit main command buffer, submit UI command buffer (ImGui), present to screen, acquire next image. Launched asynchronously via `std::future` stored in `mRenderFuture`
- `WaitForRender()`: Blocks until the async render operation completes. Called before starting the next frame's rendering

Three GPU submissions per frame: Global (pre-processing) -> Main (scene rendering) -> ImGui (UI overlay), synchronized via semaphores with the fence signaled by the final ImGui submission.

The Graphics class owns the interpolated frame state (`mFrameInterpolate`) used for smooth rendering between physics ticks.

**Resource Recreation**: Settings changes set `DestroyType` enum and `DestroyFlags` bitflags. `Destroy()` waits for device idle once, then `RecreateResources()` rebuilds only flagged resources to minimize GPU synchronization.

**Frame Tracking**: `miFrameCounter` monotonically increases for VMA memory budget tracking (not cycling framebuffer index).

### CameraBase
Abstract base camera providing view/projection matrix calculation and frustum culling. Game implementations inherit from CameraBase. Global `gpCamera` pointer initialized in Main.cpp points to game-specific camera instance.

### Islands
**Global**: `gpIslands`

Island-based terrain system with CPU heightmaps for collision and GPU textures for rendering. `GlobalElevation()` transforms world position to island-local UV, samples normalized float heightmap (0-1), applies beach/height scaling. `GlobalNormal()` samples 4 surrounding heightmap points via finite differences.

### GltfAnimationData
Runtime animation system for glTF models supporting both skeletal skinning and node-based animation. Loads skeleton and animation data from pack files into `gAnimationDataMap` global registry keyed by glTF CRC.

**Keyframe Interpolation**: `InterpolateKeyframes()` supports three interpolation modes per glTF spec: step (immediate value), linear (lerp for translation/scale, slerp for quaternion rotation), and cubicspline (Hermite spline using in/out tangents scaled by time delta).

**World Matrix Computation**: `EvaluateWorldMatrices()` initializes node transforms from bind pose TRS, applies animation channel values (channels reference nodes via `uiNodeIndex`), builds local matrices as `bindMatrix * S * R * T` (combining node matrix with TRS), then computes world matrices by traversing parent chain for each node (matching Vulkan-glTF-PBR reference approach).

**Per-Material Evaluation**: `Evaluate()` computes shader data for a specific material. Joint count is clamped to `kiMaxJoints` (128) to match shader buffer size, allowing models with more joints to render with partial skinning. For skinned meshes (jointCount > 0), computes mesh world matrix from parent node (relativeTransform * nodeWorldAnimated), then uses `skinJointToNode[]` mapping to compute joint matrices as `inverseBind * nodeWorld * inverse(meshWorld)`. This order matches Vulkan-glTF-PBR: inverseBind transforms from world-bind-pose to joint-local space, nodeWorld animates to current world position, and inverse(meshWorld) converts to mesh-local space for the shader. For non-skinned meshes attached to animated nodes, uses `iParentNodeIndex` to compute mesh world matrix from relative transform combined with animated parent node matrix. All matrices are transposed via `XMMatrixTranspose()` before storage to convert from DirectX Math row-major to GLSL column-major format. The normal matrix is precomputed CPU-side as `transpose(inverse(mat3(meshWorld)))` and stored as 3 vec4s; this avoids shader-side inverse() calls which can cause pipeline creation hangs on some NVIDIA drivers.

### OneShotCommandBuffer
Immediate-mode GPU command utility for one-time operations. Allocates command pool/buffer, records commands, submits with fence synchronization. Used for texture uploads, layout transitions, and initialization operations.

### Screenshot
**Conditional**: `ENABLE_SCREENSHOTS` define

Asynchronous screenshot capture to JPEG. Copies swapchain image to host memory, launches async thread to encode JPEG in Windows temp directory.

### GraphicsUtils
Utility functions for Vulkan development and debugging.

**CHECK_VK(expr)**: Macro for Vulkan error handling - calls `CheckVk()` with stringified expression. Uses `std::source_location` to capture call site information automatically. On failure, calls `CheckVkFailed()` which handles device lost and swapchain recreation by setting `gpGraphics->meDestroyType`, avoiding immediate crashes for recoverable errors.

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

**Lazy Texture Loading**: TextureManager creates deferred textures at startup borrowing white placeholder VkImageView (no GPU allocation), FileManager's background thread loads data from disk, TextureUploadManager's dedicated thread uploads to GPU via transfer queue, `ProcessPendingTextures()` adopts GPU resources or creates them on the main thread after fence wait and propagates new VkImageView to all registered pipeline bindings via deferred descriptor updates. Pipelines using `kUpdateAfterBind` flag support descriptor updates without command buffer re-recording.

**Command Buffer Recording**: Recorded once at startup, resubmitted every frame without re-recording. Only re-recorded when manager recreated (resize, settings change).

**Descriptor Sets**: Per-framebuffer allocation prevents GPU conflicts. Recreated when swap chain resize changes framebuffer count.

**VMA Integration**: All GPU memory allocation handled through VmaAllocator in DeviceManager.

### GltfComparisonLog.h
Debug logging infrastructure for comparing glTF animation processing between BrokenEngine and the Vulkan-glTF-PBR reference implementation. Conditionally enabled only when processing the "free_cyberpunk_hovercar" model (hardcoded CRC check).

**Globals**:
- `gComparisonLog` - Output file stream for logging
- `gbComparisonLoggingEnabled` - Enables logging when processing free_cyberpunk_hovercar model
- `gbFirstFrameLogged` - Tracks whether the first frame's runtime evaluation has been logged

**Functions**:
- `InitComparisonLog(crc)` - Opens `gltf_comparison_broken_engine.log` if CRC matches free_cyberpunk_hovercar model
- `CloseComparisonLog()` - Closes the log file
- `CompLog(format, args...)` - Printf-style logging to the comparison log

**Log Files** (when processing free_cyberpunk_hovercar model):
- `gltf_comparison_data_packer.log` - DataPacker export-time logging
- `gltf_comparison_broken_engine.log` - Engine runtime Load() and Evaluate() logging
- `gltf_comparison_vulkan_pbr.log` - Vulkan-glTF-PBR load-time logging (external repo)
- `gltf_comparison_vulkan_pbr_2.log` - Vulkan-glTF-PBR runtime logging (external repo)

## See Also
- [Managers/CLAUDE.md](Managers/CLAUDE.md) - Individual manager details and Vulkan patterns
- [Objects/CLAUDE.md](Objects/CLAUDE.md) - RAII wrappers for Vulkan resources
