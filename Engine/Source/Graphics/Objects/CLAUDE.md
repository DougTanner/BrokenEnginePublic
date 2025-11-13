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
- `BufferBarrier` - Pipeline barrier types for synchronization (ComputeRead, ComputeWrite, ShaderUniformRead, ShaderIndirectRead)
- `BarrierInfo` - Batched barrier specification (source, destination, buffer handle)
- `BufferInfo` - Creation parameters including size, usage, and vertex stride

**Core Functionality:**
- Automatic memory allocation based on usage flags (device-local vs host-visible)
- Support for vertex, index, uniform, and storage buffers
- Memory mapping for CPU access with proper alignment
- Pipeline barrier recording for synchronization
- Copy operations between host and device memory
- RAII lifetime management with proper cleanup

**VMA Memory Management:**
- Uses `VMA_MEMORY_USAGE_AUTO` for automatic memory type selection
- Host-visible buffers use `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT` for sequential write optimization
- Persistent mapping via `VMA_ALLOCATION_CREATE_MAPPED_BIT` (accesses pre-mapped pointer from VmaAllocationInfo)
- `VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT` allows VMA to use device-local+staging if more optimal
- VMA intelligently chooses between host-visible memory (integrated GPUs) or device-local+staging (discrete GPUs)

**Key Methods:**
- `Create()` - Creates buffer with specified usage and memory type
- `GetBuffer()` - Returns appropriate VkBuffer handle
- `RecordBindVertexBuffer()` - Records vertex/index buffer binding commands
- `RecordCopy()` - Records buffer copy commands with barriers
- `RecordBarriers()` - Static method to batch buffer barriers into single vkCmdPipelineBarrier call (reduces overhead by 64-71% in particle system)

**Barrier Usage Patterns:**
```cpp
// Multiple barriers batched together (optimal for related synchronization)
Buffer::RecordBarriers(vkCommandBuffer, std::to_array<BarrierInfo>(
{
    {BufferBarrier::kComputeWrite, BufferBarrier::kComputeRead, buffer1},
    {BufferBarrier::kComputeWrite, BufferBarrier::kShaderIndirectRead, buffer2},
    {BufferBarrier::kComputeWrite, BufferBarrier::kShaderIndirectRead, buffer3},
}));

// Single barrier (same interface, same performance as old RecordBarrier)
Buffer::RecordBarriers(vkCommandBuffer, std::to_array<BarrierInfo>(
{
    {BufferBarrier::kComputeWrite, BufferBarrier::kShaderUniformRead, buffer},
}));
```

### CommandBuffers.h & CommandBuffers.cpp
**Command buffer allocation and frame synchronization management**

**Key Classes:**
- `CommandBuffers` - Multi-frame command buffer manager

**Core Functionality:**
- Pre-allocated command pools and buffers per frame
- Two command buffer types (Global, Image)
- Semaphore-based GPU synchronization
- Fence-based CPU-GPU synchronization
- Frame cycling with `Next()` method

**Key Members:**
- `mpGlobalCommandBuffers[]` - Pre-processing command buffers (shadows, particles, terrain generation)
- `mpImageCommandBuffers[]` - Main rendering command buffers (lighting, blur, scene rendering)
- Semaphores for inter-command buffer synchronization
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
- **MRT Support**: Automatic blend state configuration for Multiple Render Targets
  - Detects MRT lighting render pass (`gpTextureManager->mLightingVkRenderPass`)
  - Configures 3 blend attachment states (one per color channel) for MRT
  - All attachments use same blend mode (e.g., MAX blend for lighting)
  - Single-attachment configuration for all other render passes

**Critical Implementation Details:**
- **Descriptor Set Management**:
  - Per-framebuffer descriptor sets for dynamic resources
  - Automatic recreation on framebuffer count changes
  - Individual descriptor set freeing in Pipeline::Destroy()
  - Proper descriptor pool allocation and cleanup
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

**VMA Memory Management:**
- Uses `VMA_MEMORY_USAGE_AUTO` for automatic memory type selection
- Host-visible textures use `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT` for staging operations
- Render pass textures use size-based dedicated allocation strategy (>32MB threshold)
- Small render targets (<32MB) benefit from suballocation to reduce memory fragmentation
- `VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT` used only for large render targets or when VMA recommends it
- VMA automatically uses dedicated allocations for large resources regardless of flags

**Key Methods:**
- `Create()` - Creates texture with specified parameters (dataFunction optional for empty textures)
- `UpdateData()` - Updates existing texture data in-place with staging buffer and proper layout transitions
- `TransitionImageLayout()` - Records layout transition barriers
- `RecordBeginRenderPass()` / `RecordEndRenderPass()` - Render pass management
- Static helpers for render pass recording

**Performance Optimization:**
- Both `Create()` and `UpdateData()` batch all mip level and array layer copies into a single command buffer
- Eliminates GPU synchronization overhead between individual mip level uploads
- Reduces texture upload time by ~8-60x compared to per-mip command buffer submission
- Single OneShotCommandBuffer records all vkCmdCopyBufferToImage calls before executing

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
```
