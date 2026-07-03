# Refactor: Staging Buffer RAII & Dead VkDeviceMemory Plumbing

## Context
Source: /external-refactor-clean on Engine/Source (recursive). The Graphics/Objects correctness cluster: staging buffers leak across `DeviceLostException` on a *recoverable* path (device-loss recovery recreates Graphics in place, so the leak trips the VMA unfreed-allocation assert during recovery — the same failure class `CorruptTextureChunkLifecycleHardening` documents for a different site, not covered by any live plan), plus engine-wide write-only `VkDeviceMemory` plumbing rooted in a stale comment, and an unvalidated on-disk array driving GPU indirect draws.

## Design

### RAII staging buffer (headline)
- Add a ~15-line RAII staging-buffer struct (create in ctor, `vmaDestroyBuffer` in dtor) and adopt at both manual-acquire sites: `Texture.cpp:245-295` (`UploadImageData`) and `Buffer.cpp:228-253` (`Create`, kDeviceLocal branch). Between create and destroy, `OneShotCommandBuffer::Execute` runs `CHECK_VK(vkQueueSubmit)`/`CHECK_VK(vkWaitForFences)` (`OneShotCommandBuffer.cpp:46-64`) and `CheckVkFailed` throws `DeviceLostException` on DEVICE_LOST (`GraphicsUtils.cpp:36`) — the leaked allocation survives into teardown. The fill callback (`rDataFunction`, run between create and Execute) is a second throw window with the same leak. `TextureCache.cpp:30-34`'s readback staging has the same shape and can adopt the struct [~30m]

### Dead `VkDeviceMemory` plumbing removal
- `Buffer::CreateBuffer`'s `rVkDeviceMemory` out-param exists for "compatibility with existing code that still uses vkMapMemory" (`Buffer.cpp:57` comment) — repo-wide grep finds **zero** first-party `vkMapMemory` calls (only the vendored ImGui backend). The members it feeds are write-only: `Texture::mVkDeviceMemory` (`Texture.h:121`) and `Pipeline::mIndirectVkDeviceMemory` (`Pipeline.h:159`, also nulled at `Pipeline.cpp:231`) never read; `Buffer`'s two `*VkDeviceMemory` members read only as `!= VK_NULL_HANDLE` sentinels in `Destroy()` (`Buffer.cpp:258,267`) where the `VkBuffer` handles serve identically (set/nulled in lockstep). Callers pass throwaway locals or the write-only members: `Islands.cpp:55-56`, `BufferManager.cpp:580,585,626,629,669`, `TextureCache.cpp:30-34`, `TextureUploadManager.cpp:61-62`, `PipelineCreator.cpp:102,126,689,710`. Drop the out-param, delete the write-only members, swap Buffer's Destroy sentinels to the handles, thread through `AdoptTransferredImage`'s signature (`Texture.h:101`, `Texture.cpp:130-136`). The `LazyChunk::vkDeviceMemory` field (`FileManager.h:66`) is the same write-only chain — written `TextureUploadManager.cpp:424`, nulled `:118`/`FileManager.cpp:776`, consumed only into the write-only `Texture::mVkDeviceMemory` at `TextureManager.cpp:568` — delete it too [~1h]

### ModelPipeline trust-boundary value validation
- `ModelPipeline::Create` bounds the *extent* of the on-disk index-start/material arrays (:57) but not the *values*: `mpiFirstIndices.at(i) = puiIndexStarts[i]` (:103-104) and `(next − first)` feed `firstIndex`/`indexCount` of `vkCmdDrawIndexedIndirect` unchecked — non-monotonic or out-of-range starts yield a negative difference cast to huge `uint32_t` → GPU OOB index reads (device-loss risk). Extend the file's existing `CorruptStreamException` checks: reject non-monotonic or `> pVertexBuffer->mInfo.iCount` starts [~15m]

## Critical files
- `Engine/Source/Graphics/Objects/Texture.{h,cpp}`, `Buffer.{h,cpp}`, `Pipeline.{h,cpp}`, `PipelineCreator.cpp`, `ModelPipeline.cpp`
- `Engine/Source/Graphics/Islands.cpp`, `Managers/BufferManager.cpp`, `Managers/TextureCache.cpp`, `Managers/TextureUploadManager.cpp`, `Managers/TextureManager.cpp` (caller signature updates)
- `Engine/Source/File/FileManager.{h,cpp}` (dead `LazyChunk::vkDeviceMemory` field)

## Out of scope
- Pipeline registration/descriptor lifecycle (live pipeline-cluster plans)
- The corrupt-chunk upload-thread catch (`CorruptTextureChunkLifecycleHardening` — lands before `BindlessSlotLifecycle`; this plan's leak sites are different)
- Buffer/Texture API redesign beyond the out-param removal

## Notes
- Invariant exposure: none for determinism/CRC/wire — client/graphics-only. The VkDeviceMemory removal is mechanical but ~10-file blast radius; compile-checked. Overlaps pipeline-cluster and TextureUploadManager plan files — schedule around them per Order.md Dependencies
- Grill decision: none — all three items have one clear shape

## Verification Notes
- All three claims re-verified against source (2026-07-02): the leak-on-throw shape, the zero-`vkMapMemory` grep, the write-only member set, and the unvalidated `puiIndexStarts` values all hold; line cites refreshed.
- **Pipeline-cluster sequencing**: the out-param removal touches `Pipeline.{h,cpp}` and `PipelineCreator.cpp`, which sit in the pipeline-cluster File Group (`Architecture_PipelineRegistrationOwnership`, `PipelineDescriptorInfosRightSize`, `WindowedLightingShadowDispatch`). This plan does not overlap their *scope* (they don't touch the `VkDeviceMemory` members or staging paths), but never interleave with `BindlessSlotLifecycle` and refresh citations if any of those land first.
- No conflict with allocation tracking: the RAII struct is stack-only; the `TextureCache` readback path already runs under `ScopedSuppressAllocationTracking` (`TextureCache.cpp:15`). No conflict with device-loss recovery: the dtor's `vmaDestroyBuffer` is exactly the cleanup that path needs — destroying resources after `VK_ERROR_DEVICE_LOST` is valid, and freeing the allocation prevents the VMA unfreed-allocation assert when `DeviceManager` tears down the allocator (`DeviceManager.cpp:332`) during in-place Graphics recreation.
