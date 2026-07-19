# Graphics Objects - RAII Vulkan Resource Wrappers

## Overview

Low-level Vulkan resource wrappers with automatic lifecycle management, move semantics, and VMA-based GPU allocation. Corresponding managers own each object type. `PipelineCreator` and `PipelineDescriptorWriter` are stateless helpers for pipeline construction and descriptor writes.

**Destroy discipline:** `Buffer::Destroy`, `Texture::Destroy`, and `Pipeline::Destroy` do not wait for fences. Destroy possibly in-flight resources only after `vkDeviceWaitIdle`, by one-frame parking in `BufferManager::GrowMeshDataBuffer`, or during the `RenderGlobal` post-fence-wait descriptor-patch window.

## Descriptor Model

- Descriptor arrays terminate at the first empty entry. Bindings are sequential unless a `DescriptorInfo` supplies an explicit binding; deferred registrations and `Update*` calls must pass that resolved Vulkan binding, not the loop index. Bindings 15/16 are reserved for mesh-data and joint-matrix storage buffers.
- Model pipelines use Set 0 global, Set 1 shared, and Set 2 per-material. Model descriptor writes append the renderer-owned lighting, shadow, smoke, mesh-data, joint-matrix, bindless, IBL, and material-buffer descriptors; callers leave those slots empty.
- **Rebuild-only pipeline lifecycle.** Descriptor writes register raw `Pipeline*` back-references for deferred texture/sampler updates, and only a whole-`PipelineManager` rebuild clears them. `Pipeline::Destroy` does not unregister, so settings and device-loss paths rebuild the complete manager rather than individual pipelines.
- Descriptor writes skip global Set 0 and route remaining bindings by reflected set. Compute shaders opt in to `[global Set 0, per-pipeline Set 1]` by declaring Set 1; first-party shaders use explicit sets except the standalone particle spawn/update compute shaders. Lazy CRC registrations occur only on framebuffer 0.
- Bindless texture arrays use `UINT32_MAX` until layout creation resolves their count. Consumers of arrays mutated in place register the live array pointer rather than snapshotting placeholder entries. Update-after-bind arrays are partially bound, but every populated element must still reference a live view; eviction restores the slot-0 placeholder as described in `../Managers/AGENTS.md`.
- Per-command-buffer descriptors and host-visible indirect buffers select the framebuffer-indexed per-pipeline set; otherwise it uses slot 0. Compute, indirect graphics, multi-set model, and UI depth-prepass paths always framebuffer-index global Set 0. Plain draws share the per-pipeline index. A pipeline reading the per-frame global UBO uses `kGlobalLayoutUniformBuffers`, while one reading the per-frame main UBO uses `kMainLayoutUniformBuffers`; other caller-supplied framebuffer-indexed uniform arrays use `kPerCommandBufferUniformBuffers`. None use plain `kUniformBuffer`.

## Buffer Staging Modes

Three mutually exclusive modes: persistent host-mapped, device-local with one-shot staging, and copy-every-frame with retained buffers and copy barriers. `StagingBuffer` is a non-copyable, scope-owned VMA allocation used only by device-local `Buffer::Create`, `Texture::UploadImageData`, and `TextureCache` readback; persistent manager staging such as `TextureUploadManager` remains separately owned. VMA classifies host-visible requests as readback, indirect, or upload. Graphics indirect buffers remain host-visible/coherent and allocate `max(framebufferCount, 3)` slots. Compute indirect buffers are either GPU-written device-local single-slot or CPU-written host-visible per-framebuffer; record calls must stay within the allocated slot count.

## Pipeline Creation

- Viewport uses negative height (`VK_KHR_maintenance1`) to flip Y to DirectX convention; front face is therefore counter-clockwise.
- Single-attachment pipelines using the lighting render pass auto-upgrade to 3 color attachments with replicated blend state.
- Vertex input comes from shader reflection: stride must match a bound vertex buffer; a reflected stride without a create-time buffer enables per-draw binding, while stride 0 means no vertex input.
- Indirect indexed draws use a negative omitted index-count sentinel for the whole buffer; an explicit zero remains a zero-index draw, including valid empty material ranges.
- Push constants default to `sizeof(shaders::PushConstantsLayout)` (16 bytes). Smaller shader blocks require a `PipelineInfo` override and direct push calls rather than shared `Pipeline::Record*` helpers, which always write the full layout.
- **Trust-boundary validation**: `ModelPipeline::Create` bounds the index/material alias-walk extent against `ChunkHeader::iSize`, then requires material index starts to be nondecreasing and no greater than the validated model index count; equal starts are valid empty ranges. `PipelineDescriptorWriter` bounds per-material texture-index fields against `uiTextureCount` before indexing the texture-CRC array. Out-of-range throws `common::CorruptStreamException`.
- See `../Managers/AGENTS.md` for the pipeline recreation invariant affecting descriptor writes.

## Texture Lifecycle

- `kLayoutMappings` is the sole source for each layout's Vulkan layout, access, and stage.
- Lazy path borrows a placeholder view with null image; destroy early-returns in that state so the borrowed view is never freed. Real GPU image swapped in post-upload.
- Transfer-to-graphics queue ownership has a fast path when the device reports it optional.
- Transparent materials enable alpha blending in non-shadow model passes and are filtered at draw time. `ModelPipeline::RecordDrawIndirect` callers pass push-constant `.w == 0.0f`; the method overwrites it with the material index.
- Demand-loading: non-indirect pipelines request textures immediately at create; indirect pipelines defer until the first write with a positive instance count (latched).
- Every texture handle swap increments a generation preserved across moves and destruction. Descriptor bindings snapshot it so command-buffer recording detects stale pointers to recycled handles.

## See Also

- `../Managers/AGENTS.md` - Managers that own these objects
