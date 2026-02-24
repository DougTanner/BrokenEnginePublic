# CommandBufferManager

**Global**: `gpCommandBufferManager`

Records and submits Vulkan command buffers using a record-once, submit-many pattern. Re-recorded only on resize, device loss, or settings changes.

## Command Buffer Types

Global (shadows, terrain generation, wind/smoke spread, particle spawn/update), Main (lighting, smoke emit, object shadows, then main render pass), and ImGui (UI overlay, recorded per-frame).

## Main Render Pass Order

Opaque models, terrain, water, hex shields, transparent models, particles (long then square), visible lights, billboards, text. Opaque materials drawn first with depth writing; transparent materials drawn after water/hex shields with alpha blending.

## Synchronization

Three-stage GPU submission (Global -> Main -> ImGui) with semaphore chains. Binary semaphore for cross-command-buffer particle storage buffer synchronization between frames. Host-to-shader memory barrier after uniform buffer copy ensures CPU-written animation data is visible to all GPU consumers. Multi-threaded submission via PersistentWorker at time-critical priority.

## Selective Re-recording

Per-framebuffer recorded flag enables runtime command buffer updates after buffer/descriptor changes.
