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
- `RenderMainPresentAcquire(iCommandBuffer, rFrameInterpolate)`: Render scene using the provided `FrameInterpolate`, submit main command buffer, submit UI command buffer (ImGui), present to screen, signal TextureUploadManager to process one chunk via binary semaphore release, acquire next image. Takes the interpolated frame state as a parameter so callers can provide the appropriate source: the boot-time call site in Main.cpp passes `pGame->CurrentFrame().interpolate` (since `mpFrameInterpolate` isn't initialized yet), while the normal game loop in GameBase.cpp passes `*gpGraphics->mpFrameInterpolate`. Dispatched asynchronously via `mRenderFuture.Wake()` (`PersistentWorker` identified as `kThreadRender`)
- `WaitForRender()`: Calls `mRenderFuture.Wait()` to block until the async render operation completes. Called before starting the next frame's rendering

Three GPU submissions per frame: Global (pre-processing) -> Main (scene rendering) -> ImGui (UI overlay), synchronized via semaphores with the fence signaled by the final ImGui submission.

The Graphics class owns the interpolated frame state (`mFrameInterpolate`) used for smooth rendering between physics ticks.

**Resource Recreation**: Settings changes set `DestroyType` enum and `DestroyFlags` bitflags. `Destroy()` waits for device idle once, then `RecreateResources()` rebuilds only flagged resources to minimize GPU synchronization.

**Frame Tracking**: `miFrameCounter` monotonically increases for VMA memory budget tracking (not cycling framebuffer index).

### CameraBase
Abstract base camera providing view/projection matrix calculation and frustum culling. Game implementations inherit from CameraBase. Global `gpCamera` pointer initialized in Main.cpp points to game-specific camera instance.

### Islands
**Global**: `gpIslands`

Island-based terrain system with CPU heightmaps for collision and GPU textures for rendering. `GlobalElevation()` transforms world position to island-local UV, samples normalized float heightmap (0-1), applies beach/height scaling. `GlobalNormal()` samples 4 surrounding heightmap points via finite differences. `TerrainCollision()` free function steps along a ray testing elevation for terrain hit detection. Island quad data is copied to a host-visible storage buffer for GPU rendering using `common::gpThreadLocal->mWorkbuffer` via `PushBuffer()` for temporary allocation (with `Pop()` after memcpy). Per-island flip state (`SetIslandFlip`) and global flip (`SetIslandsFlip`) update texture coordinates and re-upload quad data. `smPriorityIslands` is sorted after collection for deterministic iteration order by TextureManager and other consumers.

### AnimationData
Runtime animation system for models supporting both skeletal skinning and node-based animation. Loads skeleton and animation data from pack files into `gAnimationDataMap` global registry keyed by scene CRC. Data members (`mCrc`, `mHeader`, `mChannels`, `mKeyframes`, `mCubicKeyframes`) are public for direct access by FileManager and game code. `FindAnimation()` performs a linear search by name over animation clips, returning the index or -1 if not found.

**Keyframe Interpolation**: `InterpolateKeyframes()` supports three interpolation modes per glTF spec: step (immediate value), linear (lerp for translation/scale, slerp for quaternion rotation), and cubicspline (Hermite spline using in/out tangents scaled by time delta).

**Load-time Pre-computation**: `Load()` pre-computes several data structures to avoid redundant per-frame work: `mBindPoseLocalMatrices[]` stores `bindMatrix * S * R * T` for each node, `mbAnimatedNodes[][]` is a per-animation bitmask of which nodes are targeted by animation channels, `mAlignedInverseBindMatrices[]` and `mAlignedRelativeTransforms[]` store aligned `XMMATRIX` copies of inverse bind matrices and material relative transforms (eliminating per-frame `XMLoadFloat4x4` from unaligned `XMFLOAT4X4` storage).

**World Matrix Computation**: `EvaluateWorldMatrices()` is called once per model to compute world matrices for all skeleton nodes. Allocates temporary TRS arrays from the thread-local workbuffer via a single `PushBuffer()` call, partitioned by pointer arithmetic. Only initializes bind-pose TRS for animated nodes (using `mbAnimatedNodes` mask), applies animation channel values (channels reference nodes via `uiNodeIndex`), then builds world matrices in a single O(N) forward pass: animated nodes compute local matrices as `bindMatrix * S * R * T`, non-animated nodes use pre-computed `mBindPoseLocalMatrices[i]` directly. Topological node ordering (every parent index is less than its child index, asserted in `Load()`) guarantees parent world matrix is ready. Caller provides the output `pWorldMatrices` array and is responsible for allocating it (typically from the workbuffer). Calls `Pop()` when done.

**Per-Material Evaluation**: `EvaluateMaterial()` computes shader data for a specific material using pre-computed world matrices from `EvaluateWorldMatrices()`. Uses pre-loaded aligned `mAlignedRelativeTransforms[]` and `mAlignedInverseBindMatrices[]` instead of per-frame `XMLoadFloat4x4` calls. Writes per-mesh data to `MeshData` and joint matrices to a separate `JointMatrix` buffer at a caller-specified offset. Joint count is clamped to `kiMaxJointsPerMesh` to match shader buffer size, allowing models with more joints to render with partial skinning. For skinned meshes (jointCount > 0), computes mesh world matrix from parent node (relativeTransform * nodeWorldAnimated), then uses `skinJointToNode[]` mapping to compute joint matrices as `inverseBind * nodeWorld * inverse(meshWorld)`, storing only 3 rows per joint (48 bytes) since row 3 is always (0, 0, 0, 1) for rigid bone transforms. This order matches Vulkan-glTF-PBR: inverseBind transforms from world-bind-pose to joint-local space, nodeWorld animates to current world position, and inverse(meshWorld) converts to mesh-local space for the shader. For non-skinned meshes attached to animated nodes, uses `iParentNodeIndex` to compute mesh world matrix from relative transform combined with animated parent node matrix. No explicit matrix transpose is needed for storage -- row-major (DirectXMath) to column-major (GLSL) storage reinterpretation naturally transposes the data. The normal matrix is precomputed CPU-side as `transpose(inverse(mat3(meshWorld)))` and stored as 3 vec4s; this avoids shader-side inverse() calls which can cause pipeline creation hangs on some NVIDIA drivers.

### OneShotCommandBuffer
Immediate-mode GPU command utility for one-time operations. Allocates command pool/buffer, records commands, submits with fence synchronization. Used for texture uploads, layout transitions, and initialization operations.

### Screenshot
**Conditional**: `ENABLE_SCREENSHOTS` define

Asynchronous screenshot capture to JPEG. Copies swapchain image to host memory, launches async thread (with `ThreadLocal` constructed from static-local buffers identified as `kThreadScreenshot`) to encode JPEG in Windows temp directory.

### GraphicsUtils
Utility functions for Vulkan development and debugging.

**CHECK_VK(expr)**: Macro for Vulkan error handling - calls `CheckVk()` with stringified expression. Uses `std::source_location` to capture call site information automatically. On failure, calls `CheckVkFailed()` which handles device lost and swapchain recreation by setting `gpGraphics->meDestroyType`, avoiding immediate crashes for recoverable errors.

**VkName()**: Sets debug names on Vulkan objects for identification in validation layers and GPU debugging tools. Uses `if constexpr (kbEnableVulkanDebugLayers)` for compile-time elimination when debug layers are disabled. Builds names using the thread-local Workbuffer via `Push()`/`Append()` (prefix from object type + user-provided name), then emplaces the `View()` result into `Graphics::mDebugNames` to ensure pointer lifetime for Vulkan's retained reference.

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

**Lightweight Header**: `Graphics/Graphics.h` uses forward declarations for all manager classes and Islands. Consumer files must include the specific manager headers they need directly (e.g., `Graphics/Managers/DeviceManager.h`, `Graphics/Managers/TextureManager.h`). This avoids transitive include bloat and reduces recompilation when individual managers change.

**Fence Wait Before Updates**: GPU resources updated only after fence wait to avoid modifying in-use resources.

**Lazy Texture Loading**: TextureManager creates deferred textures at startup borrowing white placeholder VkImageView (no GPU allocation), FileManager's background thread loads data from disk, TextureUploadManager's dedicated thread uploads to GPU via transfer queue, `ProcessPendingTextures()` adopts GPU resources or creates them on the main thread after fence wait and propagates new VkImageView to all registered pipeline bindings via deferred descriptor updates. QFOT acquire barriers for all adopted textures are batched into a single command buffer prepended before the global command buffer submission. Pipelines using `kUpdateAfterBind` flag support descriptor updates without command buffer re-recording.

**Command Buffer Recording**: Recorded once at startup, resubmitted every frame without re-recording. Only re-recorded when manager recreated (resize, settings change).

**Descriptor Sets**: Per-framebuffer allocation prevents GPU conflicts. Recreated when swap chain resize changes framebuffer count.

**VMA Integration**: All GPU memory allocation handled through VmaAllocator in DeviceManager.

### ComparisonLog.h
Debug logging infrastructure for comparing animation processing between BrokenEngine and the Vulkan-glTF-PBR reference implementation. Conditionally enabled only when processing the "free_cyberpunk_hovercar" model (hardcoded CRC check).

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
