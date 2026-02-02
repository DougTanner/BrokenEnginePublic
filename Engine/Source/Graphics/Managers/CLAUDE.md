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
- MeshData storage buffer for glTF skeletal animation metadata (host-visible for CPU updates during Render)
- Joint matrices storage buffer for glTF skeletal animation (host-visible, initialized with identity matrices)

**Dynamic Buffer Creation**:
- Collections register storage buffers during CreatePipelines() via CreateDynamicBuffer() method
- Accepts CRC key, buffer name, and element size in bytes (stored for type validation)
- Creates per-framebuffer storage buffers with {kStorage, kHostVisible} flags
- Buffers stored in `mDynamicStorageBuffers` unordered_map indexed by CRC for direct lookup
- Silently returns if buffer with CRC already exists (idempotent)

**Type-Safe Buffer Access**:
- `GetDynamicStorageBuffer<T>(crc, iCommandBuffer)` provides type-safe access to mapped memory
- Runtime assertion validates `sizeof(T)` matches the element size stored at buffer creation
- Catches size mismatch bugs (e.g., creating buffer with wrong struct size) at first access in debug builds
- Replaces unsafe direct `reinterpret_cast` access pattern used by collections

**Dynamic Buffer Resizing**:
- ResizeDynamicBuffer() recreates buffers with new size using deferred destruction pattern
- Old buffer moved to `mPreviousBuffer` storage, keeping it alive until next resize
- Deferred destruction prevents Vulkan validation errors from command buffers referencing destroyed resources
- Caller must ensure fence synchronization before calling (buffer must not be in GPU use)
- After resize, caller updates descriptor sets; command buffer re-recording not needed for pipelines with update-after-bind enabled

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
- Global (primary): Pre-processing passes (shadows, terrain generation, smoke spread, particle spawn/update)
- Main (primary): Pre-processing (lighting, lighting blur, smoke emit, object shadows, object shadows blur) then main render pass

**Main Render Pass Order**:
glTF objects, terrain, water, hex shields, particles (long then square), visible lights, billboards, text. Hex shields render after water for correct transparency blending with water surface.

**Host-to-Shader Synchronization**:
- Memory barrier placed immediately after uniform buffer copy, before any indirect draws
- Ensures CPU-written animation data (joint matrices, mesh data, indirect draw buffers) is visible to all GPU consumers
- Uses `VK_ACCESS_HOST_WRITE_BIT` to `VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT` with `VK_PIPELINE_STAGE_HOST_BIT` to `VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT`
- Protects all indirect draws: lighting passes, smoke emit, glTF shadow pass, and main render pass glTF objects
- Early barrier placement eliminates CPU/GPU race conditions across all rendering passes

**Key Features**:
- MRT lighting pass outputs to 3 color attachments simultaneously (R/G/B channels)
- Synchronization via semaphores (Global → Main) and fences (frame-to-frame)
- Optimized pipeline barriers with minimal stage masks for GPU efficiency
- Optional multi-threaded submission support (kbEnableRenderThread)
- Screenshot capture integration (ENABLE_SCREENSHOTS)
- Dynamic pipelines iterated via maps (mDynamicPipelinesLightingMap, mDynamicPipelinesAxisAlignedLightingMap, mDynamicPipelinesHexShieldsLightingMap, mDynamicPipelinesSmokeAxisAlignedMap, mDynamicPipelinesSmokeMap, mDynamicGltfPipelineShadowMap, mDynamicGltfPipelineMap, mDynamicPipelinesHexShieldsMap, mDynamicPipelinesVisibleLightsMap, mDynamicPipelinesBillboardsMap)

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
- Uses dedicated command buffer from CommandBuffers structure

**Frame Flow**:
- `Submit()`: Handles entire ImGui frame cycle - begins frame (NewFrame calls), delegates UI content rendering to screen classes, finalizes draw data, records command buffer, and submits to GPU queue

**Screen Delegation**:
- Hosts HUD (HudScreen), menu screens (MainMenuScreen, PauseMenuScreen, GraphicsMenuScreen, SoundMenuScreen, DeathMenuScreen), and debug screens (TweaksScreen)
- ImGuiManager owns screen instances and calls their `Render()` methods during the frame
- See [Ui/Screens/CLAUDE.md](../../Ui/Screens/CLAUDE.md) for screen documentation

### DeviceManager.h & DeviceManager.cpp
**Global**: `gpDeviceManager`
**Purpose**: Manages the logical Vulkan device, queues, and GPU memory allocation

**Key Responsibilities**:
- Creates logical device with required extensions and Vulkan 1.2 features
- Calls `volkLoadDevice()` immediately after device creation to load device-specific function pointers
- Manages graphics and presentation queue handles
- Initializes VMA (Vulkan Memory Allocator) with optional memory budget extension for VRAM tracking
- Creates two descriptor pools for different usage patterns
- Provides memory type lookup for buffer/texture allocation

**Dual Descriptor Pool Architecture**:
- **Main pool** (`mVkDescriptorPool`): Standard descriptors for static pipelines with FREE_DESCRIPTOR_SET_BIT
- **Update-after-bind pool** (`mVkDescriptorPoolUpdateAfterBind`): For dynamic pipelines that update descriptors after command buffer recording, with UPDATE_AFTER_BIND_BIT flag
- Separation isolates update-after-bind pipelines from static pipelines with zero impact on existing code

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
- Uses `VK_EXT_layer_settings` extension to configure Khronos validation layer
- Debug layers controlled by `kbEnableVulkanDebugLayers` constexpr bool (defined in game Pch.h)
- Supports GPU-Assisted Validation (`kbEnableGpuAssistedValidation`) for runtime shader instrumentation
- Supports Debug Printf (`kbEnableDebugPrintf`) and shader realtime clock (`kbEnableShaderRealtimeClock`) via constexpr bools from ShaderLayoutsBase.h
- GPU validation modes are mutually exclusive (GPU can only run one at a time)
- Uses `if constexpr` for compile-time elimination of debug code paths

**Volk Integration**:
- Calls `volkLoadInstance()` immediately after instance creation to load instance-specific function pointers
- For debug builds: Create instance → volkLoadInstance → create debug messenger
- Volk automatically loads all extension functions including debug utils

**Critical Behavior**:
- Requires Vulkan 1.2 driver (shows error and terminates if not available)
- Validates required Vulkan 1.2 features (e.g., `descriptorBindingStorageBufferUpdateAfterBind`) with MessageBox error if unsupported
- Retries without validation layers if Vulkan SDK not installed (driver still required)

### ParticleManager.h & ParticleManager.cpp
**Global**: `gpParticleManager`
**Purpose**: GPU-based particle system using compute shaders for spawning and physics simulation

**Architecture**:
- Two particle types: Long particles (trails) and square particles (explosions)
- Both types share the same compute shaders (ParticlesSpawn.comp, ParticlesUpdate.comp)
- Fixed capacity of 16,384 particles per type with bitfield allocation tracking
- Fully GPU-driven: Spawn and update run as compute shaders, no CPU involvement in physics

**Compute Pipeline**:
- Spawn shader: Single-threaded (workgroup size 1) for sequential slot allocation
- Update shader: Multi-threaded (workgroup size 32) for parallel physics simulation via indirect dispatch
- Spawn updates indirect dispatch buffer consumed by update shader
- Update handles position, velocity, gravity, collision, and decay

**Design Rationale**:
- Spawn must be single-threaded due to sequential allocation algorithm (parallelization overhead exceeds benefits)
- Update is fully parallel because particles are independent with no inter-particle dependencies

### PipelineManager.h & PipelineManager.cpp
**Global**: `gpPipelineManager`
**Purpose**: Creates and manages all graphics and compute pipelines

**Architecture**:
- Static pipelines stored in fixed-size array indexed by enum
- Dynamic pipelines stored in vector of unique_ptr for collection-specific rendering
- Collections register pipelines during CreatePipelines() phase

**Key Responsibilities**:
- Creates 60+ specialized static pipelines for different rendering passes
- Manages lighting blur pipeline chains (separate R/G/B channels)
- Integrates glTF PBR rendering pipelines
- Creates shadow, terrain, particle, smoke, and UI pipelines
- Calls game::Frame::CreatePipelines() to allow collections to register dynamic pipelines

**Dynamic Pipeline Pattern**:
- Collections use unique_ptr to store non-copyable Pipeline objects
- Collections cache pipeline index in static member for later access
- Enables per-collection pipeline customization without enum pollution
- Multiple CRC→Pipeline* maps for different pipeline types:
  - mDynamicPipelinesLightingMap / mDynamicPipelinesAxisAlignedLightingMap for lighting
  - mDynamicPipelinesHexShieldsMap / mDynamicPipelinesHexShieldsLightingMap for hex shields
  - mDynamicPipelinesSmokeAxisAlignedMap / mDynamicPipelinesSmokeMap for smoke emit
  - mDynamicPipelinesVisibleLightsMap for visible light billboards
  - mDynamicPipelinesBillboardsMap for UI billboards
  - mDynamicGltfPipelineMap / mDynamicGltfPipelineShadowMap for glTF objects

**Smoke Pipeline Creation**:
- CreateDynamicPipelineSmokeAxisAligned() and CreateDynamicPipelineSmoke() create smoke emitter pipelines
- Smoke pipelines render to smoke emit pass (mSmokeTextureOne render pass) with additive blending
- Axis-aligned variant uses QuadsAxisAlignedVisibleAreavertCrc, generic variant uses QuadsVisibleAreavertCrc
- Both variants use Smoke.frag shader

**glTF Pipeline Creation**:
- CreateGltfPipeline() creates single pipeline (regular or shadow) with GltfPipelineSpec
- CreateDynamicGltfPipeline() and CreateDynamicGltfPipelineShadow() accept collection CRC, name, glTF CRC, and storage buffers
- Model buffer CRC is looked up at runtime from the GltfHeader's `modelCrc` field, eliminating duplicate CRC parameters
- Shadow pipelines appended with "Shadow" suffix and stored in mDynamicGltfPipelineShadowMap
- Regular pipelines use main render pass with depth test/write, sample shading, and glTF descriptors
- Shadow pipelines use object shadows render target with minimal descriptor sets

**Shader Dependencies**:
- Each pipeline requires specific shaders from ShaderManager
- Shaders referenced via `gpShaderManager->mShaders.at(crc)` - crashes if shader not found
- All critical shaders must be loaded at startup (no fallback or lazy loading)
- Pipeline recreation requires shader availability

### ShaderManager.h & ShaderManager.cpp
**Global**: `gpShaderManager`
**Purpose**: Loads and caches compiled SPIR-V shader modules

**Architecture**:
- Loads all SPIR-V bytecode from chunk map at startup (no lazy loading)
- Creates VkShaderModule objects stored in map indexed by CRC
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

### TextureManager.h & TextureManager.cpp
**Global**: `gpTextureManager`
**Purpose**: Comprehensive texture and sampler management with lazy loading

**Lazy Loading System**:
- Creates pre-sized empty textures at startup from ChunkHeader metadata (correct dimensions/format/mips)
- Background thread loads actual texture data from disk
- `ProcessPendingTextures()` called after fence wait to update textures in-place
- VkImageView references remain constant - no descriptor set updates needed when data loads
- Island textures requested with high priority
- No placeholder artifacts - shaders always see correctly-sized textures

**Texture Management**:
- Separate descriptor arrays for main textures and UI textures
- UI texture array padded to match shader array size (Vulkan requires all descriptor array elements be written)
- Particle and island texture pointers initialized during construction
- Texture map indexed by CRC for fast lookup

**Render Target Management**:
- MRT lighting system: 3 separate R/G/B textures rendered in single pass with shared render pass and framebuffer
- Shadow elevation and blur textures
- Smoke simulation textures
- Object shadow textures
- All with appropriate formats and clear values

**Sampler & glTF Support**:
- Multiple sampler types (linear, point, clamp, repeat, border, mirrored repeat) with anisotropic filtering
- Environment cubemap generation for IBL
- BRDF lookup table and irradiance map computation
- Texture caching for glTF assets

**Critical Patterns**:
- All texture updates must occur after fence synchronization
- Proper image layout transitions when loading data
- `CopyImageToHostMemory()` static helper performs GPU→CPU image transfer with proper barriers
- Resource recreation when framebuffer count changes

## Vulkan-Specific Patterns & Best Practices

### Resource Synchronization
- Fence wait required before all GPU resource updates (prevents race conditions)
- Pipeline barriers for buffer/image state transitions
- Semaphore chain: Image acquisition → Rendering → Presentation
- Per-framebuffer resource duplication enables parallel frame processing

### Memory Management
- Device-local memory for static vertex data and textures (optimal GPU performance)
- Host-visible memory for dynamic uniform buffers and staging (CPU writable)
- Host-coherent memory for immediate updates without manual flushing
- VMA handles all allocations with proper alignment requirements
- Staging buffers used for host→device transfers

### Descriptor Management
- Dual descriptor pools in DeviceManager: main pool with FREE_DESCRIPTOR_SET_BIT, update-after-bind pool with UPDATE_AFTER_BIND_BIT
- Each pipeline manages its own descriptor sets (freed in Pipeline::Destroy)
- Per-framebuffer descriptor sets for texture arrays (dynamic binding)
- Batch descriptor updates before draw calls for efficiency

### Pipeline State
- Pipelines are immutable after creation (~60 specialized pipelines)
- Dynamic state for viewport and scissor (updated per frame)
- Pipeline creation fails if required shader not found in ShaderManager

