# `/Engine/Source/Graphics/Objects/`

Low-level Vulkan resource wrappers providing RAII semantics and simplified interfaces for GPU resources. Each object encapsulates Vulkan handles and provides automatic lifecycle management.

## Design Philosophy

**RAII Pattern**: All Vulkan resources wrapped with automatic cleanup in destructors  
**Zero-Copy**: Objects use move semantics, copying disabled  
**Type Safety**: Strong typing with enum flags for configuration  
**Error Handling**: Validation via `CHECK_VK` macro for all Vulkan calls  
**Debug Support**: Objects named for debugging and profiling tools  

## Core Files

### Buffer.h & Buffer.cpp
**GPU memory buffer abstraction with automatic allocation**

**Key Classes:**
- `Buffer` - Main buffer wrapper class
- `BufferFlags` - Usage and memory type flags (IndexVertex, Uniform, Storage, DeviceLocal, HostVisible)
- `BufferBarrier` - Pipeline barrier types for synchronization
- `BufferInfo` - Creation parameters including size, usage, and vertex stride

**Core Functionality:**
- Automatic memory allocation based on usage flags (device-local vs host-visible)
- Support for vertex, index, uniform, and storage buffers
- Memory mapping for CPU access with proper alignment
- Pipeline barrier recording for synchronization
- Copy operations between host and device memory
- RAII lifetime management with proper cleanup

**Key Methods:**
- `Create()` - Creates buffer with specified usage and memory type
- `GetBuffer()` - Returns appropriate VkBuffer handle
- `RecordBindVertexBuffer()` - Records vertex/index buffer binding commands
- `RecordCopy()` - Records buffer copy commands with barriers

### CommandBuffers.h & CommandBuffers.cpp
**Command buffer allocation and frame synchronization management**

**Key Classes:**
- `CommandBuffers` - Multi-frame command buffer manager

**Core Functionality:**
- Pre-allocated command pools and buffers per frame
- Multiple command buffer types (Global, Main, Image)
- Semaphore-based GPU synchronization
- Fence-based CPU-GPU synchronization
- Frame cycling with `Next()` method

**Key Members:**
- `mpGlobalCommandBuffers[]` - General purpose command buffers
- `mpMainCommandBuffers[]` - Primary rendering command buffers  
- `mpImageCommandBuffers[]` - Image processing command buffers
- Semaphores for inter-queue synchronization
- Fences for CPU-GPU synchronization

### GltfPipeline.h & GltfPipeline.cpp
**Specialized pipeline for glTF model rendering with multiple materials**

**Key Classes:**
- `GltfPipeline` - Multi-material pipeline wrapper

**Core Functionality:**
- Extends Pipeline base functionality for glTF models
- Automatic creation of pipelines per material
- Material-specific descriptor set management
- Indirect drawing support for instanced rendering
- Integration with glTF file format and material data

**Key Methods:**
- `Create()` - Creates pipelines for all materials in glTF model
- `RecordDrawIndirect()` - Records indirect draw commands for all materials
- `WriteIndirectBuffer()` - Updates indirect draw parameters

**Key Members:**
- `mpPipelines[]` - Array of Pipeline objects per material
- `mpiIndexCounts[]` - Index counts per material
- `mpiFirstIndices[]` - Starting indices per material

### Pipeline.h & Pipeline.cpp
**Complete GPU pipeline state encapsulation for graphics and compute**

**Key Classes:**
- `Pipeline` - Main pipeline wrapper
- `PipelineInfo` - Creation parameters including shaders, buffers, and render state
- `DescriptorInfo` - Descriptor set binding configuration
  - `textureCrc` - CRC of texture to bind (for file-loaded textures from mTextureMap)
  - `pTexture` - Single texture pointer (for runtime-created textures like render targets)
  - `ppTextures` - Array of texture pointers (for texture arrays)
- `PipelineFlags` - Pipeline features (AlphaBlend, DepthTest, Compute, etc.)
- `DescriptorFlags` - Descriptor types (Textures, Buffers, Samplers, etc.)

**Core Functionality:**
- Graphics and compute pipeline creation
- Descriptor set layout and binding management
- Push constant support
- Indirect rendering buffer management
- Render state configuration (blending, depth, culling)
- Dynamic state and multi-threading support

**Critical Implementation Details:**
- **Descriptor Set Management**: 
  - Per-framebuffer descriptor sets for dynamic resources
  - Automatic recreation on framebuffer count changes
  - Proper descriptor pool allocation
- **Shader Integration**:
  - Requires valid shader modules at creation time
  - No fallback for missing shaders
  - Vertex input state derived from buffer info
- **Indirect Rendering**:
  - Pre-allocated indirect command buffers
  - CPU-writable for dynamic draw counts
  - Supports instanced rendering

**Key Methods:**
- `Create()` - Creates pipeline with specified configuration
- `RecordDraw()` - Records direct draw commands
- `RecordDrawIndirect()` - Records indirect draw commands
- `RecordCompute()` - Records compute dispatch commands
- `WriteIndirectBuffer()` - Updates indirect draw parameters
- `RecreateDescriptorSets()` - Recreates descriptor sets when framebuffer count changes

**Key Members:**
- `mVkPipeline` - Vulkan pipeline handle
- `mVkPipelineLayout` - Pipeline layout for resources
- `mVkDescriptorSets` - Descriptor sets for resource binding (per framebuffer)
- `mIndirectVkBuffer` - Buffer for indirect rendering commands

### Shader.h & Shader.cpp
**SPIR-V shader module wrapper with validation**

**Key Classes:**
- `Shader` - Shader module wrapper
- `ShaderInfo` - Creation parameters referencing chunk data

**Core Functionality:**
- SPIR-V bytecode loading from data chunks
- Shader module creation and validation
- Debug name assignment for debugging
- Automatic cleanup on destruction

**Key Methods:**
- `Create()` - Creates shader module from SPIR-V data
- `Destroy()` - Cleans up shader module

**Key Members:**
- `mVkShaderModule` - Vulkan shader module handle
- `mInfo` - Shader information including chunk header

### Texture.h & Texture.cpp
**Image resource and render target management**

**Key Classes:**
- `Texture` - Main texture wrapper
- `TextureInfo` - Creation parameters including format, usage, and render pass settings
- `TextureFlags` - Feature flags (Multisampling, RenderPass, Depth, HostVisible)
- `TextureLayout` - Image layout states for transitions

**Core Functionality:**
- 2D texture creation with mipmap and array support
- Empty texture creation for lazy loading (no initial data)
- In-place texture data updates via staging buffers
- Render target and framebuffer creation with depth buffers
- Image layout transitions with pipeline barriers
- Render pass begin/end recording
- Automatic memory allocation and layout management

**Key Methods:**
- `Create()` - Creates texture with specified parameters (dataFunction optional for empty textures)
- `UpdateData()` - Updates existing texture data in-place with staging buffer and proper layout transitions
- `TransitionImageLayout()` - Records layout transition barriers
- `RecordBeginRenderPass()` / `RecordEndRenderPass()` - Render pass management
- Static helpers for render pass recording

**Lazy Loading Pattern:**
- Create empty texture with `Create(info, nullptr)` - allocates GPU memory with correct dimensions
- Later update with `UpdateData(dataFunction)` - transitions kShaderReadOnly → kTransferDestination → uploads data → kShaderReadOnly
- VkImageView handle remains unchanged, so no descriptor set updates needed

**Key Members:**
- `mVkImage` - Vulkan image handle
- `mVkImageView` - Image view for shader access
- `mVkRenderPass` - Render pass for render targets
- `mVkFramebuffer` - Framebuffer for render targets
- `mpDepthTexture` - Associated depth buffer for render targets

## Usage Patterns

### Direct Resource Creation
```cpp
Buffer buffer({
    .pcName = "VertexBuffer",
    .flags = {BufferFlags::kIndexVertex, BufferFlags::kDeviceLocal},
    .dataVkDeviceSize = sizeof(vertices)
}, [&](void* pData) { memcpy(pData, vertices.data(), sizeof(vertices)); });
```

### Pipeline Setup
```cpp
Pipeline pipeline({
    .pcName = "MainPipeline",
    .flags = {PipelineFlags::kDepthTest, PipelineFlags::kDepthWrite},
    .ppShaders = {&vertexShader, &fragmentShader},
    .pVertexBuffer = &vertexBuffer
});
```

### Render Target Creation
```cpp
Texture renderTarget({
    .textureFlags = {TextureFlags::kRenderPass, TextureFlags::kDepth},
    .pcName = "RenderTarget",
    .format = VK_FORMAT_R8G8B8A8_UNORM,
    .extent = {1920, 1080, 1}
});
```

## Common Usage Patterns

### Resource Creation Flow
```cpp
// 1. Create buffer with data
Buffer vertexBuffer({
    .pcName = "VertexBuffer",
    .flags = {BufferFlags::kIndexVertex, BufferFlags::kDeviceLocal},
    .dataVkDeviceSize = sizeof(vertices)
}, [&](void* pData) { memcpy(pData, vertices.data(), sizeof(vertices)); });

// 2. Create texture from chunk
Texture texture({
    .pcName = "DiffuseTexture",
    .format = VK_FORMAT_R8G8B8A8_UNORM,
    .usage = VK_IMAGE_USAGE_SAMPLED_BIT
}, [&](void* pData, int64_t offset, int64_t size) {
    // Copy from chunk data
});

// 3. Create pipeline with resources
Pipeline pipeline({
    .pcName = "MainPipeline",
    .ppShaders = {&vertShader, &fragShader},
    .pVertexBuffer = &vertexBuffer,
    .descriptorInfos = {{.textureCrc = diffuseTextureCrc}}
});
```

### Render Pass Pattern
```cpp
// Begin render pass
Texture::RecordBeginRenderPass(cmd, renderTarget);

// Record draw commands
pipeline.RecordDraw(cmd, vertexCount);

// End render pass
Texture::RecordEndRenderPass(cmd);
```

### Dynamic Resource Updates
```cpp
// Update uniform buffer (after fence wait)
uniformBuffer.Map([&](void* pData) {
    memcpy(pData, &uniformData, sizeof(uniformData));
});

// Update descriptor sets for new textures
pipeline.RecreateDescriptorSets(newFramebufferCount);
```
