# `/Engine/Source/Graphics/`

Vulkan-based rendering system built on a multi-manager architecture with strict initialization ordering and resource lifetime management.

## Architecture Overview

**Rendering Pipeline**: Multi-pass deferred rendering with separate lighting, shadow, and post-processing passes  
**Frame Management**: Multiple frames in flight with per-framebuffer command buffers and synchronization  
**Resource Management**: RAII-based Vulkan object wrappers with automatic cleanup  
**Threading Model**: Optional multi-threaded command buffer recording support  

## Core Files

### Graphics.h & Graphics.cpp
**Global Access**: `gpGraphics`  
**Purpose**: Central orchestrator and entry point for the entire rendering system  

**Key Responsibilities**:
- Manager initialization in strict dependency order (critical for Vulkan resource creation)
- Frame synchronization with fences and semaphores for multi-frame rendering
- Render loop coordination: Global pass → Main pass → Present
- Lazy texture loading coordination after fence wait (safe GPU update point)
- Dynamic recreation of resources on window resize or device changes
- Performance profiling integration (CPU and GPU timers)

**Frame Lifecycle**:
1. Wait for previous frame fence (blocks if GPU still using resources)
2. Process pending texture loads (safe after fence wait)
3. Record and submit global command buffer (shadows, particles, etc.)
4. Record and submit main command buffer (scene, UI, text)
5. Present to swap chain and acquire next image
6. Handle any necessary resource recreation

**Critical Patterns**:
- Fence wait before any GPU resource updates to avoid in-use conflicts
- Texture loading deferred until after fence guarantees safety
- Command buffer recording split between global and main for optimal GPU utilization

### Islands.h & Islands.cpp
**Purpose**: Specialized terrain rendering system for island-based worlds  

**Key Features**:
- Streaming terrain data from pre-processed island chunks
- Height-based vertex generation with normal calculation
- Ambient occlusion texture integration
- Level-of-detail support for distant terrain
- Integration with terrain-specific pipelines and shaders

**Resource Management**:
- Lazy loading of island textures (elevation, color, normals, AO)
- Per-island mesh generation and buffer allocation
- Texture array indexing for efficient binding

### OneShotCommandBuffer.h & OneShotCommandBuffer.cpp
**Purpose**: Immediate-mode GPU command execution utility  

**Use Cases**:
- Texture data uploads and mipmap generation
- Image layout transitions during resource creation
- Buffer-to-buffer copies with proper barriers
- One-time initialization operations

**Key Features**:
- Automatic command pool and buffer management
- Synchronous execution with fence waiting
- Proper pipeline barriers for resource transitions
- Helper methods for common operations

### Screenshot.h & Screenshot.cpp
**Purpose**: Asynchronous frame capture system  

**Implementation Details**:
- Captures from swap chain image after rendering complete
- GPU→CPU transfer via staging buffer
- Asynchronous PNG encoding on worker thread
- Proper synchronization to avoid capturing mid-render

## Manager Dependencies & Initialization Order

### Vulkan Resource Creation Hierarchy

**Critical Initialization Order** (violating this order causes Vulkan validation errors or crashes):

1. **InstanceManager** - Creates Vulkan instance and selects physical device
   - No dependencies
   - Creates: VkInstance, selects VkPhysicalDevice
   - Enables validation layers in debug builds

2. **DeviceManager** - Creates logical device and queues
   - Depends on: InstanceManager (needs physical device)
   - Creates: VkDevice, VkQueue handles, VkDescriptorPool
   - Critical: All subsequent Vulkan objects require VkDevice

3. **SwapchainManager** - Creates presentation surface and swap chain
   - Depends on: DeviceManager (VkDevice), InstanceManager (VkSurfaceKHR)
   - Creates: VkSwapchainKHR, framebuffers, depth/MSAA textures
   - Synchronization: Semaphores and fences for frame management

4. **ShaderManager** - Loads and creates shader modules
   - Depends on: DeviceManager (VkDevice), FileManager (shader chunks)
   - Creates: VkShaderModule for each loaded shader

5. **TextureManager** - Creates textures, samplers, and render targets
   - Depends on: DeviceManager, SwapchainManager (framebuffer count), FileManager
   - Creates: Static textures, render targets, texture arrays
   - Implements lazy loading for file-based textures

6. **BufferManager** - Allocates all GPU buffers
   - Depends on: DeviceManager, FileManager (model data)
   - Creates: Vertex buffers, uniform buffers, storage buffers
   - Memory types: Device-local for static data, host-visible for dynamic

7. **PipelineManager** - Creates all graphics and compute pipelines
   - Depends on: DeviceManager, ShaderManager, SwapchainManager, TextureManager, BufferManager
   - Creates: ~60 specialized pipelines

8. **CommandBufferManager** - Allocates command pools and buffers
   - Depends on: DeviceManager, SwapchainManager (framebuffer count)
   - Creates: Command pools, pre-allocated command buffers per framebuffer
   - Three types: Global, Main, Image

9. **ParticleManager** - GPU-based particle system
   - Depends on: DeviceManager, BufferManager, PipelineManager
   - Creates: Particle buffers, compute pipelines

10. **TextManager** - Text rendering system
    - Depends on: DeviceManager, TextureManager, FileManager (fonts)
    - Creates: Character maps, text area management

### Runtime Dependencies & Resource Access

**Manager Access Patterns**:
- All managers accessed via global pointers (e.g., `gpGraphics`, `gpTextureManager`)
- Managers never deleted individually - only during Graphics destruction
- Resource recreation flows through Graphics::Create() → Destroy() → recreate

**Critical Resource Lifetime Rules**:
- GPU resources can only be updated after fence wait
- Descriptor sets must be recreated when framebuffer count changes
- Pipeline recreation requires shader availability
- Texture updates require proper image layout transitions

**External System Integration**:
- **FileManager**: Provides lazy chunk loading for textures and models
- **Frame System**: Supplies per-frame game state for rendering
- **UI System**: Integrates with TextManager for widget rendering
- **ProfileManager**: GPU/CPU timing integration

## Common Pitfalls & Warnings

1. **Shader Loading**: ShaderManager loads all shaders immediately - no fallback for missing shaders
2. **Pipeline Creation**: Crashes if referenced shader CRC not found in map
3. **Fence Timeout**: 1-second timeout (kFenceTimeoutNs) - exceeded indicates GPU hang
4. **Texture Updates**: Must occur after fence wait to avoid updating in-use resources
5. **Descriptor Sets**: Must handle dynamic recreation when framebuffer count changes
6. **Memory Types**: Incorrect memory type selection causes validation errors

## See Also
- Managers: [Managers/CLAUDE.md](Managers/CLAUDE.md) - Detailed manager documentation with Vulkan-specific patterns
- Objects: [Objects/CLAUDE.md](Objects/CLAUDE.md) - Low-level Vulkan resource wrappers and RAII patterns