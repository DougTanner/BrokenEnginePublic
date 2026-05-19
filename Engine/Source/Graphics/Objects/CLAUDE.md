# Graphics Objects - RAII Vulkan Resource Wrappers

## Overview

Low-level Vulkan resource wrappers with automatic lifecycle management, move semantics, and VMA-based GPU memory allocation. Each object type is owned by its corresponding manager in `Managers/`.

## Descriptor Model

- Pipeline descriptor arrays terminate at the first empty entry; bindings sequential unless explicit. Bindings 15/16 reserved for mesh-data / joint-matrix storage buffers (joint matrices split separately to avoid an NVIDIA driver hang).
- Model descriptor auto-appends follow-on descriptors (lighting, shadow blur, smoke, mesh, joints); caller must leave those slots empty.
- Three-set layout for model pipelines: Set 0 global (TextureManager), Set 1 shared, Set 2 per-material. Multi-set mode binds Set 0/1 once then rebinds pipeline + Set 2 per material; inner materials share the first material's Set 1 layout.
- Descriptor writer drops Set 0 (global handles them) and routes the rest by shader-reflected set indices. CRC registrations for lazy/deferred updates occur only on framebuffer 0.
- Bindless texture arrays exported with `UINT32_MAX` sentinel, rewritten to actual count at layout creation.

## Buffer Staging Modes

Three mutually-exclusive modes: persistent host-mapped (CPU-written each frame), device-local (one-shot staging copy at create), and copy-every-frame (both buffers retained, barriers bracket the copy). Indirect buffers force host-visible/coherent with no transfer fallback because the GPU reads them directly; indirect slot count is `max(framebufferCount, 3)` for resize robustness.

## Pipeline Creation

- Viewport uses negative height (`VK_KHR_maintenance1`) to flip Y to DirectX convention; front face is therefore counter-clockwise.
- Single-attachment pipelines using the lighting render pass auto-upgrade to 3 color attachments with replicated blend state.
- See [../Managers/CLAUDE.md](../Managers/CLAUDE.md) for the pipeline recreation invariant affecting descriptor writes.

## Texture Lifecycle

- Layout transitions driven by a single `kLayoutMappings` table mapping enum to `(layout, access, stage)` — the sole source of truth.
- Lazy path borrows a placeholder view with null image; destroy early-returns in that state so the borrowed view is never freed. Real GPU image swapped in post-upload.
- Transfer-queue to graphics-queue ownership transfer has a fast path when the device reports it optional.
- Transparent material detection drives auto alpha-blend in `ModelPipeline` for non-shadow passes.
- Demand-loading: non-indirect pipelines request textures immediately at create; indirect pipelines defer until first non-empty indirect write (latched).
- Generation counter: every handle swap (create, transfer-adopt) bumps a monotonic per-Texture generation; move-construct preserves it and destroy does NOT reset. Descriptor-binding sites snapshot the generation alongside the texture pointer so `PipelineManager` can assert at command-buffer record time that no cached binding still points at a recycled handle.

## See Also

- [../Managers/CLAUDE.md](../Managers/CLAUDE.md) - Managers that own these objects
- [Graphics Pipeline diagram](../../../../Documents/Architecture/GraphicsPipeline.md)
