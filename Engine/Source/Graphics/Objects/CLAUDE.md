# Graphics Objects - RAII Vulkan Resource Wrappers

Low-level Vulkan resource wrappers providing RAII semantics for GPU resources. All classes encapsulate Vulkan handles with automatic lifecycle management via constructors/destructors.

## Design Philosophy

- **RAII Pattern**: All Vulkan resources cleaned up in destructors
- **Move Semantics**: Objects support move operations, copying disabled where appropriate
- **VMA Integration**: Vulkan Memory Allocator handles all GPU memory allocation
- **Batched Operations**: Pipeline barriers and resource copies batched to minimize driver overhead

## Classes

### Buffer
GPU memory buffer wrapper supporting vertex/index/uniform/storage buffer types. Dual-buffer architecture (host-visible staging + device-local GPU) when needed. VMA handles memory type selection with persistent mapping for frequently-updated buffers. Static `RecordBarriers()` batches multiple barriers into single vkCmdPipelineBarrier call.

### CommandBuffers
Per-framebuffer command buffer allocation and synchronization. Creates one command pool per swap chain framebuffer with Global (preprocessing) and Main (rendering) primary command buffers. Manages semaphores for GPU-GPU synchronization between stages and fences for CPU-GPU synchronization.

### GltfPipeline
Multi-material pipeline wrapper for glTF model rendering. Creates separate Pipeline per material in glTF model with per-material descriptor sets and indirect draw buffers. `UpdateStorageBufferDescriptors()` propagates storage buffer updates across all material pipelines after buffer resize.

### Pipeline
Complete Vulkan pipeline state for graphics and compute operations. Combines shader modules, vertex input, render state, and resource bindings. Per-framebuffer descriptor sets prevent GPU conflicts. Supports push constants, indirect rendering, and update-after-bind descriptors. `UpdateStorageBufferDescriptor()` enables runtime buffer resizing without pipeline recreation. DescriptorInfo supports explicit binding assignment via `iExplicitBinding` for sparse layouts (e.g., joint matrices at binding 15). When `kGltf` descriptor flag is set, automatically adds 4 additional descriptors: lighting textures, shadow blur, smoke texture, and joint matrices storage buffer for skeletal animation.

### Shader
SPIR-V shader module wrapper. Creates VkShaderModule from SPIR-V bytecode in file chunks with debug naming for profiling tools.

### Texture
Image resource and render target management. Supports mipmaps and texture arrays with batched upload operations. Lazy loading pattern: create empty, update later via `UpdateData()`, VkImageView unchanged so descriptor sets remain valid. Render targets include automatic depth buffer and framebuffer setup. VMA uses dedicated allocations for large render targets (>32MB).

## Key Patterns

**Update Safety**: Buffer/texture updates must occur after fence wait to avoid modifying GPU-in-use resources.

**Descriptor Set Lifecycle**: Swap chain resize recreates framebuffers, requiring descriptor set recreation. Pipelines with `kUpdateAfterBind` flag can update storage buffer descriptors without command buffer re-recording.

**Manager Ownership**: All objects created and owned by corresponding managers (BufferManager, TextureManager, ShaderManager, PipelineManager, CommandBufferManager).
