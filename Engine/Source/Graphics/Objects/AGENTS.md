# Graphics Objects - Vulkan Resource Wrappers

Client-only RAII wrappers for Vulkan buffers, textures, shaders, pipelines, and command buffers. Graphics managers own collections of these objects; the objects own individual handles, allocation state, and construction helpers.

## Lifetime and Synchronization

- Object `Destroy` methods do not wait for GPU work. Destroy potentially in-flight resources only after device/fence synchronization, through an owning manager's deferred-retirement path, or inside the renderer's post-fence descriptor-patch window.
- Pipeline descriptor registrations contain raw pipeline back-references. Individual `Pipeline::Destroy` calls unregister them; whole-`PipelineManager` clear remains defensive.
- Texture image creation or adoption increments its generation. Descriptors snapshot that generation, while destruction is detected through the null live image; both signals protect cached bindings from recycled handles.
- Lazy textures borrow the placeholder view while their image is null. Destruction must not free that borrowed view. Eviction and descriptor fallback policy belong to Managers (`../Managers/AGENTS.md`).

## Resource Contracts

- Persistent mapped, one-shot staged, and copy-every-frame buffers have distinct ownership and barrier requirements. Keep draw indexing within the slots allocated for that mode.
- Descriptor arrays end at the first empty entry. Use each descriptor's resolved Vulkan binding for deferred updates, and preserve framebuffer routing for per-frame global, main, and pipeline data.
- Vulkan viewports use negative height to retain the DirectX coordinate convention; pipeline front-face state and uploaded matrices depend on that choice.
- Pipeline creation trusts shader reflection only after validating the expected vertex input and descriptor shape. Pack-derived model ranges, material starts, and texture indices are file trust boundaries and throw `common::CorruptStreamException` when invalid.
- Model pipeline registrations are rebuilt as a unit. Per-material draws preserve empty ranges, transparency filtering, and the manager-owned shared descriptor inputs.

## See Also

- Managers (`../Managers/AGENTS.md`) - Shared resource, descriptor, and synchronization ownership
