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

**Static Utilities**: `CreateBuffer()` creates VkBuffer with VMA allocation and optional pre-mapped pointer. `RecordBarriers()` batches multiple buffer barriers into a single vkCmdPipelineBarrier call with combined stage masks, using `common::gpThreadLocal->mWorkbuffer` via `Push()`/`PushBack<VkBufferMemoryBarrier>()`/`Span<>()` for temporary barrier storage to avoid per-call heap allocations, calling `Pop()` after the pipeline barrier is recorded.

**Instance Methods**: `RecordCopy()` records host-visible to device-local transfer with pre/post barriers. `RecordBindVertexBuffer()` binds both index and vertex portions of a combined buffer.

### CommandBuffers
Per-framebuffer command buffer allocation and synchronization. Creates one command pool per swap chain framebuffer with Global (preprocessing) and Main (rendering) primary command buffers. Manages semaphores for GPU-GPU synchronization between stages and fences for CPU-GPU synchronization.

### ModelPipeline
Multi-material pipeline wrapper for model rendering. Creates separate Pipeline per material in a model with per-material descriptor sets and indirect draw buffers. Stores the scene CRC at creation for demand-driven texture loading.

**Transparent Material Support**: During creation, reads each material's `fAlphaMask` from the scene's `MaterialShaderData`. Materials with `fAlphaMask >= 2.0` are marked transparent (`mpbTransparentMaterials[]`), and the `mbHasTransparentMaterials` flag is set. Transparent material pipelines are created with alpha blending enabled, depth writes disabled, and back-face culling disabled. Shadow pipelines skip transparency overrides (always use opaque pipeline state). `RecordDrawIndirect()` accepts a `ModelDrawPass` enum (`kAll`, `kOpaque`, `kTransparent`) to selectively draw opaque-only, transparent-only, or all materials, enabling two-pass rendering for correct transparency.

**Multi-Set Descriptor Layout**: When `kMultiSet` flag is set, model pipelines split descriptors into Set 0 (shared across all materials) and Set 1 (per-material bindings). The first inner Pipeline owns the Set 0 layout and descriptor sets; remaining inner Pipelines reference the first Pipeline's Set 0 layout via `mVkExternalDescriptorSetLayout` (not owned). During `RecordDrawIndirect()`, Set 0 is bound once before the material loop using the first Pipeline's descriptor sets, then each material binds only its own Set 1 via `RecordDrawIndirectSet1()`. `UpdateStorageBufferDescriptors()` for multi-set pipelines only updates the first Pipeline's descriptor sets for Set 0 bindings (e.g., mesh data binding 15, joint matrices binding 16), since those sets are shared.

**Demand-Driven Loading**: Two levels of demand-driven loading work together: `ModelPipeline::WriteIndirectBuffer()` triggers `RequestChunkLoad()` for the scene's textures (via texture CRCs in the chunk data payload) on the first call with a non-zero instance count, while each inner `Pipeline::WriteIndirectBuffer()` similarly triggers `RequestChunkLoad()` for its own `mTextureCrcs` (combined image sampler textures collected during descriptor set creation). Both requests are idempotent, so pipeline recreation (which resets the flags) is safe. Model textures are accessed via the global bindless texture array rather than per-material combined image sampler descriptors -- texture indices are stored in `PbrMaterialLayout` and populated by `CrcToIndex()` during material buffer creation in `Pipeline::WriteDescriptorSets()`.

### Pipeline
Complete Vulkan pipeline state for graphics and compute operations. Combines shader modules, vertex input, render state, and resource bindings.

**Descriptor Management**: Per-framebuffer descriptor sets prevent GPU conflicts. DescriptorInfo supports explicit binding assignment via `iExplicitBinding` for sparse layouts. When `kModel` descriptor flag is set, automatically adds 5 additional descriptors: lighting textures, shadow blur, smoke texture, mesh data storage buffer (binding 15), and joint matrices storage buffer (binding 16) for skeletal animation. Descriptor writes are filtered against the actual shader layout bindings, handling sparse binding layouts gracefully. For model material buffers, `WriteDescriptorSets()` populates `PbrMaterialLayout` texture index fields (e.g., `fColorTextureIndex`) by calling `CrcToIndex()` on each material's texture CRCs, mapping them to the global bindless texture array.

**Multi-Set Descriptor Support**: When `kMultiSet` flag is set, Pipeline splits bindings into Set 0 and Set 1 based on per-binding set indices from shader reflection (`ShaderInfo::pDescriptorSetIndices`). Creates separate `VkDescriptorSetLayout` and `VkDescriptorSet` for each set, with a two-set `VkPipelineLayout`. Inner Pipelines in a ModelPipeline can use an external Set 0 layout (`mVkExternalDescriptorSetLayout`) from the first Pipeline, skipping Set 0 allocation and descriptor writes. `RecordDrawIndirectSet1()` binds only Set 1 at set index 1, assuming Set 0 is already bound by ModelPipeline. Descriptor set cleanup in `Destroy()` handles both sets and correctly skips destroying Set 0 layout when using external layout.

**Indirect Rendering**: Host-visible indirect buffers use standard `VkDrawIndexedIndirectCommand` struct. Buffers are pre-mapped via VMA and zero-initialized to prevent undefined behavior. `WriteIndirectBuffer()` updates draw parameters (indexCount, instanceCount, firstIndex, vertexOffset, firstInstance) and flushes host writes for memory synchronization.

**Demand-Driven Texture Loading**: `WriteDescriptorSets()` collects texture CRCs into `mTextureCrcs` during descriptor set creation (for both model per-material textures and combined image samplers). `WriteIndirectBuffer()` checks `mbTexturesRequested` on the first call with a non-zero instance count and triggers `RequestChunkLoad()` for all collected texture CRCs, deferring texture loading until the pipeline actually renders. The request is idempotent -- pipeline recreation (which resets `mbTexturesRequested` via destruction) is safe.

**Update-After-Bind**: Pipelines with `kUpdateAfterBind` flag use a separate descriptor pool and can update storage buffer, combined image sampler, and sampled image descriptors at runtime without command buffer re-recording. `ConfigureUpdateAfterBind()` sets `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT` on storage buffer, combined image sampler, and sampled image binding types.

**Runtime Descriptor Updates**: `UpdateStorageBufferDescriptor()` updates a single storage buffer binding for one framebuffer. `UpdateCombinedImageSamplerDescriptor()` updates a combined image sampler binding across all framebuffer descriptor sets. `UpdateTextureArrayDescriptor()` updates a sampled image array binding across all framebuffer descriptor sets. These are used by TextureManager's deferred descriptor update system to propagate lazy-loaded texture image views to pipelines at runtime.

**Texture Binding Registration**: During descriptor set creation, pipelines register texture bindings with TextureManager via `RegisterTextureBinding()` and `RegisterTextureArrayPipeline()`, enabling deferred descriptor updates when lazy-loaded textures arrive. Registration is guarded by a shader layout check (`bindingExistsInShaderLayout`) that verifies the binding exists in the vertex or fragment shader before registering, preventing pipelines with sparse layouts (e.g., shadow pipelines) from registering for deferred updates on bindings they don't use.

**Draw Recording**: Multiple draw variants support direct, indirect, and alternate pipeline/descriptor set combinations for flexible rendering patterns.

### Shader
SPIR-V shader module wrapper. `ShaderInfo` holds a pointer to the chunk header, pointers into the chunk data payload for descriptor bindings, per-binding set indices (`pDescriptorSetIndices`), and vertex attributes (zero-copy), and the SPIR-V bytecode size. Creates VkShaderModule from SPIR-V bytecode in file chunks with debug naming for profiling tools. The chunk data layout is `[bindings ALIGN16] [setIndices ALIGN16] [attrs ALIGN16] [SPIR-V]`.

### Texture
Image resource and render target management. Supports mipmaps and texture arrays with batched upload operations.

**Lazy Loading**: `InitDeferred()` stores metadata and borrows a placeholder VkImageView (white texture) without any GPU allocation. `Create()` allocates the real GPU image when data arrives. `AdoptTransferredImage()` takes GPU handles (VkImage, VmaAllocation, VkDeviceMemory) by reference and transfers ownership via `std::exchange`, nulling the source handles atomically; it then creates a VkImageView but does not record or submit any barriers itself. `RecordAcquireBarrier()` records a QFOT acquire barrier into an externally-provided command buffer; when QFOT is optional (VK_KHR_maintenance9), uses IGNORED queue family indices and SHADER_READ_ONLY_OPTIMAL as old layout (no-op transition to avoid sync hazards with transfer queue), otherwise performs explicit QFOT acquire matching the release barrier recorded by TextureUploadManager. TextureManager batches all acquire barriers into a single command buffer submitted before the global command buffer. `UpdateData()` updates texture data in-place via staging buffer with layout transitions. Texture loaded state is tracked by `ChunkState::kReady` in the `LazyChunk`, not by TextureFlags.

**Layout Transitions**: `TransitionImageLayout()` supports `kUndefined`, `kColorAttachment`, `kGeneral`, `kShaderReadOnly`, `kTransferDestination`, and `kTransferSource` layouts with appropriate access masks and pipeline stage flags for each transition direction. The `kTransferSource` layout enables textures to serve as copy sources for `vkCmdCopyImage` operations (used by the wind simulation's copy+simulate architecture).

**Render Targets**: Textures with `kRenderPass` flag automatically create a VkRenderPass and VkFramebuffer. Optional depth buffer created via `kDepth` flag. VMA uses dedicated allocations for large render targets (>32MB). Subpass dependency ensures read-after-write correctness for shader reads, compute access, and transfer operations (e.g., `vkCmdCopyImage` from a render target).

**Component Swizzle**: BC4 textures (single-channel) automatically get RRRR swizzle for correct sampling unless they are render targets.

## Key Patterns

**Update Safety**: Buffer/texture updates must occur after fence wait to avoid modifying GPU-in-use resources.

**Descriptor Set Lifecycle**: Swap chain resize recreates framebuffers, requiring descriptor set recreation. Descriptor sets are explicitly freed back to the pool during pipeline destruction, using the correct pool (main or update-after-bind) based on pipeline flags.

**Manager Ownership**: All objects created and owned by corresponding managers (BufferManager, TextureManager, ShaderManager, PipelineManager, CommandBufferManager).
