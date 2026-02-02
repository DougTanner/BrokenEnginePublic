# Graphics Objects - RAII Vulkan Resource Wrappers

Low-level Vulkan resource wrappers providing RAII semantics for GPU resources. All classes encapsulate Vulkan handles with automatic lifecycle management via constructors/destructors.

## Design Philosophy

- **RAII Pattern**: All Vulkan resources cleaned up in destructors
- **Move Semantics**: Objects support move operations, copying disabled where appropriate
- **VMA Integration**: Vulkan Memory Allocator handles all GPU memory allocation
- **Batched Operations**: Pipeline barriers and resource copies batched to minimize driver overhead

## Classes

### Buffer
GPU memory buffer wrapper supporting vertex/index/uniform/storage buffer types. Dual-buffer architecture (host-visible staging + device-local GPU) enables efficient CPU-to-GPU data transfer. BufferInfo stores element size for dynamic storage buffers to enable type-safe access via BufferManager's templated accessor.

**Memory Allocation**: VMA handles memory type selection with intelligent allocation strategies: readback buffers get random access mapping, indirect buffers require true HOST_VISIBLE + HOST_COHERENT memory for coherent CPU writes, and upload buffers may use staging with ALLOW_TRANSFER_INSTEAD optimization.

**Static Utilities**: `CreateBuffer()` creates VkBuffer with VMA allocation and optional pre-mapped pointer. `RecordBarriers()` batches multiple buffer barriers into a single vkCmdPipelineBarrier call with combined stage masks.

**Instance Methods**: `RecordCopy()` records host-visible to device-local transfer with pre/post barriers. `RecordBindVertexBuffer()` binds both index and vertex portions of a combined buffer.

### CommandBuffers
Per-framebuffer command buffer allocation and synchronization. Creates one command pool per swap chain framebuffer with Global (preprocessing) and Main (rendering) primary command buffers. Manages semaphores for GPU-GPU synchronization between stages and fences for CPU-GPU synchronization.

### GltfPipeline
Multi-material pipeline wrapper for glTF model rendering. Creates separate Pipeline per material in glTF model with per-material descriptor sets and indirect draw buffers. `UpdateStorageBufferDescriptors()` propagates storage buffer updates across all material pipelines after buffer resize.

### Pipeline
Complete Vulkan pipeline state for graphics and compute operations. Combines shader modules, vertex input, render state, and resource bindings.

**Descriptor Management**: Per-framebuffer descriptor sets prevent GPU conflicts. DescriptorInfo supports explicit binding assignment via `iExplicitBinding` for sparse layouts. When `kGltf` descriptor flag is set, automatically adds 5 additional descriptors: lighting textures, shadow blur, smoke texture, mesh data storage buffer (binding 15), and joint matrices storage buffer (binding 16) for skeletal animation.

**Indirect Rendering**: Host-visible indirect buffers use standard `VkDrawIndexedIndirectCommand` struct. Buffers are pre-mapped via VMA and zero-initialized to prevent undefined behavior. `WriteIndirectBuffer()` updates draw parameters (indexCount, instanceCount, firstIndex, vertexOffset, firstInstance).

**Update-After-Bind**: Pipelines with `kUpdateAfterBind` flag use a separate descriptor pool and can update storage buffer descriptors at runtime via `UpdateStorageBufferDescriptor()` without command buffer re-recording.

**Draw Recording**: Multiple draw variants support direct, indirect, and alternate pipeline/descriptor set combinations for flexible rendering patterns.

### Shader
SPIR-V shader module wrapper. Creates VkShaderModule from SPIR-V bytecode in file chunks with debug naming for profiling tools.

### Texture
Image resource and render target management. Supports mipmaps and texture arrays with batched upload operations. Lazy loading pattern: create empty, update later via `UpdateData()`, VkImageView unchanged so descriptor sets remain valid. Render targets include automatic depth buffer and framebuffer setup. VMA uses dedicated allocations for large render targets (>32MB).

## Key Patterns

**Update Safety**: Buffer/texture updates must occur after fence wait to avoid modifying GPU-in-use resources.

**Descriptor Set Lifecycle**: Swap chain resize recreates framebuffers, requiring descriptor set recreation. Descriptor sets are explicitly freed back to the pool during pipeline destruction.

**Manager Ownership**: All objects created and owned by corresponding managers (BufferManager, TextureManager, ShaderManager, PipelineManager, CommandBufferManager).
