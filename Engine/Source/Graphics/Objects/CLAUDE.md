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
Multi-material pipeline wrapper for glTF model rendering. Creates separate Pipeline per material in glTF model with per-material descriptor sets and indirect draw buffers. `UpdateStorageBufferDescriptors()` propagates storage buffer updates across all material pipelines after buffer resize. `UpdateGltfTextureDescriptors()` re-writes per-material combined image sampler descriptors using actual texture image views (or white texture fallback for unloaded textures), called after lazy texture loading completes.

### Pipeline
Complete Vulkan pipeline state for graphics and compute operations. Combines shader modules, vertex input, render state, and resource bindings.

**Descriptor Management**: Per-framebuffer descriptor sets prevent GPU conflicts. DescriptorInfo supports explicit binding assignment via `iExplicitBinding` for sparse layouts. When `kGltf` descriptor flag is set, automatically adds 5 additional descriptors: lighting textures, shadow blur, smoke texture, mesh data storage buffer (binding 15), and joint matrices storage buffer (binding 16) for skeletal animation. Descriptor writes are filtered against the actual shader layout bindings, handling sparse binding layouts gracefully.

**Indirect Rendering**: Host-visible indirect buffers use standard `VkDrawIndexedIndirectCommand` struct. Buffers are pre-mapped via VMA and zero-initialized to prevent undefined behavior. `WriteIndirectBuffer()` updates draw parameters (indexCount, instanceCount, firstIndex, vertexOffset, firstInstance) and flushes host writes for memory synchronization.

**Update-After-Bind**: Pipelines with `kUpdateAfterBind` flag use a separate descriptor pool and can update storage buffer, combined image sampler, and sampled image descriptors at runtime without command buffer re-recording. `ConfigureUpdateAfterBind()` sets `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT` on appropriate binding types.

**Runtime Descriptor Updates**: `UpdateStorageBufferDescriptor()` updates a single storage buffer binding for one framebuffer. `UpdateCombinedImageSamplerDescriptor()` updates a combined image sampler binding across all framebuffer descriptor sets. `UpdateTextureArrayDescriptor()` updates a sampled image array binding across all framebuffer descriptor sets, used to propagate lazy-loaded texture image views to dynamic pipelines.

**Texture Binding Registration**: During descriptor set creation, pipelines register texture bindings with TextureManager via `RegisterTextureBinding()` and `RegisterTextureArrayPipeline()`, enabling deferred descriptor updates when lazy-loaded textures arrive. Registration is guarded by a shader layout check (`bindingExistsInShaderLayout`) that verifies the binding exists in the vertex or fragment shader before registering, preventing pipelines with sparse layouts (e.g., shadow pipelines) from registering for deferred updates on bindings they don't use.

**Draw Recording**: Multiple draw variants support direct, indirect, and alternate pipeline/descriptor set combinations for flexible rendering patterns.

### Shader
SPIR-V shader module wrapper. Creates VkShaderModule from SPIR-V bytecode in file chunks with debug naming for profiling tools.

### Texture
Image resource and render target management. Supports mipmaps and texture arrays with batched upload operations.

**Lazy Loading**: `InitDeferred()` stores metadata and borrows a placeholder VkImageView (white texture) without any GPU allocation. `Create()` allocates the real GPU image when data arrives. `AdoptTransferredImage()` takes ownership of a VkImage already uploaded by the transfer queue, creates a VkImageView, and performs an acquire barrier on the graphics queue when the transfer and graphics queues are on separate families. When QFOT is optional (VK_KHR_maintenance9), the barrier is a simple layout transition without queue family ownership transfer; otherwise, it performs a full QFOT acquire barrier matching the release recorded by FileManager. `UpdateData()` updates texture data in-place via staging buffer with layout transitions.

**Render Targets**: Textures with `kRenderPass` flag automatically create a VkRenderPass and VkFramebuffer. Optional depth buffer created via `kDepth` flag. VMA uses dedicated allocations for large render targets (>32MB). Subpass dependency ensures shader read-after-write correctness.

**Component Swizzle**: BC4 textures (single-channel) automatically get RRRR swizzle for correct sampling unless they are render targets.

## Key Patterns

**Update Safety**: Buffer/texture updates must occur after fence wait to avoid modifying GPU-in-use resources.

**Descriptor Set Lifecycle**: Swap chain resize recreates framebuffers, requiring descriptor set recreation. Descriptor sets are explicitly freed back to the pool during pipeline destruction, using the correct pool (main or update-after-bind) based on pipeline flags.

**Manager Ownership**: All objects created and owned by corresponding managers (BufferManager, TextureManager, ShaderManager, PipelineManager, CommandBufferManager).
