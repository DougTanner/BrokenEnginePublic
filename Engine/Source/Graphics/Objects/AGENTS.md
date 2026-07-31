# Graphics Objects - Vulkan Resource Wrappers

Client-only RAII wrappers for Vulkan buffers, textures, shaders, pipelines, and command buffers. Graphics managers own collections of these objects; the objects own individual handles, allocation state, and construction helpers.

## Lifetime and Synchronization

- Object `Destroy` methods do not wait for GPU work. Destroy potentially in-flight resources only after device/fence synchronization, through an owning manager's path that retires objects later, once the GPU is done with them, or inside the renderer's post-fence descriptor-patch window.
- Pipeline descriptor registrations contain raw pipeline back-references. Individual `Pipeline::Destroy` calls unregister them; whole-`PipelineManager` clear remains defensive.
- Texture image creation or adoption increments its generation (the reuse counter). Descriptors snapshot that generation, while destruction is detected through the null live image; both signals protect cached bindings from recycled handles.
- Lazy textures borrow the placeholder view while their image is null. Destruction must not free that borrowed view. Eviction and descriptor fallback policy belong to Managers (`../Managers/AGENTS.md`).

## Resource Contracts

- Persistent mapped, one-shot staged, and copy-every-frame buffers have distinct ownership and barrier requirements. Keep draw indexing within the slots allocated for that mode.
- Descriptor arrays end at the first empty entry. Use each descriptor's resolved Vulkan binding for deferred updates.
- A uniform-buffer descriptor must name the routing its source uses: `kGlobalLayoutUniformBuffers` for the per-frame global layout, `kMainLayoutUniformBuffers` for the per-frame main layout, and `kPerCommandBufferUniformBuffers` for any other caller-supplied array indexed by framebuffer. Per-frame data never uses plain `kUniformBuffer`, which binds one buffer shared by every frame in flight — the pipeline then reads a buffer the CPU is concurrently writing for another frame, giving stale or torn values with no validation error.
- Vulkan viewports use negative height to retain the DirectX coordinate convention; pipeline front-face state and uploaded matrices depend on that choice.
- Pipeline creation trusts shader reflection only after validating the expected vertex input and descriptor shape. Pack-derived model ranges, material starts, and texture indices are file trust boundaries and throw `common::CorruptStreamException` when invalid.
- Model pipeline registrations are rebuilt as a unit. Per-material draws preserve empty ranges, transparency filtering, and the manager-owned shared descriptor inputs.
- Indirect indexed draws read a negative index count as "draw the whole vertex buffer". An explicit zero stays a zero-index draw, which is how a valid empty material range is expressed. Passing `0` to mean "draw everything" silently draws nothing with no validation message, and normalizing a negative count to zero breaks whole-buffer draws.
- A pipeline created with one color attachment against the lighting render pass is upgraded to three attachments, with its blend state copied to each. Expect that upgrade rather than an attachment-count mismatch, and do not add a second mechanism alongside it.

## See Also

- Managers (`../Managers/AGENTS.md`) - Shared resource, descriptor, and synchronization ownership
