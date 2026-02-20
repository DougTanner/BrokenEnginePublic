# `/Engine/Source/Graphics/Managers/`

Manager classes that handle high-level graphics resources and operations for the Vulkan renderer. All managers follow a singleton pattern with global pointers initialized during Graphics construction.

## Architecture Patterns

**Manager Lifecycle**:
- Created in strict dependency order during Graphics construction
- Never individually destroyed - only during Graphics destruction
- Support resource recreation for window resize and device changes
- Global pointer access (e.g., `gpTextureManager`) for cross-system communication

**Vulkan Resource Management**:
- Managers own and manage Vulkan objects with proper cleanup
- Resource updates only after fence synchronization
- Descriptor set management for dynamic resource binding
- Pipeline state object caching and reuse
- VMA (Vulkan Memory Allocator) handles all GPU memory allocation

## Core Managers

### BufferManager.h & BufferManager.cpp
**Global**: `gpBufferManager`
**Purpose**: Manages all GPU buffers for rendering and compute operations

**Buffer Types**:
- Vertex buffers for terrain, water, and model meshes (device-local for performance)
- Uniform buffers for global constants and per-framebuffer view/projection data (host-visible for updates)
- Storage buffers for dynamic game objects and particle systems (accessed by compute shaders)
- Model buffers stored in map indexed by CRC for efficient lookup
- MeshData storage buffer for model skeletal animation metadata (host-visible for CPU updates during Render)
- Joint matrices storage buffer for model skeletal animation (host-visible, initialized with identity matrices, using `JointMatrix` 3-row format at 48 bytes per joint instead of full mat4 at 64 bytes)

**Dynamic Buffer Creation**:
- Collections register storage buffers during CreatePipelines() via CreateDynamicBuffer() method
- Accepts CRC key, `DynamicBufferType` enum (kMain, kVisibleLights, kWindDeposit), buffer name, and element size in bytes (stored for type validation)
- Creates per-framebuffer storage buffers with {kStorage, kHostVisible} flags
- Buffers stored in `mDynamicStorageBuffers` array indexed by `DynamicBufferType` enum, with each element being a CRC-keyed unordered_map
- Silently returns if buffer with CRC already exists (idempotent)

**Type-Safe Buffer Access**:
- `GetDynamicStorageBuffer<T>(crc, iCommandBuffer)` provides type-safe access to mapped memory
- Returns `DynamicStorageBufferResult<T>` containing both the mapped pointer and element capacity
- Runtime assertion validates `sizeof(T)` matches the element size stored at buffer creation
- Callers can assert their write count against the returned capacity to catch buffer overflows
- Replaces unsafe direct `reinterpret_cast` access pattern used by collections

**Dynamic Buffer Resizing**:
- ResizeDynamicBuffer() recreates buffers with new size using deferred destruction pattern
- Old buffer moved to `mPreviousBuffer` storage, keeping it alive until next resize
- Deferred destruction prevents Vulkan validation errors from command buffers referencing destroyed resources
- Caller must ensure fence synchronization before calling (buffer must not be in GPU use)
- After resize, caller updates descriptor sets; command buffer re-recording not needed for pipelines with update-after-bind enabled
- ResizeDynamicBufferIfNeeded() is a convenience wrapper that compares (layoutSize * iCapacity) against the current buffer size, calls ResizeDynamicBuffer() only when growth is needed, and returns the new Buffer pointer (for descriptor update) or nullptr if no resize occurred

**Skinning Buffer Allocation**:
- `AllocateMeshData(iCommandBuffer, iCount)` and `AllocateJointMatrices(iCommandBuffer, iCount)` provide per-frame bump allocation into the MeshData and JointMatrix storage buffers, returning the starting offset for each allocation
- `ResetSkinningAllocations(iCommandBuffer)` resets both offset counters to zero and releases deferred old buffers, called at the start of each frame's main render phase
- Per-command-buffer offset tracking (`miMeshDataOffset[]`, `miJointMatrixOffset[]`) ensures concurrent frames do not interfere
- When allocations exceed current capacity, buffers automatically grow by doubling in size via `GrowMeshDataBuffer()` / `GrowJointMatrixBuffer()`, which copy existing data to the new buffer and propagate descriptor updates to all model pipelines (both regular and shadow maps)
- Old buffers are held in per-command-buffer `mPreviousMeshDataBuffer[]` / `mPreviousJointMatrixBuffer[]` using deferred destruction to prevent Vulkan validation errors from in-flight command buffers referencing destroyed resources
- Callers (e.g., Player::Render) use these methods to obtain contiguous regions for their mesh data and joint matrices without knowledge of other consumers' allocations

**Swapchain-Dependent Buffer Lifecycle**:
- `DestroySwapchainDependentBuffers()` tears down per-framebuffer buffers (uniform, storage, text, smoke, wind, particles, skinning, dynamic) while preserving non-framebuffer-dependent resources (terrain mesh, water mesh, model buffers, quads vertex buffer). Enables partial teardown during swapchain recreation without destroying the full BufferManager
- `CreateSwapchainDependentBuffers()` rebuilds all per-framebuffer buffers based on the new swapchain's framebuffer count. Together with `DestroySwapchainDependentBuffers()`, supports granular resource recreation during swapchain changes while the BufferManager instance and static geometry remain alive
- During `kSwapchain`-level recreation, the BufferManager survives (only swapchain-dependent buffers are torn down and rebuilt). During `kSurface`-level recreation (device lost), the full BufferManager is destroyed and reconstructed

**Key Patterns**:
- Per-framebuffer duplication for uniform and storage buffers enables parallel frame rendering
- Storage buffers support both graphics and compute shader access
- Terrain and water meshes created at startup with fixed geometry
- CRC-based lookup eliminates need for static index variables in collections

### CommandBufferManager.h & CommandBufferManager.cpp
**Global**: `gpCommandBufferManager`
**Purpose**: Records and submits Vulkan command buffers using record-once, submit-many pattern

**Architecture**:
- Command buffers recorded once at startup, then resubmitted every frame without re-recording
- Re-recorded only when manager recreated (window resize, device lost, settings changes)
- One command buffer set per framebuffer for triple-buffered swapchain
- VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT NOT used - recordings are reusable
- All rendering uses primary command buffers with VK_SUBPASS_CONTENTS_INLINE

**Command Buffer Types**:
- Global (primary): Pre-processing passes (shadows, terrain generation, wind deposit + wind spread via ping-pong dual render passes for both oriented and axis-aligned deposits, smoke spread, particle spawn/update)
- Main (primary): Pre-processing (lighting, lighting blur, smoke emit, object shadows, object shadows blur) then main render pass
- ImGui (primary): UI overlay rendering, recorded per-frame in ImGuiManager::Submit()

**Main Render Pass Order**:
Opaque model objects, terrain, water, hex shields, transparent model objects (only for models with `mbHasTransparentMaterials`), particles (long then square), visible lights, billboards, text. Opaque model materials are drawn first with depth writing, then transparent materials are drawn after water/hex shields with alpha blending and no depth writes for correct transparency compositing. Hex shields render after water for correct transparency blending with water surface. Lighting pass includes particle lighting render calls (both long and square) alongside dynamic lighting pipelines.

**Object Shadow Pass**: Draws only opaque model materials (`ModelDrawPass::kOpaque`) into the shadow map, skipping transparent materials.

**Cross-Command-Buffer Particle Synchronization**:
- Binary semaphore (`mParticleSyncVkSemaphore`) synchronizes particle storage buffer access between the main and global command buffers across frames
- Main command buffer submission signals the semaphore after rendering completes
- Global command buffer submission conditionally waits on the semaphore (only when `mbParticleSemaphoreSignaled` is true, skipping the wait on the first frame since no prior signal exists)
- Waits at `VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT` to stall particle spawn/update compute work until the previous frame's rendering is complete
- Prevents compute shaders from reading particle storage buffers while the previous frame's render pass is still drawing from them
- TODO: A `VkEvent` via `VK_KHR_synchronization2` would allow finer-grained synchronization without stalling non-particle compute work in the global command buffer

**Host-to-Shader Synchronization**:
- Memory barrier placed immediately after uniform buffer copy, before any indirect draws
- Ensures CPU-written animation data (joint matrices, mesh data, indirect draw buffers) is visible to all GPU consumers
- Uses `VK_ACCESS_HOST_WRITE_BIT` to `VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT` with `VK_PIPELINE_STAGE_HOST_BIT` to `VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT`
- Protects all indirect draws: lighting passes, smoke emit, model shadow pass, and main render pass model objects
- Early barrier placement eliminates CPU/GPU race conditions across all rendering passes

**Three-Stage GPU Submission**:
- `SubmitGlobalCommandBuffer()`: Conditionally waits on particle sync semaphore (skipped on first frame), prepends TextureManager's acquire barrier command buffer (if `mbHasPendingAcquireBarriers` is set) before the global command buffer in a single queue submission, completing QFOT for textures uploaded by the transfer queue. Signals global-finished semaphore
- `SubmitMainCommandBuffer()`: Waits on global-finished and image-available semaphores, submits main rendering, signals main-finished semaphore and particle sync semaphore
- `SubmitUiCommandBuffer()`: Waits for main submission future, delegates to ImGuiManager::Submit() which waits on main-finished semaphore, renders ImGui, signals ImGui-finished semaphore and fence

**Key Features**:
- MRT lighting pass outputs to 3 color attachments simultaneously (R/G/B channels)
- Synchronization via semaphores (Global -> Main -> ImGui) and fences (frame-to-frame, signaled by ImGui submission)
- Optimized pipeline barriers with minimal stage masks for GPU efficiency
- Multi-threaded submission via `PersistentWorker` members (`mSubmitGlobal` for `kThreadSubmitGlobal`, `mSubmitMain` for `kThreadSubmitMain`) at time-critical thread priority; dispatched via `Wake()` with `Wait()` for synchronization between stages
- Global submission conditionally waits on the particle sync binary semaphore (skipped on first frame), while main submission uses stack-allocated C-style arrays for semaphores and pipeline stage flags instead of `std::vector`
- Screenshot capture integration (ENABLE_SCREENSHOTS)
- Dynamic pipelines iterated via `mDynamicPipelineMaps[]` array indexed by `DynamicPipelineType` enum and `mDynamicModelPipelineMaps[]` array indexed by `DynamicModelPipelineType` enum

**Selective Re-recording**:
- Recorded flag per framebuffer controls whether RecordCommandBuffers() re-records
- Enables runtime command buffer updates after buffer/descriptor changes

### ImGuiManager.h & ImGuiManager.cpp
**Global**: `gpImGuiManager`
**Purpose**: Integrates Dear ImGui for menu and debug UI rendering with dedicated Vulkan resources

**Architecture**:
- Creates dedicated render pass and framebuffers separate from main rendering pipeline
- Dedicated framebuffers reference swapchain images but use ImGui-specific render pass
- Ensures Vulkan renderpass/framebuffer compatibility (framebuffers must match their associated render pass)
- Renders after main pass completes, preserving existing frame content with `VK_ATTACHMENT_LOAD_OP_LOAD`
- Dual font support: EFIGS font and Chinese font (NotoSansSC) loaded from Raw chunk via eager loading system (AddFontFromMemoryTTF)
- Chinese font exposed via `mpChineseFont` for screens to switch fonts based on language selection
- UI scaled to 2x size for readability
- Gamepad navigation enabled via `ImGuiConfigFlags_NavEnableGamepad`

**Synchronization**:
- Waits on main pass completion semaphore before rendering
- Signals ImGui completion semaphore for presentation dependency chain
- Signals the per-framebuffer fence (final submission in the frame's GPU work)
- Uses dedicated command buffer from CommandBuffers structure

**GPU Profiling**:
- Resets and records GPU timestamp queries for `kGpuTimerUiRender` within its command buffer
- Enables UI rendering time to appear in the profile overlay alongside other GPU timers

**Frame Flow**:
- `Submit()`: Handles entire ImGui frame cycle - begins frame (NewFrame calls), delegates UI content rendering to screen classes, finalizes draw data, records command buffer with GPU profiling, and submits to GPU queue

**Screen Delegation**:
- Hosts HUD (HudScreen), menu screens (MainMenuScreen, PauseMenuScreen, GraphicsMenuScreen, SoundMenuScreen, DeathMenuScreen), and debug screens (TweaksScreen)
- ImGuiManager owns screen instances and calls their `Render()` methods during the frame
- See [Ui/Screens/CLAUDE.md](../../Ui/Screens/CLAUDE.md) for screen documentation

### DeviceManager.h & DeviceManager.cpp
**Global**: `gpDeviceManager`
**Purpose**: Manages the logical Vulkan device, queues, and GPU memory allocation

**Key Responsibilities**:
- Creates logical device with required extensions and Vulkan 1.2 features (including 16-bit storage, non-uniform indexing, update-after-bind for storage buffers and sampled images, partially bound descriptors for bindless texture arrays, scalar block layout for C-like struct packing in storage buffers)
- Calls `volkLoadDevice()` immediately after device creation to load device-specific function pointers
- Manages graphics, presentation, and transfer queue handles
- Deduplicates queue family indices for device creation (Vulkan forbids duplicate family indices in VkDeviceCreateInfo) across graphics, present, and transfer families
- Transfer queue shares the graphics queue handle when both use the same queue family; retrieves a separate queue when a dedicated transfer family is available
- Initializes VMA (Vulkan Memory Allocator) with optional memory budget extension for VRAM tracking
- Creates a single descriptor pool with both FREE_DESCRIPTOR_SET_BIT and UPDATE_AFTER_BIND_BIT flags, used by all pipelines regardless of update-after-bind usage
- Enables optional extensions conditionally: shader clock, debug printf, maintenance9, memory budget, wireframe fill mode
- Queries VK_KHR_maintenance9 `optimalImageTransferToQueueFamilies` to determine if queue family ownership transfer (QFOT) is optional for transfer-to-graphics transitions (`mbTransferQfotOptional`), enabling simplified barrier paths in TextureUploadManager and Texture

**Single Descriptor Pool**:
- `mVkDescriptorPool`: Single pool with both `FREE_DESCRIPTOR_SET_BIT` and `UPDATE_AFTER_BIND_BIT` flags, serving all pipelines (both static and update-after-bind)
- Pool sizes cover uniform buffers, combined image samplers, storage buffers, samplers, sampled images, and storage images
- `maxSets` is computed as the sum of all descriptor counts

**VMA Integration**:
- All GPU memory allocation handled through VMA
- Supports Vulkan 1.2 features including `descriptorBindingStorageBufferUpdateAfterBind`
- Each buffer/texture allocation goes through VmaAllocator

### InstanceManager.h & InstanceManager.cpp
**Global**: `gpInstanceManager`
**Purpose**: Manages Vulkan instance and physical device selection

**Key Responsibilities**:
- Creates Vulkan instance with required extensions (Vulkan 1.2 required)
- Selects best available physical device (GPU)
- Creates Win32 window surface
- Queries device capabilities, limits, features, and queue family properties
- Validates and stores surface format and color space pairs
- Manages validation layers conditionally via `if constexpr (kbEnableVulkanDebugLayers)`

**Validation Layer Configuration**:
- Uses `VK_EXT_layer_settings` extension to configure Khronos validation layer with best practices and sync validation. Layer settings use a fixed-size C-style array with a count variable instead of `std::vector`
- Debug layers controlled by `kbEnableVulkanDebugLayers` constexpr bool (defined in game Pch.h)
- Supports GPU-Assisted Validation (`kbEnableGpuAssistedValidation`) for runtime shader instrumentation
- Supports Debug Printf (`kbEnableDebugPrintf`) and shader realtime clock (`kbEnableShaderRealtimeClock`) via constexpr bools from ShaderLayoutsBase.h
- GPU validation modes are mutually exclusive (GPU can only run one at a time)
- Uses `if constexpr` for compile-time elimination of debug code paths
- Debug callback suppresses known benign warnings (lazy texture undefined-to-read-only transitions, ConcurrentUsageOfExclusiveImage false positive when maintenance9 makes QFOT optional, Debug Printf messages)

**Volk Integration**:
- Calls `volkLoadInstance()` immediately after instance creation to load instance-specific function pointers
- For debug builds: Create instance → volkLoadInstance → create debug messenger
- Volk automatically loads all extension functions including debug utils

**Critical Behavior**:
- Requires Vulkan 1.2 driver (shows error and terminates if not available)
- Validates required Vulkan 1.2 features with MessageBox error if unsupported: `descriptorBindingStorageBufferUpdateAfterBind`, `shaderSampledImageArrayNonUniformIndexing`, `descriptorBindingSampledImageUpdateAfterBind`, `descriptorBindingPartiallyBound`
- Retries without validation layers if Vulkan SDK not installed (driver still required)
- Disables validation layers and limits extensions when running under RenderDoc

### ParticleManager.h & ParticleManager.cpp
**Global**: `gpParticleManager`
**Purpose**: GPU-based particle system using compute shaders for spawning and physics simulation

**Architecture**:
- Two particle types: Long particles (trails) and square particles (explosions)
- Both types share the same compute shaders (ParticlesSpawn.comp, ParticlesUpdate.comp)
- Fixed capacity of 16,384 particles per type with bitfield allocation tracking
- Fully GPU-driven: Spawn and update run as compute shaders, no CPU involvement in physics

**Dynamic Particle Textures**:
- `Spawn()` accepts a texture CRC per particle, resolved to a global bindless texture index via `GetOrAssignTextureIndex()`
- `GetOrAssignTextureIndex()` delegates directly to TextureManager's `CrcToIndex()`, which maps texture CRCs to global bindless texture array indices
- Callers (e.g., Explosions) specify per-explosion-type texture CRCs in `ExplosionType::particleCrc`

**Compute Pipeline**:
- Spawn shader: Single-threaded (workgroup size 1) for sequential slot allocation
- Update shader: Multi-threaded (workgroup size 32) for parallel physics simulation via indirect dispatch
- Spawn updates indirect dispatch buffer consumed by update shader
- Update handles position, velocity, gravity, collision, decay, and wind force application
- Wind force: Update shader samples the wind velocity field texture (ping-pong selected via `fWindTextureIndex`) and applies it to particle XY velocity, scaled by `fParticlesWindStrength` from the global layout (set from `gParticlesWindStrength` wrapper)

**Design Rationale**:
- Spawn must be single-threaded due to sequential allocation algorithm (parallelization overhead exceeds benefits)
- Update is fully parallel because particles are independent with no inter-particle dependencies

### PipelineManager.h & PipelineManager.cpp
**Global**: `gpPipelineManager`
**Purpose**: Creates and manages all graphics and compute pipelines

**Architecture**:
- `Pipelines` enum and `ModelPipelineSpec` struct defined in PipelineManager.h alongside the manager class
- Static pipelines stored in fixed-size array indexed by `Pipelines` enum
- Dynamic pipelines stored in vector of unique_ptr for collection-specific rendering
- Collections register pipelines during CreatePipelines() phase

**Key Responsibilities**:
- Creates 60+ specialized static pipelines for different rendering passes
- Manages lighting blur pipeline chains (separate R/G/B channels)
- Integrates model PBR rendering pipelines
- Creates shadow, terrain, particle, smoke, and UI pipelines
- Calls `game::FrameInterpolate::GraphicsResources()` to allow collections to register dynamic pipelines and buffers

**Pipeline Creation Methods**:
- Constructor orchestrates full pipeline creation by calling named methods in dependency order, then `game::FrameInterpolate::GraphicsResources()` for dynamic pipelines
- `CreateLightingPipelines()`: Lighting blur chains and combine pipelines for R/G/B channels
- `CreatePipelineShadows()`: Shadow elevation, shadow blur, and object shadow blur pipelines
- `CreateLightingShadowDependantPipelines()`: Terrain, water, and other pipelines that depend on lighting blur and shadow blur textures as inputs
- `CreateTerrainDataPipelines()`: Terrain data generation pipelines (elevation, color, normal, ambient occlusion) that render TO terrain detail textures
- `CreateSmokeWindPipelines()`: Smoke clear/spread and wind clear/spread pipelines (ping-pong pairs)
- `CreateParticlePipelines()`: Long and square particle spawn/update/render compute and graphics pipelines

**Selective Pipeline Recreation**:
- `RecreatePipelineGroups(DestroyFlags_t flags)` recreates only pipeline groups affected by specific resource changes, avoiding full PipelineManager teardown/rebuild
- Respects dependency ordering: lighting pipelines first (referenced by terrain/water and particles), then shadows, then terrain/water, then smoke/wind, then particles
- Also recreates affected dynamic pipelines by updating their render pass and extent from the new textures, then calling `Create()` on each
- Called by `Graphics::Destroy()` when `DestroyType` is `kPipelines` and flags indicate which resources changed (e.g., lighting textures, smoke textures, terrain detail textures)
- Falls back to full `mpPipelineManager.reset()` when object shadow changes require it or when flags are empty

**Dynamic Pipeline Pattern**:
- Collections use unique_ptr to store non-copyable Pipeline objects
- Collections cache pipeline index in static member for later access
- Enables per-collection pipeline customization without enum pollution
- `DynamicPipelineType` enum indexes `mDynamicPipelineMaps[]` array for non-model pipelines (lighting, axis-aligned lighting, visible lights, billboards, smoke, smoke axis-aligned, wind deposit, wind deposit two, wind deposit axis-aligned, wind deposit axis-aligned two, hex shields, hex shields lighting). Wind deposit pipelines come in two variants: oriented (WindTrails, using `QuadsVisibleArea.vert`) and axis-aligned (WindRadials, using `QuadsAxisAlignedVisibleArea.vert`). Both use `kBufferMain` for storage with ping-pong target selection via `giWindTextureIndex`
- `DynamicModelPipelineType` enum indexes `mDynamicModelPipelineMaps[]` array for model pipelines (model, model shadow)
- Each array element is a CRC-keyed unordered_map of Pipeline/ModelPipeline pointers
- Particle and lighting particle pipelines use the global bindless texture array from TextureManager's Set 0 for texture sampling, with a clamp sampler also from Set 0. Particle render pipelines additionally bind the smoke texture (mSmokeTextureOne with border sampler) for smoke shadow attenuation
- Visible light pipelines use `kAddAlpha` blend mode for alpha-modulated additive blending

**Smoke Pipeline Creation**:
- CreateDynamicPipelineSmokeAxisAligned() and CreateDynamicPipelineSmoke() create smoke emitter pipelines
- Smoke pipelines render to smoke emit pass (mSmokeTextureOne render pass) with additive blending
- Axis-aligned variant uses QuadsAxisAlignedVisibleAreavertCrc, generic variant uses QuadsVisibleAreavertCrc
- Both variants use Smoke.frag shader

**Model Pipeline Creation**:
- CreateModelPipeline() creates single pipeline (regular or shadow) with ModelPipelineSpec, returns ModelPipeline pointer
- `ModelPipelineSpec::bIsPipelineShadow` is passed through to `ModelPipeline::Create()` so it can skip transparency overrides for shadow pipelines
- CreateDynamicModelPipeline() and CreateDynamicModelPipelineShadow() accept collection CRC, name, scene CRC, and storage buffers
- Model buffer CRC and animation flag are looked up at runtime from the SceneHeader's `modelCrc` and `bHasAnimation` fields
- Vertex shader automatically selected based on animation flag: `ModelSkinned.vert` for animated models, `ModelStatic.vert` for static models
- Shadow pipelines appended with "Shadow" suffix and stored in `mDynamicModelPipelineMaps[DynamicModelPipelineType::kModelShadow]`, names owned by `mShadowPipelineNames` map
- Regular model pipelines use main render pass with depth test/write, sample shading, model descriptors, and `kMultiSet` flag for 3-set descriptor layout (Set 0 global from TextureManager, Set 1 shared across materials, Set 2 per-material)
- Shadow pipelines use object shadows render target with minimal descriptor sets
- Both variants are idempotent (skip creation if pipeline already exists in map)

**Shader Dependencies**:
- Each pipeline requires specific shaders from ShaderManager
- Shaders referenced via `gpShaderManager->mShaders.at(crc)` - crashes if shader not found
- All critical shaders must be loaded at startup (no fallback or lazy loading)
- Pipeline recreation requires shader availability

### ShaderManager.h & ShaderManager.cpp
**Global**: `gpShaderManager`
**Purpose**: Loads and caches compiled SPIR-V shader modules

**Architecture**:
- Loads all shaders from chunk map at startup (no lazy loading)
- Parses chunk data payload to set up zero-copy pointers for descriptor bindings, per-binding set indices, and vertex attributes, then creates VkShaderModule from the trailing SPIR-V bytecode. Chunk data layout: `[bindings ALIGN16] [setIndices ALIGN16] [attrs ALIGN16] [SPIR-V]`
- Shader objects stored in map indexed by CRC, each containing a `ShaderInfo` with pointers into pack memory
- Shaders accessed via `mShaders.at(crc)` - throws exception if not found
- No fallback mechanism - missing shader causes pipeline creation crash

### SwapchainManager.h & SwapchainManager.cpp
**Global**: `gpSwapchainManager`
**Purpose**: Manages swap chain presentation and frame synchronization

**Key Responsibilities**:
- Creates and recreates swap chain on window resize or settings changes
- Manages framebuffers for each swap chain image
- Creates depth and multisampling textures
- Handles frame synchronization with semaphores and fences
- Uses validated surface format and color space from InstanceManager
- Provides image acquisition and presentation to screen
- `AcquireNextImage()` and `PresentImpl()` handle `VK_ERROR_OUT_OF_DATE_KHR` and `VK_SUBOPTIMAL_KHR` gracefully by setting `gpGraphics->meDestroyType = DestroyType::kSwapchain` for deferred swapchain recreation, avoiding `CHECK_VK` debug breaks during normal window mode transitions
- Async presentation via `PersistentWorker` member (`mPresent` identified as `kThreadPresent`) dispatched via `Wake()` at time-critical thread priority

### TextManager.h & TextManager.cpp
**Global**: `gpTextManager`
**Purpose**: Efficient text rendering system for UI and debug text

**Key Features**:
- Manages character maps for EFIGS (European languages) and Chinese fonts loaded from BMFont binary format
- Converts all translated strings to uppercase during initialization
- Batches text quads into storage buffers for efficient rendering
- Updates text areas for debug stats, graphics info, and profiling data
- Character lookup supports fallback to EFIGS font if character not found in Chinese font
- Renders text with drop shadow effect via dual-pass rendering (shadow pass with offset, then main text on top)
- Uses `common::gpThreadLocal->mWorkbuffer` for temporary float storage via `PushBack<float>()`/`Span<float>()` during rendering, avoiding per-frame allocations for x-offset arrays. Calls `Pop()` after consuming the span

### TextureUploadManager.h & TextureUploadManager.cpp
**Global**: `gpTextureUploadManager`
**Purpose**: Dedicated thread and Vulkan transfer queue resources for background GPU texture uploads, streaming one chunk per frame through a fixed-size staging buffer

**Architecture**:
- Created in Main.cpp before FileManager, owns a dedicated upload thread and Vulkan transfer queue command pool/fence
- `InitTransferResources()` / `DestroyTransferResources()` called by Graphics during device creation/destruction to manage the Vulkan command pool, persistent command buffer, fence, and a persistent staging buffer (4MB, allocated once via VMA) on the transfer queue family. `InitTransferResources()` resets `mShutdown` to `false` so the upload thread can be restarted after device recreation. `DestroyTransferResources()` joins the upload thread, resets in-progress upload state (`mCurrentCrc`, layer/mip/offset tracking), clears the stale upload queue, cleans up unadopted GPU-uploaded texture images, and destroys all Vulkan resources
- `StartThread()` launched by TextureManager before `WaitForTextures()` for IBL cubemaps to begin processing upload requests
- FileManager's loading thread calls `RequestUpload(crc, priority)` after disk-loading a texture chunk (chunk already in `kUploading` state), enqueuing a `LoadRequest` for GPU upload
- Upload thread runs at time-critical priority, dequeues from `std::priority_queue<LoadRequest>`, processing higher-priority uploads first (kRealtime before kNormal)
- Processes one texture at a time, creating the VkImage via VMA on the first chunk, then filling the staging buffer with as many mip/layer regions as fit per frame
- Large textures that exceed the 4MB staging buffer are split across multiple frames, with in-progress state (current layer, mip, Y offset, data offset) persisted between frames
- Sub-mip partial copies supported for compressed formats (BC4/BC7) by splitting at block-row boundaries
- After the final chunk of a texture, sets `LazyChunk::eState` to `kGpuUploadComplete` and notifies FileManager waiters
- After TextureManager calls `Texture::AdoptTransferredImage()`, the GPU handles in the LazyChunk are nulled via `std::exchange` (ownership transferred atomically), and CPU data (`pData`, `iDataSize`) is cleared inline by TextureManager's `ProcessPendingTextures()`
- The upload thread catches `DeviceLostException` to handle GPU device loss gracefully: resets the current texture to `kDiskLoaded` state (preserving CPU data for re-upload) and exits the thread loop, allowing `DestroyTransferResources()` to clean up
- During shutdown, joins the upload thread and cleans up the persistent staging buffer and any GPU-uploaded images not yet adopted by TextureManager

**Per-Frame Pacing**:
- The upload thread sleeps on a `std::binary_semaphore` (`mFrameSignal`) between frames, woken each frame by Graphics::RenderMainPresentAcquire() releasing the semaphore after presentation
- Each frame signal processes exactly one chunk (up to 4MB of staging buffer), then blocks again
- Provides deterministic pacing of GPU texture uploads to avoid VMA allocation stalls on the main thread

**Synchronization**:
- Uses a persistent command buffer (allocated once in `InitTransferResources()`, reset via `vkResetCommandBuffer` before each chunk) to avoid per-upload allocation/free overhead
- After `vkQueueSubmit`, the final chunk waits on the transfer fence before setting `kGpuUploadComplete`, ensuring the release barrier is complete on the GPU before the graphics queue records an acquire barrier
- The fence wait at the start of the next chunk (for staging buffer reuse safety) returns immediately since the fence is already signaled from the previous submission

**Queue Family Ownership Transfer**:
- When transfer and graphics queues are on separate families and QFOT is required: records explicit release barrier on transfer queue, TextureManager's `Texture::AdoptTransferredImage()` records matching acquire barrier on graphics queue
- When QFOT is optional (VK_KHR_maintenance9): release barrier transitions layout directly to SHADER_READ_ONLY_OPTIMAL without ownership transfer, acquire barrier on graphics queue uses IGNORED queue family indices
- Same-family case skips background upload entirely

### TextureManager.h & TextureManager.cpp
**Global**: `gpTextureManager`
**Purpose**: Comprehensive texture and sampler management with lazy loading and deferred descriptor updates

**Lazy Loading System**:
- `InitDeferred()` stores metadata and borrows white placeholder VkImageView (no GPU allocation). White placeholder textures (2D and cube) created at startup provide valid VkImageView for all deferred textures
- Background thread loads actual texture data from disk via FileManager, then TextureUploadManager uploads to GPU on a dedicated thread
- `ProcessPendingTextures()` called after fence wait to finalize pending textures, update descriptors, and propagate new VkImageView to all registered bindings via `UpdateDescriptorsForTexture()`. GPU-uploaded textures (kGpuUploadComplete) are adopted up to 4 per frame to prevent frame spikes when many textures complete simultaneously; after adoption, CPU data pointers (`pData`, `iDataSize`) are cleared inline at the call site. Fallback main-thread creation (kDiskLoaded) is limited to one texture per frame. Both paths set the chunk's `ChunkState` to `kReady` after completion. After all textures are adopted, calls `WriteGlobalDescriptorSets()` to update the global Set 0 texture array. When transfer and graphics queues use separate families, records all QFOT acquire barriers into a single dedicated command buffer (`mAcquireVkCommandBuffer`) that CommandBufferManager prepends before the global command buffer submission
- Uses `ChunkState` (not TextureFlags) to track whether a texture has been fully loaded and adopted. Textures at `kReady` state are skipped
- `WaitForTextures()` synchronously waits for specific textures (used for island textures). Issues `RequestChunkLoad()` with `kRealtime` priority, then spin-waits via `std::this_thread::yield()` calling `ProcessPendingTextures()` until each texture reaches `kReady` state. During spin-wait, signals TextureUploadManager's `mFrameSignal` semaphore (drain-then-release pattern to avoid binary_semaphore double-release UB) so the upload thread can make progress. The `WaitForTextures(span<Texture*>)` overload uses `common::gpThreadLocal->mWorkbuffer` via `Push()`/`PushBack<crc_t>()`/`Span<crc_t>()` to build the CRC array, calling `Pop()` after the call. Flushes any pending acquire barriers immediately via standalone queue submission with fence synchronization, since the render loop's normal submission path is not active
- Priority and non-model texture load requests issued after TextureManager construction and IBL cubemap loading, not during FileManager's `LoadPackFiles()`. Model textures (referenced by scene headers) are deferred until their ModelPipeline first renders with a non-zero instance count, triggered in `ModelPipeline::WriteIndirectBuffer()`. Non-model combined image sampler textures are similarly deferred by `Pipeline::WriteIndirectBuffer()`, which requests chunk loads for all CRCs collected in `mTextureCrcs` on first use

**Screen-Dependent Resource Lifecycle**:
- `DestroyScreenDependentResources()` tears down resources tied to the current screen configuration: global descriptor sets and layout, acquire command pool/buffers, and lighting textures. Enables partial teardown without destroying the full TextureManager during swapchain recreation
- `CreateScreenDependentResources()` rebuilds all screen-dependent resources: lighting, shadow, smoke, wind, object shadow, and terrain detail textures, global descriptor sets, and acquire command pool/buffers. Together with `DestroyScreenDependentResources()`, supports granular resource recreation during swapchain changes while the TextureManager instance and all loaded textures remain alive
- During `kSwapchain`-level recreation, the TextureManager survives (only screen-dependent resources are torn down and rebuilt). During `kSurface`-level recreation (device lost), the full TextureManager is destroyed and reconstructed

**Global Descriptor Set 0**:
- `CreateGlobalDescriptorSet()` creates a shared descriptor set layout and per-framebuffer descriptor sets for Set 0, containing bindings used by all graphics pipelines: uniform buffers (bindings 0-1), repeat sampler (binding 3), unsized bindless texture array (binding 4, with PARTIALLY_BOUND and UPDATE_AFTER_BIND flags), and clamp sampler (binding 12). Layout uses UPDATE_AFTER_BIND_POOL_BIT and is allocated from DeviceManager's single descriptor pool. Called during TextureManager construction after `mImageInfos` is sized. Destructor frees the descriptor sets and destroys the layout
- `WriteGlobalDescriptorSets()` writes all global descriptor set bindings across all framebuffers, including the full `mImageInfos` texture array. Called during initialization by `CreateGlobalDescriptorSet()` and after texture adoption in `ProcessPendingTextures()` to keep the global texture array current
- All non-compute graphics pipelines reference `mGlobalDescriptorSetLayout` as their external Set 0 layout, and bind `mGlobalDescriptorSets[iCommandBuffer]` at set index 0 during rendering. Model pipelines with `kMultiSet` additionally use Set 2 for per-material bindings

**Deferred Descriptor Update System**:
- `RegisterTextureBinding()` tracks which pipelines reference each texture CRC, storing sampler flags and a `Texture*` pointer (or `ppTextures` for array bindings) alongside the binding info. Called during `Pipeline::WriteDescriptorSets()` for model IBL textures, combined image sampler descriptors, runtime textures, and array bindings. Does not trigger chunk loads -- texture loading is demand-driven by `Pipeline::WriteIndirectBuffer()` and `ModelPipeline::WriteIndirectBuffer()` when a pipeline first renders with a non-zero instance count
- `RegisterStandaloneSamplerBinding()` tracks standalone `VK_DESCRIPTOR_TYPE_SAMPLER` bindings that contain only a sampler (no texture). Called during `Pipeline::WriteDescriptorSets()` for sampler-only descriptors (e.g., repeat sampler in model Set 1). Stored in `mStandaloneSamplerBindings` vector
- `UpdateDescriptorsForTexture()` propagates new VkImageView to individual registered bindings when a texture loads. For array bindings (islands), delegates to `WriteArrayBindingDescriptors()`. For single bindings, calls `UpdateCombinedImageSamplerDescriptor()`. Also stores the updated imageView into mImageInfos for the global descriptor set flush
- `WriteArrayBindingDescriptors()` writes combined image sampler descriptors for array bindings (e.g., islands, lighting blur) across all framebuffer descriptor sets. Builds VkDescriptorImageInfo array from `TextureBinding::textures` using the workbuffer, with null-safe fallback to `mWhiteTexture.mVkImageView`. Shared by `UpdateDescriptorsForTexture()` and `RewriteSamplerDescriptors()`
- `RewriteSamplerDescriptors()` performs surgical sampler updates across per-pipeline descriptor sets without pipeline recreation. Iterates `mStandaloneSamplerBindings` calling `Pipeline::UpdateSamplerDescriptor()`, then iterates `mTextureBindings` updating combined image sampler descriptors with new sampler handles while preserving existing image views. Array bindings delegate to `WriteArrayBindingDescriptors()`. Called by `Graphics::Destroy()` at `kSamplers` level when pipelines are not being rebuilt (global Set 0 is updated separately by the caller via `WriteGlobalDescriptorSets()`)
- After all textures are adopted in a frame, calls `WriteGlobalDescriptorSets()` to update the global Set 0 descriptor sets with current texture array contents. When transfer and graphics queues use separate families, records all QFOT acquire barriers into a single dedicated command buffer (`mAcquireVkCommandBuffer`) that CommandBufferManager prepends before the global command buffer submission
- `ClearTextureBindings()` called at pipeline recreation (in PipelineManager constructor) to prevent stale pipeline pointers

**Texture Management**:
- Main texture descriptor array (`mImageInfos`) pre-filled with white placeholder entries sized to `mTextureMap.size()` at construction. Texture array indices are assigned lazily at runtime by `CrcToIndex()`, which maps texture CRCs to descriptor array indices on first use via `mImageInfosMap` and `mNextTextureIndex`. Model material textures are resolved to these indices at material buffer creation time (stored in `PbrMaterialLayout` fields like `fColorTextureIndex`), eliminating the need for per-material combined image sampler descriptors
- UI textures accessed individually via ImGui (`ImGui_ImplVulkan_AddTexture`), not through a descriptor array
- Island textures collected by iterating `gpIslands->smPriorityIslands` (sorted CRC list) for deterministic ordering. Constructor resets static `smPriorityTextures` to its initial size (removing island CRCs appended by previous TextureManager instances), then appends island texture CRCs for current construction. Enables correct texture batch loading across Graphics recreations (swapchain changes, device loss recovery)
- Texture map (`mTextureMap`) indexed by CRC for fast lookup, containing all lazy-loaded textures
- White placeholder textures (2D and cube) provide valid VkImageView for deferred textures

**Render Target Management**:
- MRT lighting system: 3 separate R/G/B textures rendered in single pass with shared render pass and framebuffer
- Lighting blur texture chains with configurable downscale factor and combine index
- Shadow elevation, shadow, and shadow blur textures
- Smoke simulation textures (two ping-pong textures plus gradient)
- Wind simulation textures (both TextureOne and TextureTwo are render targets with LOAD_OP_LOAD, used in a ping-pong pattern where each frame one texture is written via WindSpread and the other is read as input)
- Object shadow and object shadow blur textures
- All with appropriate formats and clear values

**Sampler & Model Support**:
- Eight sampler types: smoke (CLAMP_TO_BORDER, no anisotropy), wind clamp (CLAMP_TO_EDGE, LINEAR filtering, no anisotropy), clamp, border, repeat, mirrored repeat, nearest border
- Anisotropic filtering configurable at runtime, clamped to device limits
- Environment cubemaps for IBL: All three IBL cubemaps are loaded from pre-baked pack data generated offline by DataPacker using CMFT. Irradiance cubemap (from `GenerateIrradianceCubemaps()` using spherical harmonics), plus two pre-filtered radiance cubemaps (from `GeneratePreFilteredCubemaps()`, full mip chain for roughness levels) from different skybox sources (Kloofendal for PBR model reflections, Ryfjallet for water reflections). All three are loaded via `WaitForTextures()` and accessed via CRC-based `mTextureMap` lookups using their `data::` CRC constants (no dedicated pointer members). `miPbrCubeMipCount` is read from the pre-filtered texture's mip level count
- BRDF lookup table computation
- Texture file caching for BRDF LUT with source CRC validation for cache invalidation

**Critical Patterns**:
- All texture updates must occur after fence synchronization
- Proper image layout transitions when loading data
- `CopyImageToHostMemory()` static helper performs GPU-to-CPU image transfer with proper barriers
- Resource recreation when framebuffer count changes

## Vulkan-Specific Patterns & Best Practices

### Resource Synchronization
- Fence wait required before all GPU resource updates (prevents race conditions)
- Pipeline barriers for buffer/image state transitions
- Semaphore chain: Image acquisition → Global → Main → ImGui → Presentation
- Per-framebuffer resource duplication enables parallel frame processing

### Memory Management
- Device-local memory for static vertex data and textures (optimal GPU performance)
- Host-visible memory for dynamic uniform buffers and staging (CPU writable)
- Host-coherent memory for immediate updates without manual flushing
- VMA handles all allocations with proper alignment requirements
- Staging buffers used for host→device transfers

### Descriptor Management
- Single descriptor pool in DeviceManager with both FREE_DESCRIPTOR_SET_BIT and UPDATE_AFTER_BIND_BIT flags, used by all pipelines
- Global Set 0 descriptor sets owned by TextureManager, shared by all non-compute graphics pipelines (uniform buffers at bindings 0-1, repeat sampler at binding 3, bindless texture array at binding 4 with PARTIALLY_BOUND and UPDATE_AFTER_BIND flags, clamp sampler at binding 12). Created via `CreateGlobalDescriptorSet()`, updated via `WriteGlobalDescriptorSets()` after texture adoption
- Each pipeline manages its own descriptor sets for Sets 1 and 2 (freed in Pipeline::Destroy)
- Per-framebuffer descriptor sets for all set levels (dynamic binding)
- Batch descriptor updates before draw calls for efficiency

### Pipeline State
- Pipelines are immutable after creation (~60 specialized pipelines)
- Dynamic state for viewport and scissor (updated per frame)
- Pipeline creation fails if required shader not found in ShaderManager

