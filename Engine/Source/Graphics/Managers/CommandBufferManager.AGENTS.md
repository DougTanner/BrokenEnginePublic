# CommandBufferManager

**Global**: `gpCommandBufferManager`

Records and submits Vulkan command buffers using a record-once, submit-many pattern. Re-recorded only on resize, device loss, or settings changes. The manager itself owns the record/submit entry points; per-pass recording bodies live in `CommandBufferRecordGlobal` / `CommandBufferRecordMain` helper structs (each exposes a single static `Record(iFramebuffer)`).

## Command Buffer Types

Global (shadows, terrain generation, wind/smoke spread, particle spawn/update), Main (lighting, smoke emit, object shadows, then main render pass), and ImGui (UI overlay, recorded per-frame).

## Smoke Spread Recording

Smoke spread uses hierarchical indirect dispatch rather than direct full-grid dispatch. Each ping-pong half:

1. Dilates input occupancy together with the output texture's existing occupancy and compacts active tiles into the shared indirect-dispatch list.
2. Resets only output occupancy after the dilate consumes its stale-storage union term.
3. Indirect-spreads into the output texture and re-marks occupancy for nonzero tiles.

Pass B uses direct tile lookup; pass A uses the previous-area remap. Each pass transitions only its output texture to compute read/write and back, with no per-frame image clear.

The enable/recreate clear edge is recorded in Main after Global spread. An indirect-gated fullscreen draw clears TextureTwo in its own render pass, followed by a matching TextureOne clear at the start of the deposit render pass; stale occupancy drains in the following spread.

## Lighting Pipeline Order

After the lighting deposit render pass (area lights, point lights, lighting particles), the spread passes run sequentially. Barriers enforce: fragment write → fragment read (spread). Each spread pass uses a dedicated render pass with MRT to write all three spread textures in a single draw.

## Main Render Pass Order

Opaque models, terrain, water, hex shields, transparent models, particles (long then square), visible lights, and billboards. Opaque materials draw first with depth writing; transparent materials draw after water/hex shields with alpha blending. In debug builds, the entire geometry sequence can be replaced by a single fullscreen debug texture draw (toggled at runtime via F2). Fixed profile and debug text renders later in the ImGui submission.

## Synchronization

Three-stage GPU submission (Global -> Main -> ImGui) with semaphore chains. Binary semaphore for cross-command-buffer particle storage buffer synchronization between frames. Uniform buffer copies rely on RecordCopy()'s internal post-copy barriers and vkQueueSubmit's implicit host-write memory dependency. Multi-threaded submission via PersistentWorker at time-critical priority.

## Recorded Flag

The per-framebuffer `kRecorded` flag is an idempotence guard, not a runtime re-record mechanism — set on first record and cleared only by destroy/recreate (resize, device loss, settings change), per the parent's CB re-record ban.
