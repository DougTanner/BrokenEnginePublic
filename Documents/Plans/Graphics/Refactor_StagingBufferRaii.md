# Refactor: Staging Buffer RAII & Dead VkDeviceMemory Plumbing

## Context
Source: /external-refactor-clean on Engine/Source (recursive). The Graphics/Objects correctness cluster: staging buffers leak across `DeviceLostException` on a *recoverable* path (device-loss recovery recreates Graphics in place, so the leak trips the VMA unfreed-allocation assert during recovery — the same failure class `CorruptTextureChunkLifecycleHardening` documents for a different site, not covered by any live plan), plus engine-wide write-only `VkDeviceMemory` plumbing rooted in a stale comment, and an unvalidated on-disk array driving GPU indirect draws.

## Design

### RAII staging buffer (headline)
- Add a ~15-line RAII staging-buffer struct (create in ctor, `vmaDestroyBuffer` in dtor) and adopt at both manual-acquire sites: `Texture.cpp:245-295` (`UploadImageData`) and `Buffer.cpp:231-252` (`Create`, kDeviceLocal branch). Between create and destroy, `OneShotCommandBuffer::Execute` runs `CHECK_VK(vkQueueSubmit)`/`CHECK_VK(vkWaitForFences)` (`OneShotCommandBuffer.cpp:46-64`) and `CheckVkFailed` throws `DeviceLostException` on DEVICE_LOST (`GraphicsUtils.cpp:36`) — the leaked allocation survives into teardown. `TextureCache.cpp:31`'s readback staging has the same shape and can adopt the struct [~30m]

### Dead `VkDeviceMemory` plumbing removal
- `Buffer::CreateBuffer`'s `rVkDeviceMemory` out-param exists for "compatibility with existing code that still uses vkMapMemory" (`Buffer.cpp:57` comment) — repo-wide grep finds **zero** `vkMapMemory` calls. The members it feeds are write-only: `Texture::mVkDeviceMemory` (`Texture.h:121`) and `Pipeline::mIndirectVkDeviceMemory` (`Pipeline.h:159`) never read; `Buffer`'s two `*VkDeviceMemory` members read only as `!= VK_NULL_HANDLE` sentinels in `Destroy()` (`Buffer.cpp:258,267`) where the `VkBuffer` handles serve identically. Callers declare throwaway locals: `Islands.cpp:55`, `BufferManager.cpp:579,584,625,628,668`, `TextureCache.cpp:31`, `TextureUploadManager.cpp:61`. Drop the out-param, delete the write-only members, swap Buffer's Destroy sentinels to the handles, thread through `AdoptTransferredImage`'s signature; check the `FileManager.h:66` chunk field for the same write-only status [~1h]

### ModelPipeline trust-boundary value validation
- `ModelPipeline::Create` bounds the *extent* of the on-disk index-start/material arrays (:57) but not the *values*: `mpiFirstIndices.at(i) = puiIndexStarts[i]` (:103-104) and `(next − first)` feed `firstIndex`/`indexCount` of `vkCmdDrawIndexedIndirect` unchecked — non-monotonic or out-of-range starts yield a negative difference cast to huge `uint32_t` → GPU OOB index reads (device-loss risk). Extend the file's existing `CorruptStreamException` checks: reject non-monotonic or `> pVertexBuffer->mInfo.iCount` starts [~15m]

## Critical files
- `Engine/Source/Graphics/Objects/Texture.{h,cpp}`, `Buffer.{h,cpp}`, `Pipeline.h`, `ModelPipeline.cpp`
- `Engine/Source/Graphics/Islands.cpp`, `Managers/BufferManager.cpp`, `Managers/TextureCache.cpp`, `Managers/TextureUploadManager.cpp` (caller signature updates)
- `Engine/Source/File/FileManager.h` (chunk field check)

## Out of scope
- Pipeline registration/descriptor lifecycle (live pipeline-cluster plans)
- The corrupt-chunk upload-thread catch (`CorruptTextureChunkLifecycleHardening` — lands before `BindlessSlotLifecycle`; this plan's leak sites are different)
- Buffer/Texture API redesign beyond the out-param removal

## Notes
- Invariant exposure: none for determinism/CRC/wire — client/graphics-only. The VkDeviceMemory removal is mechanical but ~8-file blast radius; compile-checked. Overlaps pipeline-cluster and TextureUploadManager plan files — schedule around them per Order.md Dependencies
- Grill decision: none — all three items have one clear shape
