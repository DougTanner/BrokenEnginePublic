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

## Core Managers

### BufferManager.h & BufferManager.cpp  
**Global**: `gpBufferManager`  
**Purpose**: Manages GPU buffers for vertex data, uniform buffers, and storage buffers  
- Creates and stores terrain and water mesh buffers
- Manages uniform buffers for global constants and view/projection matrices
- Manages storage buffers for lights, hex shields, billboards, text, widgets, smoke, and particles
- Stores model vertex buffers indexed by CRC
- Key methods: `CreateTerrainMesh()`, `CreateWaterMesh()`

### CommandBufferManager.h & CommandBufferManager.cpp
**Global**: `gpCommandBufferManager`
**Purpose**: Records and submits Vulkan command buffers
- **Architecture**: "Record-once, submit-many" pattern for optimal performance
  - Command buffers recorded **once at startup** in Graphics::Create()
  - Same recordings **resubmitted every frame** without re-recording
  - Only re-recorded when CommandBufferManager destroyed/recreated (window resize, device lost, settings changes)
  - Double-buffering (kiCommandBuffersPerFramebuffer = 2) enables parallel GPU/CPU work
- **VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT NOT used** - each recording submitted multiple times
- **Two command buffer types** per slot:
  - **Global**: Pre-processing (shadows, terrain generation, smoke spread, particle spawn/update)
  - **Image**: All rendering (lighting MRT pass, blur cascades, object shadows, scene rendering to swapchain)
- **MRT Lighting Pass**: Single render pass outputs to 3 color attachments simultaneously (R/G/B lighting channels)
  - 5 lighting draw calls per frame (area lights, point lights, hex shields, long particles, square particles)
  - No per-channel push constants needed - shaders compute all channels in parallel
- **Synchronization**: Global → Image via semaphore, Image waits on both Global completion and swapchain image available
- **Pipeline Barriers**: Optimized stage masks for efficient GPU synchronization
  - Shadow system uses compute-to-compute read-only transitions for texture cache optimization
  - Particle system uses storage buffer barriers covering both vertex and fragment shader stages
  - Batched barrier recording reduces vkCmdPipelineBarrier overhead
- Supports optional multi-threaded command buffer submission
- Handles screenshot capture functionality
- Key methods: `RecordCommandBuffer()`, `RecordCommandBuffers()`, `SubmitGlobalCommandBuffer()`, `SubmitImageCommandBuffer()`

### DeviceManager.h & DeviceManager.cpp
**Global**: `gpDeviceManager`
**Purpose**: Manages the logical Vulkan device, queues, and GPU memory allocation
- Creates logical device with required extensions
- **Volk Integration**: Calls `volkLoadDevice(mVkDevice)` immediately after device creation to load device-specific function pointers
- Queries and conditionally enables VK_EXT_memory_budget extension for VMA
- Manages graphics and presentation queue handles
- Creates and manages the global descriptor pool
  - Created with VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT flag to enable individual descriptor set freeing
  - Each pipeline frees its own descriptor sets in Pipeline::Destroy()
  - Allows individual pipeline recreation without destroying entire pool (e.g., shader hot-reload)
- Provides memory type lookup functionality
- **VMA Integration**: Initializes Vulkan Memory Allocator after device creation with Vulkan 1.1+ features and optional VK_EXT_memory_budget extension for VRAM tracking
- **Key Members**:
  - `mbMemoryBudgetAvailable` - Flag indicating if memory budget extension is supported
  - `mpAllocator` - VmaAllocator handle for memory allocation
  - `mVmaFunctions` - VMA function pointer table

### InstanceManager.h & InstanceManager.cpp
**Global**: `gpInstanceManager`
**Purpose**: Manages Vulkan instance and physical device selection
- Creates Vulkan instance with required extensions
- **Vulkan Version Requirement**: Vulkan 1.1 or higher is REQUIRED (no fallback to 1.0)
  - Shows error message box and terminates if Vulkan 1.1 not available
  - Retries without validation layers if Vulkan SDK not installed (still requires 1.1 driver)
- Selects best available physical device (GPU)
- Creates Win32 window surface
- Queries device capabilities, limits, and features
- **Queue Family Properties**: Stores queue family properties in `mVkQueueFamilyProperties` for validation and feature checks (e.g., timestamp support)
- **Surface Format and Color Space Validation**: Queries supported surface formats and stores both format and color space
  - Prefers VK_FORMAT_B8G8R8A8_UNORM or VK_FORMAT_R8G8B8A8_UNORM
  - Stores the associated color space from the surface format query
  - Ensures the (format, colorSpace) pair is validated against surface capabilities
- Manages validation layers in debug builds
- **Volk Integration**: Calls `volkLoadInstance(mVkInstance)` immediately after successful instance creation to load instance-specific function pointers
- **Debug Utils Initialization Order** (CRITICAL):
  1. Read validation layer properties
  2. Create VkInstance with VK_EXT_DEBUG_UTILS_EXTENSION_NAME enabled
  3. Call `volkLoadInstance()` to load all instance functions (including debug utils)
  4. Create debug messenger using Volk's global function pointers
  - **NOTE**: Volk automatically loads all extension functions, including debug utils

### ParticleManager.h & ParticleManager.cpp
**Global**: `gpParticleManager`
**Purpose**: GPU-based particle system simulation
- Spawns particles using compute shaders
- Updates particle physics on GPU
- Manages long particles (trails) and square particles (explosions)
- Key methods: `Spawn()` (static), `RenderGlobal()`

**Particle System Architecture**:
- **Two Particle Types**: Long particles (trails) and square particles (explosions) both using same compute shaders
- **Shared Shaders**: ParticlesSpawn.comp and ParticlesUpdate.comp used by both particle types
- **Maximum Capacity**: 16,384 particles per type (kiMaxParticles)
- **Storage**: Fixed-size arrays with bitfield allocation tracking

**Compute Shader Details**:
- **ParticlesSpawn.comp**:
  - Workgroup size: 1 (single-threaded by design)
  - Dispatch: 1 workgroup per frame
  - Purpose: Sequential slot allocation for new particles
  - Critical: MUST remain single-threaded due to sequential allocation algorithm
  - Updates dispatch indirect buffer for ParticlesUpdate compute shader

- **ParticlesUpdate.comp**:
  - Workgroup size: 32 (parallelized for GPU efficiency)
  - Dispatch: ceil(particle_count / 32) workgroups via indirect dispatch
  - Purpose: Independent per-particle physics simulation
  - Operations: Position, velocity, gravity, collision, decay
  - Synchronization: Single atomic operation for free index tracking

**Why Spawn is Single-Threaded**:
- Uses sequential search starting from tracked free index (particles.i4Misc.y)
- Updates shared allocation state during search
- Parallelization would require expensive synchronization that eliminates benefits
- Current approach is optimal for spawn workload

**Why Update is Multi-Threaded**:
- Each particle reads/writes only its own data
- No inter-particle dependencies or interactions
- Physics operations are fully independent
- Single atomic operation protects shared state (minimum free index)
- Execution order does not affect results

### PipelineManager.h & PipelineManager.cpp  
**Global**: `gpPipelineManager`  
**Purpose**: Creates and manages all graphics and compute pipelines  
- Creates 60+ specialized pipelines for different rendering passes
- Manages lighting blur pipeline chains (separate R/G/B)
- Creates shadow rendering pipelines
- Integrates glTF PBR rendering pipelines
- **Shader Dependencies**: Each pipeline requires specific shaders from ShaderManager
  - References shaders via `gpShaderManager->mShaders.at(crc)`
  - Will crash if required shader not found in map
  - Shader modules passed to CreatePipeline/CreateComputePipeline
  - Critical shaders loaded at startup, no fallback mechanism
- Key methods: `CreateLightingPipelines()`, `CreateShadowPipelines()`, `CreateLightingShadowDependantPipelines()`

### ShaderManager.h & ShaderManager.cpp  
**Global**: `gpShaderManager`  
**Purpose**: Loads and caches compiled shader modules  
- Loads SPIR-V bytecode from chunk map at startup
- Creates VkShaderModule objects in Shader constructor
- Stores shaders in `mShaders` map indexed by CRC
- **CRITICAL**: All shaders loaded immediately during construction
- **WARNING**: No lazy loading or fallback - missing shader causes pipeline creation crash
- **Pattern**: Shaders referenced by CRC via `mShaders.at(crc)` - throws if not found

### SwapchainManager.h & SwapchainManager.cpp
**Global**: `gpSwapchainManager`
**Purpose**: Manages swap chain presentation and frame synchronization
- Creates and recreates swap chain on window resize
- Manages framebuffers for each swap chain image
- Creates depth and multisampling textures
- Handles frame synchronization with semaphores and fences
- Uses validated surface format and color space from InstanceManager
- Key methods: `AcquireNextImage()`, `Present()`

### TextManager.h & TextManager.cpp  
**Global**: `gpTextManager`  
**Purpose**: Efficient text rendering system  
- Manages character maps for EFIGS and Chinese fonts
- Updates text areas for debug, graphics stats, and profile info
- Batches text quads for efficient rendering
- Key methods: `GetCharacter()`, `UpdateTextArea()`, `RenderMain()`, `MeasureQuads()`, `WriteQuads()`

### TextureManager.h & TextureManager.cpp  
**Global**: `gpTextureManager`  
**Purpose**: Comprehensive texture and sampler management with lazy loading  

**Core Features**:
- **Lazy Loading System**:
  - Pre-sized empty textures created at startup from ChunkHeader metadata
  - Textures have correct dimensions/format/mips before data loads
  - Background thread loads actual texture data from disk
  - `ProcessPendingTextures()` called after fence wait to update textures in-place
  - Island textures requested with high priority
  - Maintains set of requested textures to avoid duplicate requests
  - No placeholder artifacts - shaders see correctly-sized textures throughout

- **Texture Descriptor Management**:
  - Single texture array shared across all framebuffers
  - VkImageView references remain constant throughout texture lifetime
  - No descriptor set updates needed when data loads (VkImageView unchanged)
  - Separate arrays for main textures and UI textures
  - **CRITICAL**: UI texture array (`mUiImageInfos`) padded to `kiMaxTextureCount` (184) to match shader expectations
    - Widgets shader declares `uniform texture2D pTextures[kiMaxTextureCount]`
    - Vulkan requires ALL descriptor array elements be written, even if unused
    - Padding uses first UI texture as placeholder to fill remaining slots
    - Without padding: validation error "descriptor Index X has never been updated"
  - Particle and island texture pointers set during construction

- **Render Target Management**:
  - **MRT Lighting System**: 3 separate lighting textures (R/G/B) rendered in a single pass using Multiple Render Targets
    - `mpLightingTextures[3]` - Individual R16G16B16A16_SFLOAT textures for each color channel
    - `mLightingVkRenderPass` - Shared MRT render pass with 3 color attachments
    - `mLightingVkFramebuffer` - Shared framebuffer binding all 3 lighting textures
    - Reduces draw calls by 66% (15→5) compared to previous 3-pass approach
    - All lighting shaders output to 3 MRT locations simultaneously
  - Shadow elevation and blur textures
  - Smoke simulation textures
  - Object shadow textures
  - All with appropriate formats and clear values
  
- **Sampler Creation**:
  - Multiple sampler types: linear, point, clamp, repeat, border
  - Anisotropic filtering support
  - Specialized smoke sampler with custom LOD bias
  
- **glTF Support**:
  - Environment cubemap generation for IBL
  - BRDF lookup table generation
  - Irradiance map computation
  
**Critical Patterns**:
- Texture updates only after fence synchronization
- Proper image layout transitions for new textures
- Resource recreation on framebuffer count changes
- Memory barrier handling for compute-to-graphics transitions

**Key Methods**:
- `ProcessPendingTextures()` - Checks for loaded chunks and updates textures in-place via Texture::UpdateData()
- `WaitForTextures(std::span<const common::crc_t>)` - Blocks until textures loaded, then updates all data
- `WaitForTextures(std::span<Texture* const>)` - Overload accepting texture pointers (calls CRC version)
- `LoadTextureChunk()` - Updates existing texture data when chunk loaded (no longer creates new textures)
- `CreateLightingTextures()` - Multi-resolution blur chain
- `GetSampler()` - Returns appropriate sampler for descriptor flags
- `SaveTextureToCache()` - Saves texture data from GPU to cache file (uses CopyImageToHostMemory helper)
- `TryLoadCachedTexture()` - Loads texture data from cache file if available

**Helper Functions**:
- `CombineTextureInfo()` - Returns lighting combine texture index and blur count
- `CopyImageToHostMemory()` - Static helper that performs complete GPU→CPU image transfer
  - Takes boolean parameter to distinguish between shader textures (false) and swapchain images (true)
  - Automatically selects appropriate layouts, pipeline stages, and access masks based on source type
  - Creates staging buffer for data transfer
  - Handles image layout transitions with proper barriers
  - Copies all mip levels and array layers
  - Maps memory and returns data via std::vector
  - Automatically cleans up staging resources
  - Used by SaveTextureToCache() and SaveScreenshot()

**Texture Streaming Flow**:
1. Constructor: Create empty textures from ChunkHeader (correct size/format, no data)
2. Constructor: Build descriptor arrays and set particle/island texture pointers
3. Runtime: Background thread loads chunk data from disk
4. ProcessPendingTextures: Call Texture::UpdateData() to upload data in-place
5. Result: No descriptor updates, no pointer updates, seamless transition

## Vulkan-Specific Patterns & Best Practices

### Resource Synchronization
- **Fence Wait Required**: All GPU resource updates must occur after fence wait
- **Pipeline Barriers**: Proper barriers for buffer/image transitions
- **Semaphore Chain**: Image acquisition → Rendering → Presentation
- **Multi-Frame**: Resources duplicated per framebuffer for parallel frame processing

### Memory Management
- **Buffer Types**:
  - Device Local: Static vertex data, textures (optimal performance)
  - Host Visible: Dynamic uniform buffers, staging (CPU writable)
  - Host Coherent: Immediate updates without flushing
- **Alignment**: Uniform buffers require minUniformBufferOffsetAlignment
- **Staging**: Host→Device transfers via staging buffers

### Descriptor Management
- **Descriptor Pool**: Single pool in DeviceManager for all sets
  - Created with VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT flag
  - Individual descriptor set freeing enabled for flexible pipeline management
  - Each Pipeline frees its descriptor sets in Pipeline::Destroy()
- **Dynamic Binding**: Per-framebuffer descriptor sets for texture arrays
- **Update Pattern**: Batch descriptor updates before draw calls
- **Lifetime**: Descriptor sets tied to pipeline lifetime
  - Each pipeline manages its own descriptor sets
  - Pool destruction handles any remaining sets via implicit freeing

### Pipeline State
- **Immutable State**: Pipelines cannot be modified after creation
- **Specialization**: ~60 specialized pipelines for different passes
- **Dynamic State**: Viewport/scissor updated per frame
- **Shader Dependencies**: Pipeline creation fails if shader not loaded

### Common Pitfalls
1. **Race Conditions**: Updating resources still in use by GPU
2. **Missing Barriers**: Incorrect resource state transitions
3. **Descriptor Limits**: Exceeding pool or set layout limits
4. **Memory Leaks**: Not destroying Vulkan objects
5. **Validation Errors**: Enable validation layers in debug builds

### Performance Considerations
- **Command Buffer Recording**: Pre-record where possible
- **Batch Operations**: Group similar draw calls
- **Memory Barriers**: Minimize with proper resource planning
- **Texture Arrays**: Reduce descriptor set switches
- **Compute Overlap**: Utilize async compute for particles