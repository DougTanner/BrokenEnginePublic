# Graphics Objects - RAII Vulkan Resource Wrappers

Low-level Vulkan resource wrappers providing RAII semantics for GPU resources. All classes encapsulate Vulkan handles with automatic lifecycle management via constructors/destructors, move semantics, and VMA-based GPU memory allocation.

## Classes

### Buffer
GPU memory buffer wrapper supporting vertex/index/uniform/storage buffer types. Dual-buffer architecture (host-visible staging + device-local GPU) enables efficient CPU-to-GPU data transfer. VMA selects allocation strategies based on usage pattern (readback, indirect, or upload). Static `RecordBarriers()` batches multiple buffer barriers into a single pipeline barrier call using the workbuffer for temporary storage.

### CommandBuffers
Per-framebuffer command buffer allocation and synchronization. Creates one command pool per swap chain framebuffer with Global, Main, and ImGui primary command buffers. Manages semaphores for GPU-GPU synchronization between stages and fences for CPU-GPU synchronization.

### ModelPipeline
Multi-material pipeline wrapper for model rendering. Creates a separate Pipeline per material with per-material descriptor sets and indirect draw buffers. Detects transparent materials from scene data and supports selective draw passes (opaque-only, transparent-only, or all) for two-pass transparency rendering. Shadow pipelines skip transparency overrides.

**3-Set Descriptor Layout**: With `kMultiSet`, uses three descriptor sets -- Set 0 (global from TextureManager), Set 1 (shared across materials, owned by first Pipeline), Set 2 (per-material). Sets 0 and 1 bind once before the material loop; each material binds only its own Set 2.

**Demand-Driven Loading**: Two levels work together -- ModelPipeline triggers scene texture loading on first non-zero-count write, while each inner Pipeline triggers loading of its own combined image sampler textures. Model textures are accessed via the global bindless texture array rather than per-material descriptors.

### Pipeline
Complete Vulkan pipeline state for graphics and compute operations. Combines shader modules, vertex input, render state, and resource bindings. Supports multiple blend modes (alpha, additive, alpha-modulated additive, max). Split across three files by responsibility: Pipeline.cpp (core lifecycle, command recording, indirect buffer writes), PipelineCreator (Vulkan pipeline/layout creation), PipelineDescriptorWriter (descriptor set allocation and writes).

**Descriptor System**: Per-framebuffer descriptor sets prevent GPU conflicts. The `kModel` flag auto-adds lighting, shadow, smoke, mesh data, and joint matrix descriptors. Supports explicit binding assignment for sparse layouts. Three texture binding paths: CRC-based (deferred loading), pointer-based (runtime textures), and array-based. Pipelines register bindings with TextureManager so lazy-loaded textures can propagate descriptor updates at runtime.

**Global Set 0**: All non-compute graphics pipelines automatically use TextureManager's shared Set 0 (uniform buffers, bindless texture array, samplers). Bindings are split by shader reflection set indices; Set 0 bindings are dropped from per-pipeline sets since they are managed globally. Compute pipelines use a single descriptor set.

**Indirect Rendering**: Two indirect buffer types -- host-visible (CPU-written per command buffer) and device-local (GPU compute-written, single shared command). Buffers are pre-mapped via VMA and zero-initialized. Demand-driven texture loading defers chunk requests until first non-zero-count render.

**Update-After-Bind**: Pipelines with this flag can update storage buffer, combined image sampler, and sampled image descriptors at runtime without command buffer re-recording. Runtime update methods support per-framebuffer storage buffers, cross-framebuffer image samplers, and sampler-only updates for TextureManager's deferred descriptor system.

### PipelineCreator
Static helper that builds Vulkan pipeline and layout objects for Pipeline. `CreateGraphicsPipeline` merges vertex/fragment shader descriptor layouts, configures rasterization and blend state from PipelineFlags, handles multi-set layout creation (Set 0 global, Set 1 per-pipeline, Set 2 per-material), and creates indirect buffers. `CreateComputePipeline` handles compute shader layout and pipeline creation. Uses file-static Vulkan create-info structs that are mutated in place for each pipeline creation.

### PipelineDescriptorWriter
Static helper that allocates and writes Vulkan descriptor sets for Pipeline. `Write` iterates DescriptorInfo entries to build descriptor writes for uniform/storage buffers, combined image samplers, standalone samplers, storage images, bindless texture arrays, and model-specific descriptors (PBR materials, irradiance, pre-filtered, LUT BRDF). Filters writes by shader layout for sparse bindings and routes writes to the correct set index for multi-set pipelines. Runtime update methods (`UpdateStorageBuffer`, `UpdateCombinedImageSampler`, `UpdateSampler`) support per-framebuffer or cross-framebuffer descriptor updates for update-after-bind pipelines.

### Shader
SPIR-V shader module wrapper with zero-copy pointers into chunk data for descriptor bindings, per-binding set indices, and vertex attributes. Chunk data layout: bindings, set indices, attributes, then SPIR-V bytecode (each section aligned).

### Texture
Image resource and render target management with mipmaps, texture arrays, and lazy loading support.

**Lazy Loading**: Deferred textures start with a borrowed placeholder image view and no GPU allocation. Ownership transfer from the upload thread uses handle exchange to atomically adopt GPU resources, followed by queue family ownership transfer barriers (simplified when VK_KHR_maintenance9 is available).

**Render Targets**: Textures with `kRenderPass` flag automatically create a VkRenderPass and VkFramebuffer with optional depth buffer. VMA uses dedicated allocations for large render targets. Subpass dependency ensures read-after-write correctness for shader reads, compute, and transfer operations.

**Layout Transitions**: Supports transitions between undefined, color attachment, compute read/write, general, shader read-only, transfer destination, and transfer source layouts with appropriate synchronization. BC4 single-channel textures automatically get RRRR component swizzle unless they are render targets.

## Key Patterns

- **Manager Ownership**: All objects created and owned by corresponding managers (BufferManager, TextureManager, PipelineManager, CommandBufferManager)
- **Descriptor Set Lifecycle**: Swap chain resize triggers descriptor set recreation; sets are explicitly freed back to DeviceManager's pool during pipeline destruction
