# `/Engine/Source/Graphics/`

Vulkan-based rendering system orchestrating graphics resources through specialized manager classes. Entirely client-only (`#ifdef BT_CLIENT`), including Islands (GPU terrain rendering), CameraBase, and all managers. Terrain collision queries are handled by `IslandTerrain` in `/Frame/` (shared by both builds).

## Architecture Overview

- **Vulkan Function Loading**: Volk meta-loader provides direct driver access
- **Rendering**: Multi-pass deferred renderer with lighting, shadows, and post-processing
- **Synchronization**: Multiple frames in flight with per-framebuffer command buffers and fences
- **Resource Management**: RAII wrappers for all Vulkan objects with automatic cleanup

## Core Classes

### Graphics
**Global**: `gpGraphics`

Central orchestrator that owns all graphics managers and coordinates the render loop. Verifies Vulkan 1.2 support at startup and creates managers in strict dependency order (see Manager Initialization Order below). Initializes TextureUploadManager's transfer queue resources after DeviceManager creation.

**Render Loop**: Three GPU submissions per frame: Global (shadows, particles) -> Main (scene rendering) -> ImGui (UI overlay), synchronized via semaphores with the fence signaled by the final submission. Main rendering is called synchronously on the main thread. Owns persistent per-frame render interpolates populated by GameBase. Samples `mRenderFrameTimer` immediately after the fence wait in `RenderGlobal()` to produce `miRenderFrameDeltaNs`, a GPU-paced render-frame delta used by Camera for smooth motion timing (instead of wall-clock time, which can spike during fence stalls).

**Resource Recreation**: `DestroyType` is an ordered cascade (`kNone < kCommandBuffers < kSamplers < kPipelines < kSwapchain < kSurface`), each level including all lower levels. `DestroyFlags` bitflags control which resources are rebuilt to minimize GPU synchronization. Swapchain-level recreation keeps TextureManager and BufferManager alive with loaded data intact (partial teardown/rebuild). Surface-level recreation (device lost) fully destroys and reconstructs everything. Sampler-level recreation surgically updates descriptor sets without rebuilding pipelines.

**Device Lost Recovery**: `DeviceLostException` propagates to `Main.cpp`, which destroys and reconstructs the entire Graphics instance. Time clocks are reset to prevent time jumps.

### CameraBase
Abstract base camera providing view/projection matrix calculation and visible area culling. Game implementations inherit from CameraBase. Global `gpCamera` pointer points to game-specific camera instance.

### Islands
**Global**: `gpIslands`

Client-only GPU terrain rendering system (`#ifdef BT_CLIENT`). Manages per-island quad data uploaded to a storage buffer for GPU rendering pipelines. Synchronizes island data with the active multi-frame grid each physics step, dynamically growing capacity as needed. Reads terrain constants (beach elevation) from `gpIslandTerrain`. Terrain collision queries (GlobalElevation/GlobalNormal) are in `IslandTerrain` under `/Frame/`.

### AnimationData
Runtime skeletal animation system for models, registered in a global map keyed by scene CRC. Loads skeleton and animation data from pack files using zero-copy const pointers into eagerly-loaded pack memory. Pre-computes bind-pose matrices, animated-node bitmasks, and aligned inverse bind / relative transform matrices at load time to avoid redundant per-frame work.

**Evaluation flow**: `EvaluateWorldMatrices()` computes world matrices for all skeleton nodes in a single O(N) forward pass exploiting topological node ordering. `EvaluateMaterial()` computes per-material shader data (mesh transforms and joint matrices) using the pre-computed world matrices. Joint matrices stored as 3 rows (48 bytes) since row 3 is always identity. Normal matrices precomputed CPU-side to avoid shader-side inverse() calls. `EvaluateAnimation()` combines both steps for convenience.

**Keyframe interpolation**: Supports step, linear (lerp/slerp), and cubicspline (Hermite) modes per glTF spec.

### OneShotCommandBuffer
Immediate-mode GPU command utility for one-time operations with fence synchronization. Used for texture uploads, layout transitions, and initialization.

### Screenshot
Asynchronous screenshot capture to JPEG on a dedicated thread. Conditional on `ENABLE_SCREENSHOTS` define.

### GraphicsUtils
Vulkan error handling (`CHECK_VK` macro with device-lost and swapchain recreation support), debug object naming (`VkName`), `DeviceLostException`, and shared rendering helpers (`IsPointVisible`, `ProjectToBaseHeight`, `BuildAxisAlignedQuad`) used by SmokeTrails, WindTrails, WindRadials, and lighting collections.

## Manager Initialization Order

Strict dependency order required for Vulkan resource creation (violating crashes or causes validation errors). `IslandTerrain` (shared terrain collision) is created before Graphics in Main.cpp:

1. **InstanceManager** - VkInstance and physical device selection
2. **DeviceManager** - VkDevice, queues, descriptor pool, VmaAllocator
3. **SwapchainManager** - Swapchain, framebuffers, depth textures
4. **CommandBufferManager** - Command pools and buffers (Global/Main types)
5. **BufferManager** - Vertex/index/uniform/storage buffers
6. **TextureManager** - Textures, samplers, render targets with lazy loading
7. **TextManager** - Font rendering and text layout
8. **Islands** - GPU terrain quad storage buffer (reads from `gpIslandTerrain`)
9. **PipelineManager** - Loads SPIR-V shaders and creates graphics/compute pipelines (~60 total)
10. **ParticleManager** - GPU particle system with compute shaders
11. **ImGuiManager** - ImGui-based UI rendering

All managers accessed via global pointers (e.g., `gpTextureManager`).

## Key Patterns

- **Lightweight Header**: `Graphics.h` forward-declares all managers; consumers include specific manager headers directly to avoid transitive include bloat
- **Fence Wait Before Updates**: GPU resources updated only after fence wait to avoid modifying in-use resources
- **Lazy Texture Loading**: Deferred textures start with white placeholder, load from disk on background thread, upload to GPU on transfer queue, then adopt into rendering pipeline with deferred descriptor updates. Swapchain recreation preserves all loaded textures
- **Record-Once Command Buffers**: Recorded at startup, resubmitted every frame. Re-recorded only on resize or settings change
- **Per-Framebuffer Descriptor Sets**: Prevents GPU conflicts across frames in flight
- **VMA Integration**: All GPU memory allocation handled through VmaAllocator in DeviceManager

## See Also
- [Managers/CLAUDE.md](Managers/CLAUDE.md) - Individual manager details and Vulkan patterns
- [Objects/CLAUDE.md](Objects/CLAUDE.md) - RAII wrappers for Vulkan resources
