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
- Pre-records command buffers for each framebuffer for efficiency
- Supports optional multi-threaded command buffer recording
- Handles screenshot capture functionality
- Key methods: `RecordCommandBuffer()`, `RecordAllCommandBuffers()`, `SubmitGlobalCommandBuffer()`, `SubmitMainCommandBuffer()`

### DeviceManager.h & DeviceManager.cpp  
**Global**: `gpDeviceManager`  
**Purpose**: Manages the logical Vulkan device and queues  
- Creates logical device with required extensions
- Manages graphics and presentation queue handles
- Creates and manages the global descriptor pool
- Provides memory type lookup functionality

### InstanceManager.h & InstanceManager.cpp  
**Global**: `gpInstanceManager`  
**Purpose**: Manages Vulkan instance and physical device selection  
- Creates Vulkan instance with required extensions
- Selects best available physical device (GPU)
- Creates Win32 window surface
- Queries device capabilities, limits, and features
- Manages validation layers in debug builds

### ParticleManager.h & ParticleManager.cpp  
**Global**: `gpParticleManager`  
**Purpose**: GPU-based particle system simulation  
- Spawns particles using compute shaders
- Updates particle physics on GPU
- Manages long particles (trails) and square particles (explosions)
- Key methods: `Spawn()` (static), `RenderGlobal()`

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
- **Integration with Dynamic Textures**: Notifies TextureManager to initialize per-framebuffer texture arrays after framebuffer creation
- Key methods: `AcquireNextImage()`, `Present()`, `ReduceInputLag()`

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

- **Dynamic Texture Arrays**:
  - Per-framebuffer texture arrays for runtime updates
  - Initialized with pre-sized empty textures at startup
  - No descriptor set updates needed when data loads (VkImageView unchanged)
  - Separate arrays for main textures and UI textures
  - Particle and island texture pointers set during construction
  
- **Render Target Management**:
  - Lighting textures (R/G/B separate for bandwidth optimization)
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
- `UpdateTextureSlot()` - Updates texture array binding (rarely used now)
- `InitializePerFrameTextureArrays()` - Initializes descriptor arrays with pre-sized empty textures
- `CreateLightingTextures()` - Multi-resolution blur chain
- `GetSampler()` - Returns appropriate sampler for descriptor flags

**Texture Streaming Flow**:
1. Constructor: Create empty textures from ChunkHeader (correct size/format, no data)
2. Constructor: Initialize descriptor arrays and particle/island pointers with empty textures
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
- **Dynamic Binding**: Per-framebuffer descriptor sets for texture arrays
- **Update Pattern**: Batch descriptor updates before draw calls
- **Lifetime**: Descriptor sets tied to framebuffer lifetime

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