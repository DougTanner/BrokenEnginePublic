# `/Engine/Source/Graphics/Objects/`

Low-level Vulkan resource wrappers providing RAII semantics for GPU resources. Each class encapsulates Vulkan handles with automatic lifecycle management.

## Design Philosophy

**RAII Pattern**: All Vulkan resources wrapped with automatic cleanup in destructors
**Zero-Copy**: Objects use move semantics, copying disabled where appropriate
**Type Safety**: Strong typing with enum flags for configuration
**Batched Operations**: Pipeline barriers and resource copies batched to minimize driver overhead
**VMA Integration**: Vulkan Memory Allocator handles all GPU memory allocation decisions

## Classes

### Buffer
GPU memory buffer wrapper supporting vertex/index/uniform/storage buffers with automatic memory allocation.

**Purpose**: Simplifies GPU buffer creation and memory management for rendering and compute operations.

**Architecture**:
- Dual-buffer support (host-visible staging + device-local GPU buffer) when needed
- VMA automatic memory type selection based on usage flags with `VMA_MEMORY_USAGE_AUTO`
- VMA persistent memory mapping via `VMA_ALLOCATION_CREATE_MAPPED_BIT` flag for frequently-updated buffers
- Batched pipeline barrier recording reduces synchronization overhead
- Buffer sizes rounded to `nonCoherentAtomSize` for alignment requirements

**Key Features**:
- Creates appropriate buffer types based on flags (vertex, index, uniform, storage)
- Host-to-device copy operations with automatic barrier insertion
- Static `RecordBarriers()` method batches multiple barriers into single vkCmdPipelineBarrier call
- Memory mapping abstraction for CPU-writable buffers

### CommandBuffers
Multi-frame command buffer allocation and GPU-CPU synchronization.

**Purpose**: Manages per-frame command pools, buffers, and synchronization primitives for the rendering pipeline.

**Architecture**:
- Pre-allocates command pools and buffers for each swap chain framebuffer
- Primary command buffer types: Global (preprocessing) and Image (main rendering)
- Secondary command buffers: 3 glTF pipelines (Player, Spaceships, PlayerMissiles), 5 lighting pipelines (AreaLights, PointLights, HexShieldsLighting, LongParticlesLighting, SquareParticlesLighting), 1 scene buffer (grouped non-glTF/non-lighting rendering)
- Semaphore-based GPU synchronization between command buffer stages
- Fence-based CPU-GPU synchronization for safe resource updates
- Frame cycling via `Next()` method

**Secondary Command Buffer Organization**:
- Dedicated secondary buffers for each glTF pipeline type enable per-object-type selective re-recording
- Dedicated secondary buffers for each lighting pipeline enable per-light-type selective re-recording within MRT lighting pass
- Scene secondary buffer groups all non-glTF/non-lighting rendering (terrain, water, visible lights, widgets, text) for efficiency
- All secondary buffers inherit render pass state from primary command buffer
- Foundation for future multithreaded command recording

### GltfPipeline
Multi-material pipeline wrapper specialized for glTF model rendering.

**Purpose**: Extends Pipeline class to support models with multiple materials and indirect rendering.

**Architecture**:
- Creates separate pipeline per material in glTF model
- Per-material descriptor sets and indirect draw buffers
- Tracks index counts and starting indices for each material submesh
- Integrates with glTF file format material data

### Pipeline
Complete Vulkan pipeline state encapsulation for graphics and compute operations.

**Purpose**: Abstracts graphics/compute pipeline creation with automatic descriptor set management and render state configuration.

**Architecture**:
- Combines shader modules, vertex input, render state, and resource bindings
- Per-framebuffer descriptor sets for dynamic resources (recreated on swap chain resize)
- Push constant support for small per-draw data
- Indirect rendering buffer management for GPU-driven rendering
- Automatic MRT blend state configuration when using lighting render pass

**Key Design Decisions**:
- Descriptor sets allocated per framebuffer to avoid GPU resource conflicts
- No shader fallbacks - requires valid shaders at creation time
- Vertex input state derived from buffer configuration
- Individual descriptor set cleanup in Destroy() for proper resource lifetime

**DescriptorInfo Pattern**:
- Supports three texture binding methods: CRC lookup (file textures), single pointer (render targets), array pointer (texture arrays)
- Unified interface for uniform buffers, storage buffers, samplers, and images
- Flags configure descriptor types and sampler modes

### Shader
SPIR-V shader module wrapper with validation.

**Purpose**: Loads and manages compiled shader bytecode from data chunks.

**Architecture**:
- Creates VkShaderModule from SPIR-V data in file chunks
- Debug naming for profiling and validation tools
- Simple create/destroy lifecycle tied to parent pipeline

### Texture
Image resource and render target management with lazy loading support.

**Purpose**: Handles 2D textures, render targets, framebuffers, and image layout transitions.

**Architecture**:
- Supports mipmaps and texture arrays with batched upload operations
- Empty texture creation for lazy loading (defers data upload)
- In-place data updates via `UpdateData()` without recreating VkImageView
- Render target creation with automatic depth buffer and framebuffer setup
- VMA size-based dedicated allocation for large render targets (>32MB)

**Key Features**:
- Batches all mip level copies into single command buffer (8-60x faster than per-mip submission)
- Image layout transitions with optimized pipeline stage masks
- Lazy loading pattern: create empty → update later → descriptor sets unchanged
- Static render pass recording helpers with VkSubpassContents parameter for secondary command buffer support

**TextureLayout Enum**: Defines image layout states with associated pipeline stage masks for efficient transitions (ComputeReadWrite, ComputeReadOnly, FragmentReadOnly, ShaderReadOnly, ColorAttachment, TransferDestination).

## Resource Lifetime & Synchronization

**Creation Pattern**: Constructor calls `Create()`, destructor calls `Destroy()`. Objects support move semantics for transfer of ownership.

**Update Safety**: Buffer/texture updates must occur after fence wait to avoid modifying GPU-in-use resources.

**Descriptor Set Invalidation**: Swap chain resize recreates framebuffers, requiring descriptor set recreation in all pipelines.

**Barrier Batching**: `Buffer::RecordBarriers()` accepts spans of barriers to minimize vkCmdPipelineBarrier calls (significant performance gain in particle systems).

## Manager Integration

All Objects created and owned by corresponding Managers:
- **BufferManager**: Creates vertex, index, uniform, storage buffers
- **TextureManager**: Creates textures, render targets, samplers
- **ShaderManager**: Loads SPIR-V modules from file chunks
- **PipelineManager**: Combines all above resources into renderable pipelines
- **CommandBufferManager**: Allocates command buffers for recording draw commands
