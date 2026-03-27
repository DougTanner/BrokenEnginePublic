# Graphics Objects - RAII Vulkan Resource Wrappers

## Overview

Low-level Vulkan resource wrappers providing RAII semantics for GPU resources. All classes encapsulate Vulkan handles with automatic lifecycle management, move semantics, and VMA-based GPU memory allocation. Each object type is created and owned by its corresponding manager in `Managers/`.

## Key Classes

- **Buffer** - GPU memory buffer (vertex, index, uniform, storage) with dual-buffer staging architecture for CPU-to-GPU transfer. Supports batched pipeline barriers via `RecordBarriers()`
- **CommandBuffers** - Per-framebuffer command buffer allocation with semaphore (GPU-GPU) and fence (CPU-GPU) synchronization. Creates Global, Main, and ImGui primary command buffers per swap chain image
- **Pipeline** - Complete Vulkan pipeline state for graphics and compute. Combines shaders, vertex input, render state, and descriptor bindings. `PipelineInfo::iColorAttachmentCount` controls MRT blend state expansion (default 1; set to 3 for R/G/B blur pipelines). Split across three files by responsibility:
  - `Pipeline.cpp` - Core lifecycle, command recording, indirect buffer writes
  - `PipelineCreator.cpp` - Vulkan pipeline/layout object creation
  - `PipelineDescriptorWriter.cpp` - Descriptor set allocation and writes. `UpdateStorageImageDescriptor()` updates a single storage image binding in place (used by lighting blur pipelines to point at per-texture intermediate and result images)
- **ModelPipeline** - Multi-material pipeline wrapper that creates a Pipeline per material with per-material descriptor sets and indirect draw buffers. Supports two-pass transparency rendering (opaque/transparent). Defines `kModelPipelineBindingMeshData` and `kModelPipelineBindingJointMatrix` binding index constants used by `BufferManager` when updating descriptors after buffer growth
- **Shader** - SPIR-V shader module wrapper with zero-copy pointers into chunk data for descriptor bindings, set indices, and vertex attributes
- **Texture** - Image resource and render target management with mipmaps, texture arrays, and lazy loading. Supports deferred loading via placeholder swap, render target auto-creation, and layout transitions with synchronization. `RecordBeginRenderPass` accepts a `RenderPassFlags_t` (combining `kDepth`, `kMultisampling`, `kClear`) instead of individual booleans

## Architecture Notes

- Per-framebuffer descriptor sets prevent GPU conflicts across frames in flight
- Three-set descriptor layout for model pipelines: Set 0 (global/TextureManager), Set 1 (shared), Set 2 (per-material). In the `kMultiSet` path, `ModelPipeline::RecordDrawIndirect` binds Set 0, Set 1, and the vertex buffer once before the material loop; each `Pipeline::RecordDrawIndirectSet2` call then only rebinds the pipeline and Set 2
- Lazy texture loading uses placeholder images until background upload completes, then atomically adopts GPU resources
- Pipelines support update-after-bind for runtime descriptor updates without command buffer re-recording
- Indirect rendering supports both host-visible (CPU-written) and device-local (GPU compute-written) buffer types

## See Also

- [../Managers/CLAUDE.md](../Managers/CLAUDE.md) - Manager implementations that own these objects
- [Graphics Pipeline diagram](../../../../Documents/Architecture/GraphicsPipeline.md)
