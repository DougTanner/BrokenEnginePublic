# /Engine/Source/Graphics/

The `/Engine/Source/Graphics/` directory contains the Vulkan-based rendering system with manager-based architecture for efficient 3D rendering, particle systems, and UI.

## Core Files

### /Engine/Source/Graphics/Graphics.h / /Engine/Source/Graphics/Graphics.cpp
**Global Access**: `gpGraphics`  
**Purpose**: Central orchestrator containing all rendering managers and subsystems  
- Initializes and manages all graphics managers (Device, Instance, Swapchain, etc.)
- Handles frame synchronization with multiple frames in flight
- Coordinates the rendering pipeline from initialization to presentation
- Provides high-level rendering interface for game systems

### /Engine/Source/Graphics/Islands.h / /Engine/Source/Graphics/Islands.cpp
**Purpose**: Island-based terrain rendering system  
- Loads and renders terrain chunks from island data files
- Height-based terrain rendering with ambient occlusion
- Terrain mesh generation and level-of-detail support
- Integration with global terrain rendering pipeline

### /Engine/Source/Graphics/OneShotCommandBuffer.h / /Engine/Source/Graphics/OneShotCommandBuffer.cpp
**Purpose**: Utility for immediate GPU operations  
- Executes single-use command buffers for immediate operations
- Buffer uploads and image layout transitions
- Mipmap generation and texture operations
- Synchronous GPU command execution

### /Engine/Source/Graphics/Screenshot.h / /Engine/Source/Graphics/Screenshot.cpp
**Purpose**: Frame capture functionality  
- GPU-to-CPU image transfer for screenshot capture
- PNG file writing with async support
- Frame buffer reading and format conversion

## Manager Subdirectory (/Managers/)

### /Engine/Source/Graphics/Managers/BufferManager.h / /Engine/Source/Graphics/Managers/BufferManager.cpp
**Global Access**: `gpBufferManager`  
**Purpose**: Manages GPU buffers for vertex data, uniform buffers, and storage buffers  
- Creates terrain and water mesh buffers
- Manages uniform buffers for global constants and matrices
- Storage buffers for lights, particles, UI elements
- Model vertex buffer storage indexed by CRC

### /Engine/Source/Graphics/Managers/CommandBufferManager.h / /Engine/Source/Graphics/Managers/CommandBufferManager.cpp
**Global Access**: `gpCommandBufferManager`  
**Purpose**: Records and submits Vulkan command buffers  
- Pre-records command buffers for each framebuffer for efficiency
- Multi-threaded command buffer recording support
- Screenshot capture functionality
- Command buffer submission and synchronization

### /Engine/Source/Graphics/Managers/DeviceManager.h / /Engine/Source/Graphics/Managers/DeviceManager.cpp
**Global Access**: `gpDeviceManager`  
**Purpose**: Manages the logical Vulkan device and queues  
- Creates logical device with required extensions
- Graphics and presentation queue management
- Global descriptor pool creation and management
- Memory type lookup functionality

### /Engine/Source/Graphics/Managers/InstanceManager.h / /Engine/Source/Graphics/Managers/InstanceManager.cpp
**Global Access**: `gpInstanceManager`  
**Purpose**: Manages Vulkan instance and physical device selection  
- Creates Vulkan instance with required extensions
- Physical device (GPU) selection and capabilities query
- Win32 window surface creation
- Validation layers in debug builds

### /Engine/Source/Graphics/Managers/ParticleManager.h / /Engine/Source/Graphics/Managers/ParticleManager.cpp
**Global Access**: `gpParticleManager`  
**Purpose**: GPU-based particle system simulation  
- Compute shader particle spawning and updates
- Long particles (trails) and square particles (explosions)
- GPU particle physics simulation
- Instanced particle rendering

### /Engine/Source/Graphics/Managers/PipelineManager.h / /Engine/Source/Graphics/Managers/PipelineManager.cpp
**Global Access**: `gpPipelineManager`  
**Purpose**: Creates and manages all graphics and compute pipelines  
- 60+ specialized pipelines for different rendering passes
- Lighting blur pipeline chains (separate R/G/B channels)
- Shadow rendering pipelines
- glTF PBR rendering pipeline integration

### /Engine/Source/Graphics/Managers/ShaderManager.h / /Engine/Source/Graphics/Managers/ShaderManager.cpp
**Global Access**: `gpShaderManager`  
**Purpose**: Loads and caches compiled shader modules  
- SPIR-V bytecode loading from Data.bin
- VkShaderModule creation and caching
- Shader caching indexed by CRC

### /Engine/Source/Graphics/Managers/SwapchainManager.h / /Engine/Source/Graphics/Managers/SwapchainManager.cpp
**Global Access**: `gpSwapchainManager`  
**Purpose**: Manages swap chain presentation and frame synchronization  
- Swap chain creation and recreation on window resize
- Framebuffer management for each swap chain image
- Depth and multisampling texture creation
- Frame synchronization with semaphores and fences

### /Engine/Source/Graphics/Managers/TextManager.h / /Engine/Source/Graphics/Managers/TextManager.cpp
**Global Access**: `gpTextManager`  
**Purpose**: Efficient text rendering system  
- Character maps for EFIGS and Chinese fonts
- Text area updates for debug, stats, and profile info
- Text quad batching for efficient rendering
- Screen-space and world-space text support

### /Engine/Source/Graphics/Managers/TextureManager.h / /Engine/Source/Graphics/Managers/TextureManager.cpp
**Global Access**: `gpTextureManager`  
**Purpose**: Loads and manages textures and samplers  
- BC4/BC7 compressed texture loading from Data.bin
- Render target creation for deferred lighting and shadows
- Texture arrays for particles and UI elements
- Sampler creation (linear, point, clamp, wrap)
- glTF environment map and BRDF lookup table generation

## Objects Subdirectory (/Objects/)

### /Engine/Source/Graphics/Objects/Buffer.h / /Engine/Source/Graphics/Objects/Buffer.cpp
**Purpose**: GPU memory buffer abstraction with automatic allocation  
- Vertex, index, uniform, and storage buffer support
- Automatic memory allocation (device-local vs host-visible)
- Memory mapping for CPU access with proper alignment
- Pipeline barrier recording for synchronization
- RAII lifetime management

### /Engine/Source/Graphics/Objects/CommandBuffers.h / /Engine/Source/Graphics/Objects/CommandBuffers.cpp
**Purpose**: Command buffer allocation and frame synchronization management  
- Multi-frame command buffer pools and allocation
- Multiple command buffer types (Global, Main, Image)
- Semaphore-based GPU synchronization
- Fence-based CPU-GPU synchronization

### /Engine/Source/Graphics/Objects/GltfPipeline.h / /Engine/Source/Graphics/Objects/GltfPipeline.cpp
**Purpose**: Specialized pipeline for glTF model rendering with multiple materials  
- Extends Pipeline base for glTF-specific functionality
- Automatic pipeline creation per material
- Material-specific descriptor set management
- Indirect drawing support for instanced rendering

### /Engine/Source/Graphics/Objects/Pipeline.h / /Engine/Source/Graphics/Objects/Pipeline.cpp
**Purpose**: Complete GPU pipeline state encapsulation for graphics and compute  
- Graphics and compute pipeline creation
- Descriptor set layout and binding management
- Push constant support and indirect rendering
- Render state configuration (blending, depth, culling)
- Multi-threading support

### /Engine/Source/Graphics/Objects/Shader.h / /Engine/Source/Graphics/Objects/Shader.cpp
**Purpose**: SPIR-V shader module wrapper with validation  
- SPIR-V bytecode loading from data chunks
- Shader module creation and validation
- Debug name assignment for debugging
- Automatic cleanup on destruction

### /Engine/Source/Graphics/Objects/Texture.h / /Engine/Source/Graphics/Objects/Texture.cpp
**Purpose**: Image resource and render target management  
- 2D texture creation with mipmap and array support
- Render target and framebuffer creation
- Image layout transitions with pipeline barriers
- Texture data upload via staging buffers
- Render pass begin/end recording

## Architecture Overview

**Manager-Based Design**: All major subsystems use singleton managers accessible via global pointers (e.g., `gpBufferManager`, `gpTextureManager`)

**Vulkan Integration**: Direct Vulkan API usage with RAII wrappers for resource management

**Multi-Threading**: Optional multi-threaded command buffer recording with thread-safe interfaces

**Memory Management**: Efficient GPU memory allocation with sub-allocators and staging buffers

**Rendering Pipeline**: 
1. Frame start (acquire swapchain image)
2. Command recording (shadow, geometry, transparency, post-processing, UI)
3. Command submission to graphics queue
4. Presentation to swapchain