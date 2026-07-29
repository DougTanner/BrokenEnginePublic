# Graphics - Vulkan Rendering System

Client-only multi-pass Vulkan renderer. `gpGraphics` owns renderer-wide creation, frame sequencing, recreation, world-area projection, and capture; Managers (`Managers/AGENTS.md`) own shared Vulkan resource and synchronization contracts, while Objects (`Objects/AGENTS.md`) own individual RAII handles.

## Frame and Resource Lifecycle

- Frames submit Global, Main, and ImGui work before present. The frame boundary is between Main submission and the next acquire.
- Global and Main command buffers are recorded once and rebuilt only by the recreation pipeline. Per-frame variation belongs in host-visible buffers, descriptors, and indirect commands; resources discovered within existing capacity must not depend on a re-record.
- `RenderGlobal` pre-scans descriptor churn. When island eviction, lazy adoption, or restoration is pending, it drains every framebuffer fence, opens the bindless write epoch, then performs those mutations; a completion that races the scan waits for the next frame. Descriptor writes must not race in-flight samplers.
- Texture-adoption acquire barriers are single-publication work: clear prior publication before the churn scan, and let only a fresh adoption republish barriers for Global submission. An idle frame must not replay prior barriers.
- Destroy tiers are monotonic within a frame. Selective texture, sampler, command-buffer, pipeline, swapchain, and surface rebuilds preserve lower-tier resources; full teardown waits for uploads and device work before destroying dependencies.
- A zero-area swapchain extent defers swapchain-tier recreation. The render loop skips submission, retries creation at sim-tick cadence, and acquires again only after recreation succeeds; surface-loss recovery remains a full recreate.
- Every source in this subtree is whole-file `BT_CLIENT`-guarded and client-project-only. Shared simulation code that touches client graphics state needs its own `BT_CLIENT` guard.

## World and Camera Contracts

- `CameraBase` owns matrices, world/screen projection, visible-area LOD, and grid-snapped render extents. Game code derives `Camera` from it.
- Visible-area edges snap to the water quad grid. Zoom and LOD use hysteretic buckets so quad size remains stable across adjacent frames; terrain, water, shadow, and lighting areas must preserve this shared grid contract.
- Shadow and lighting texel sizes track independent camera-height references that expand immediately with outward zoom and contract at their existing rates inward. The references never fall below live eye height, preserving raw-frustum coverage without changing settled on-screen density; configured filter and spread margins remain subject to texture-bounds clamping. Outward live-eye movement marks lighting history so the next `RenderGlobal` uses pure-current data and re-seeds history; deferred rendering leaves the mark pending. Shadow instead rejects newly exposed history through its prior-valid-window bounds.
- Terrain rendering uses boot-fixed island templates, a stable 64 MiB mesh arena, and per-framebuffer instance and indirect storage. The record-once terrain command buffer binds the arena handle once; drained residency updates publish or revoke each template's arena subrange through indirect records, never by replacing that handle or re-recording. Zero every framebuffer's indirect record before returning arena subranges for reuse. Arena teardown invalidates every suballocation, while File-owned asynchronous range state survives.
- Terrain collision belongs to shared Frame island terrain (`../Frame/AGENTS.md`), not this client renderer.

## Assets and Capture

- Animation data aliases eagerly loaded pack memory and therefore cannot outlive `FileManager`. Validate all pack-derived ranges and indices before constructing aliases; temporary transforms use the workbuffer.
- DirectXMath matrices are uploaded without an extra transpose. Animation nodes remain parent-before-child, enabling a single forward world-transform pass.
- Screenshot and render-target capture run after the ImGui submission is enqueued and before present. The fence signal must already be pending before readback waits, or capture deadlocks. Requests enter through `Graphics` mailboxes; asynchronous encoders own separate thread-local state.

## See Also

- Managers (`Managers/AGENTS.md`) - Cross-manager lifecycle, synchronization, and resources
- Objects (`Objects/AGENTS.md`) - Vulkan object ownership
- Render (`Render/AGENTS.md`) - Per-frame buffer publication and pass ordering
- Debug (`Debug/AGENTS.md`) - Debug rendering
