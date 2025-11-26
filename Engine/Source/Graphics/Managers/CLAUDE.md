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

**Dynamic Buffer Creation**:
- Collections register storage buffers during CreatePipelines() via CreateDynamicBuffer() method
- Accepts CRC key, buffer name, and pre-calculated size in bytes
- Creates per-framebuffer storage buffers with {kStorage, kHostVisible} flags
- Buffers stored in `mDynamicStorageBuffers` unordered_map indexed by CRC for direct lookup
- Silently returns if buffer with CRC already exists (idempotent)
- Access pattern: `mDynamicStorageBuffers.at(common::Crc("BufferName"))[iCommandBuffer]`

**Dynamic Buffer Resizing**:
- ResizeDynamicBuffer() destroys and recreates a buffer with new size for a specific framebuffer
- Caller must ensure fence synchronization before calling (buffer must not be in GPU use)
- Designed for per-framebuffer stall-free updates as each framebuffer's fence clears independently
- After resize, caller must update descriptor sets and request command buffer re-recording

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
- Double-buffering enables parallel GPU/CPU work
- VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT NOT used - recordings are reusable

**Command Buffer Types**:
- Global (primary): Pre-processing passes (shadows, terrain generation, smoke spread, particle spawn/update)
- Image (primary): Orchestrates main render pass execution and secondary buffer invocation
- Secondary command buffers: Static lighting pipelines (PointLights, HexShieldsLighting, LongParticlesLighting, SquareParticlesLighting), 1 scene buffer (grouped non-glTF/non-lighting rendering), plus dynamically allocated per-pipeline buffers

**Secondary Command Buffer Organization**:
- Dynamic pipelines (GltfPipeline, dynamic lighting) own secondary buffers allocated via Pipeline::AllocateSecondaryBuffers()
- Static lighting secondary buffers allocated in CommandBuffers for per-light-type selective re-recording
- Scene secondary buffer groups all non-glTF/non-lighting rendering (terrain, water, visible lights, widgets, text) for efficiency
- SecondaryBufferSpec struct encapsulates render pass, framebuffer, buffer pointer, and recording callback
- Generic RecordSecondary() method handles common boilerplate (begin/end command buffer) and invokes spec-specific callback
- mSceneSecondarySpecs populated during startup; dynamic lighting iterates mDynamicPipelinesLightingMap directly
- Lighting secondary buffers executed within MRT lighting render pass (uses VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS)
- Main render pass secondary buffers (glTF + Scene) executed within swapchain render pass (uses VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS)
- Foundation for future multithreaded command recording

**Key Features**:
- AllocateSecondaryBuffer() creates secondary command buffers from existing command pools
- MRT lighting pass outputs to 3 color attachments simultaneously (R/G/B channels)
- Synchronization via semaphores (Global → Image) and fences (frame-to-frame)
- Optimized pipeline barriers with minimal stage masks for GPU efficiency
- Optional multi-threaded submission support
- Screenshot capture integration

**Selective Re-recording**:
- RequestRerecord() clears the recorded flag for a specific framebuffer and slot
- Next frame's RecordCommandBuffers() will re-record that slot when flag is false
- Enables runtime command buffer updates after buffer/descriptor changes
- Caller specifies framebuffer index and slot (0 or 1 for double-buffered command buffers)

### DeviceManager.h & DeviceManager.cpp
**Global**: `gpDeviceManager`
**Purpose**: Manages the logical Vulkan device, queues, and GPU memory allocation

**Key Responsibilities**:
- Creates logical device with required extensions
- Calls `volkLoadDevice()` immediately after device creation to load device-specific function pointers
- Manages graphics and presentation queue handles
- Initializes VMA (Vulkan Memory Allocator) with optional memory budget extension for VRAM tracking
- Creates global descriptor pool with FREE_DESCRIPTOR_SET_BIT flag for flexible pipeline management
- Provides memory type lookup for buffer/texture allocation

**VMA Integration**:
- All GPU memory allocation handled through VMA
- Supports Vulkan 1.1+ features and optional VK_EXT_memory_budget extension
- Each buffer/texture allocation goes through VmaAllocator

### InstanceManager.h & InstanceManager.cpp
**Global**: `gpInstanceManager`
**Purpose**: Manages Vulkan instance and physical device selection

**Key Responsibilities**:
- Creates Vulkan instance with required extensions (Vulkan 1.1+ required, no fallback to 1.0)
- Selects best available physical device (GPU)
- Creates Win32 window surface
- Queries device capabilities, limits, features, and queue family properties
- Validates and stores surface format and color space pairs
- Manages validation layers in debug builds

**Volk Integration**:
- Calls `volkLoadInstance()` immediately after instance creation to load instance-specific function pointers
- For debug builds: Create instance → volkLoadInstance → create debug messenger
- Volk automatically loads all extension functions including debug utils

**Critical Behavior**:
- Requires Vulkan 1.1 driver (shows error and terminates if not available)
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
- Dynamic lighting pipelines own their secondary buffers (allocated via Pipeline::AllocateSecondaryBuffers())
- mDynamicPipelinesLightingMap stores CRC→Pipeline* mappings for lighting pipeline iteration

**glTF Pipeline Creation**:
- CreateGltfPipeline() creates single pipeline (regular or shadow) with GltfPipelineSpec
- For non-shadow pipelines: allocates secondary command buffers and registers SecondaryBufferSpec for each framebuffer
- CreateGltfPipelinePair() creates both regular and shadow pipelines in single call with GltfPipelinePairSpec
- GltfPipelinePairSpec accepts name, glTF CRC, model vertex buffer CRC, and storage buffers
- CreateGltfPipelinePair() constructs shadow pipeline name by appending "Shadow" to regular pipeline name
- Returns GltfPipelinePair struct with pointers to both pipelines
- Non-shadow pipelines registered in mRegisteredGltfPipelines with per-pipeline secondary buffers
- Shadow pipelines registered in mRegisteredGltfShadowPipelines (render directly in primary command buffer)
- Regular pipeline uses main render pass with depth test/write, sample shading, and glTF descriptors
- Shadow pipeline uses object shadows render target with minimal descriptor sets (no glTF descriptors)

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
- Single descriptor pool in DeviceManager with FREE_DESCRIPTOR_SET_BIT flag
- Each pipeline manages its own descriptor sets (freed in Pipeline::Destroy)
- Per-framebuffer descriptor sets for texture arrays (dynamic binding)
- Batch descriptor updates before draw calls for efficiency

### Pipeline State
- Pipelines are immutable after creation (~60 specialized pipelines)
- Dynamic state for viewport and scissor (updated per frame)
- Pipeline creation fails if required shader not found in ShaderManager

