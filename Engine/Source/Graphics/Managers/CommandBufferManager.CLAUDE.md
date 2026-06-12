# CommandBufferManager

**Global**: `gpCommandBufferManager`

Records and submits Vulkan command buffers using a record-once, submit-many pattern. Re-recorded only on resize, device loss, or settings changes. The manager itself owns the record/submit entry points; per-pass recording bodies live in `CommandBufferRecordGlobal` / `CommandBufferRecordMain` helper structs (each exposes a single static `Record(iFramebuffer)`).

## Command Buffer Types

Global (shadows, terrain generation, wind/smoke spread, particle spawn/update), Main (lighting, smoke emit, object shadows, then main render pass), and ImGui (UI overlay, recorded per-frame).

## Smoke Spread Recording

Smoke spread uses **hierarchical indirect dispatch** rather than direct full-grid dispatch. The sequence runs twice per frame (once for each ping-pong direction):

1. **Dilate+Compact** (`kPipelineSmokeOccupancyDilate`) - reads the bit-packed occupancy buffer, dilates active tiles, and writes a compacted tile index list into `mSmokeActiveTileVkBuffer` (which also serves as the indirect dispatch args buffer).
2. `vkCmdFillBuffer` resets the occupancy buffer to zero.
3. `vkCmdClearColorImage` clears the output smoke texture (requires `VK_IMAGE_USAGE_TRANSFER_DST_BIT`).
4. **Indirect spread** (`kPipelineSmokeSpreadComputeB` or `kPipelineSmokeSpreadComputeA`) dispatched via `vkCmdDispatchIndirect` from `mSmokeActiveTileVkBuffer`; each workgroup looks up its tile from the active tile list.

Full sequence per frame: Dilate+Compact → Fill → Clear → indirect SpreadB → Dilate+Compact → Fill → Clear → indirect SpreadA. Each spread pass transitions the output texture from `kShaderReadOnly` to `kComputeReadWrite` before dispatch and back to `kShaderReadOnly` after. Pass B writes the larger `mSmokeTextureTwo`; pass A writes `mSmokeTextureOne`.

## Lighting Pipeline Order

After the lighting deposit render pass (area lights, point lights, lighting particles), the spread passes run sequentially. Barriers enforce: fragment write → fragment read (spread). Each spread pass uses a dedicated render pass with MRT to write all three spread textures in a single draw.

## Main Render Pass Order

Opaque models, terrain, water, hex shields, transparent models, particles (long then square), visible lights, billboards, text. Opaque materials drawn first with depth writing; transparent materials drawn after water/hex shields with alpha blending. In debug builds, the entire geometry sequence can be replaced by a single fullscreen debug texture draw (toggled at runtime via F2), with only profile text remaining.

## Synchronization

Three-stage GPU submission (Global -> Main -> ImGui) with semaphore chains. Binary semaphore for cross-command-buffer particle storage buffer synchronization between frames. Uniform buffer copies rely on RecordCopy()'s internal post-copy barriers and vkQueueSubmit's implicit host-write memory dependency. Multi-threaded submission via PersistentWorker at time-critical priority.

## Recorded Flag

The per-framebuffer `kRecorded` flag is an idempotence guard, not a runtime re-record mechanism — set on first record and cleared only by destroy/recreate (resize, device loss, settings change), per the parent's CB re-record ban.
