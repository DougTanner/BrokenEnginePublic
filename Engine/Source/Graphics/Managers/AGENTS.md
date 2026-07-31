# Managers - Vulkan Renderer Services

Vulkan services exposed through `gp*` globals. `Graphics` constructs its managers in dependency order and destroys or resets them in reverse; `TextureUploadManager` is the exception, owned by `Main` so its thread can span graphics recreation.

## Shared Contracts

- A manager owns a `gp*` singleton; owned-by-value helpers are reached through their manager. Constructors publish the global and destructors clear it.
- Swapchain- and screen-dependent resources use paired destroy/create phases without replacing every manager. Full device recreation also tears down and rebuilds transfer resources around the independently owned upload manager.
- Global and Main command buffers are immutable between recreation events; ImGui records per frame. Fence and semaphore ownership is documented by the submitting manager references below.
- Queue synchronization depends on selected queue families: uploads may transfer ownership to graphics, and present waits for graphics when present uses a distinct queue. Do not assume all queues alias or that every transition is cross-queue.
- The shared descriptor pool serves graphics and compute pipelines. Most pipelines consume global Set 0, but legacy standalone compute pipelines retain their own layouts and do not. Pipeline-specific sets and bindless arrays must remain valid through partial recreation.
- The texture-descriptor registry owns island bindless-slot metadata and descriptor registrations. Fixed bindless-array storage addresses are registry keys. During the post-fence bindless write epoch (a time window in which bindless descriptor writes are safe), eviction redirects all five island channels to slot-0 placeholders and retires their registrations before any view is freed or slot is reused; restoration exposes elevation only after the four lazy channels are ready.
- A pipeline rebuild clears raw pipeline registrations but preserves live island-slot metadata. Registering a rebuilt bindless consumer must recreate its live per-slot descriptors so an in-flight lazy adoption still reaches that pipeline.
- Boot fails loud when the device does not advertise `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT` (the device's promise that it can blend into a given pixel format) for every special-format render target the MAX/ADD-blended prepasses write: elevation, lighting, smoke, and wind. Blending without it is silent undefined behavior at pipeline creation, so this device dependency is hard, unlike the sampler linear-filter probe that downgrades gracefully. Adding a blended prepass on a new format must extend that guard.
- Pack-backed shaders, models, fonts, and textures are trust boundaries. Validate declared ranges, counts, dimensions, and derived byte sizes against the resident chunk before allocation or copy. Boot consumers throw `CorruptStreamException`; per-frame texture adoption soft-fails and preserves its placeholder.

## Responsibility Map

- InstanceManager (`InstanceManager.AGENTS.md`) and DeviceManager (`DeviceManager.AGENTS.md`) - Vulkan instance, device, queues, allocator, and capabilities
- SwapchainManager (`SwapchainManager.AGENTS.md`) - Swapchain, render passes, framebuffers, and presentation
- CommandBufferManager (`CommandBufferManager.AGENTS.md`) - Record-once submission graph and frame synchronization
- BufferManager (`BufferManager.AGENTS.md`) - GPU buffers and per-frame dynamic storage
- TextureManager (`TextureManager.AGENTS.md`) and TextureUploadManager (`TextureUploadManager.AGENTS.md`) - Textures, descriptors, render targets, and staged uploads
- PipelineManager (`PipelineManager.AGENTS.md`) - Fixed engine and dynamic collection pipelines
- ParticleManager (`ParticleManager.AGENTS.md`) - Thread-safe CPU staging for GPU particles
- ImGuiManager (`ImGuiManager.AGENTS.md`) - UI recording, submission, scaling, and opaque regions

Renderer-wide frame and recreation ordering stays in Graphics; manager references own manager-specific algorithms and failure modes.
