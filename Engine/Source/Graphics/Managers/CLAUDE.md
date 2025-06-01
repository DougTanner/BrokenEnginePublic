# /Engine/Source/Graphics/Managers/

The `/Engine/Source/Graphics/Managers/` directory contains manager classes that handle high-level graphics resources and operations for the Vulkan renderer. All managers follow a singleton pattern with global pointers initialized during Graphics construction.

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
- All shaders loaded immediately during construction
- No lazy loading - if shader missing at startup, will fail during pipeline creation

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
**Purpose**: Loads and manages textures and samplers  
- Loads BC4/BC7 compressed textures from Data.bin
- Creates render targets for deferred lighting, shadows, and effects
- Manages texture arrays for particles and UI
- Creates various samplers (linear, point, clamp, wrap, etc.)
- Generates glTF environment maps and BRDF lookup tables
- **Dynamic Texture Binding**: Supports per-framebuffer texture arrays for runtime texture updates
  - Creates default white texture for uninitialized slots
  - Maintains separate texture arrays per framebuffer
  - Allows runtime texture slot updates without descriptor set recreation
- Key methods: `GetSampler()`, `CreateLightingTextures()`, `CreateShadowTextures()`, `GenerateGltfCubemap()`, `InitializePerFrameTextureArrays()`, `UpdateTextureSlot()`, `UpdateUiTextureSlot()`