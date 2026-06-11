# Architecture: Thread & Lifetime Guards (Graphics/Managers)

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Managers` (non-recursive). The directory's
thread model is sound overall (submission chain serialization verified end-to-end; upload-thread handshake
release/acquire-correct), but three spots rest on implicit invariants that are undocumented or thinner than
their siblings, and one record-time setting needs its destroy-tier coverage confirmed.

## Design

### Engine/Source/Graphics/Managers/TextureDescriptors.cpp — unsynchronized `CrcToIndex`
- `TextureDescriptors::CrcToIndex` mutates `mImageInfosMap` / `miNextTextureIndex` with no lock
  (`TextureDescriptors.cpp:397-413`). It is reachable from worker threads during parallel sim dispatch via
  `ParticleManager::Spawn` → `GetOrAssignTextureIndex` (`ParticleManager.cpp:29,48` → `:24`), serialized only
  against *other Spawns* by `mSpawnMutex`, while the main thread calls it un-mutexed via
  `UpdateDescriptorsForTexture` (`TextureDescriptors.cpp:302`) and `BlurLightingTexture`
  (`TextureManager.cpp:909`). Safety today rests on the undocumented invariant that parallel sim dispatch
  never overlaps the RenderGlobal/render-layout phases. Fix (grill decision): a dedicated mutex inside
  `CrcToIndex`, or document the phase-exclusion invariant + ASSERT main-thread/phase at the un-mutexed
  call sites. [~30m]

### Engine/Source/Graphics/Managers/BufferManager.cpp — single-slot deferred destruction (verdict: safe, undocumented)
- `ResizeDynamicBuffer` (`BufferManager.cpp:423-440`) holds one `mPreviousBuffer` stash; a second same-frame
  resize destroys the first old buffer immediately. **Investigated this run — NOT a live bug**, protected by a
  three-point chain: (1) every dynamic buffer is a per-framebuffer instance (`:405-418`), so only framebuffer
  *i*'s command buffer ever referenced the `(crc, i)` buffer; (2) all resize callers run after `RenderGlobal`'s
  top-of-frame fence wait for framebuffer *i* (`Graphics.cpp:169-174`), so the prior submission completed;
  (3) callers immediately rewrite the per-framebuffer descriptor set (`BillboardsRender.cpp:36`,
  `PlayersRender.cpp:82-83`) and record-once CBs reference buffers only through descriptor sets. Remaining
  work: the protection is invisible at the destruction site — document the three-point chain in a comment at
  `BufferManager.cpp:425`, or mirror the per-framebuffer stash arrays the skinning path uses
  (`BufferManager.h:139-140`) if comment-only feels too thin (grill decision). [~15m]

### Engine/Source/Graphics/Managers/TextureUploadManager.cpp — `WaitIdle` TOCTOU window
- `WaitIdle` (`TextureUploadManager.cpp:142-145`) is an empty `unique_lock(mWorkMutex)` body, but
  `UploadThread` consumes `mFrameSignal` at `:156` *before* taking `mWorkMutex` at `:159`. If
  `Graphics::Destroy` calls `WaitIdle()` while the upload thread sits between those two lines, `WaitIdle`
  acquires an uncontended mutex and returns, and the upload thread then records and `vkQueueSubmit`s on the
  transfer queue (`:397`) concurrently with `vkDeviceWaitIdle` and teardown — a Vulkan external-synchronization
  violation. Window is one instruction-span wide (very low practical hit rate) but structurally present. Fix:
  mark the iteration active under `mWorkMutex` immediately after the signal acquire (cleared before
  re-parking), and have `WaitIdle` drain `mFrameSignal` via `try_acquire` then wait until the thread is parked.
  [~30m]

### Engine/Source/Graphics/Managers/SwapchainManager.cpp — undocumented load-bearing Wait
- The present worker writes `gpGraphics->meDestroyType` (`SwapchainManager.cpp:481`) and the only thing
  sequencing that write before the main thread's `Refresh()` read-modify-write is the `mPresent.Wait()` at
  `Engine/Source/Graphics/Graphics.cpp:251-256`. Comment-only change: at the `:481` write site, name the Wait
  it depends on (and that the same Wait prevents next-frame's Global submit racing `vkQueuePresentKHR` on a
  shared queue), so a future refactor of the frame tail doesn't silently break it. [~5m]

### Destroy-tier coverage for `gDebugTexture` (verify)
- Record-once command buffers bake mutable settings at record time; the escalation ladder in
  `Graphics.cpp:356-520` must cover each baked wrapper. Verified covered: `gSpreadPassCount`
  (`CommandBufferRecordMain.cpp:345`), `gMultisampling` (`:213`), `gWaterShapeDetail` (`:188`). Unconfirmed:
  `gDebugTexture` (`CommandBufferRecordMain.cpp:230-236` — the per-record check only matters on re-record).
  Confirm toggling `gDebugTexture` escalates at least the `kCommandBuffers` destroy tier; if it does not, add
  it to the ladder (or document why stale-until-next-recreate is acceptable for a debug-only view). [~15m]

## Critical files
- `Engine/Source/Graphics/Managers/TextureDescriptors.cpp` (+ `.h` if a mutex member lands)
- `Engine/Source/Graphics/Managers/BufferManager.cpp`, `BufferManager.h`
- `Engine/Source/Graphics/Managers/TextureUploadManager.cpp` (+ `.h` if the parked-flag member lands)
- `Engine/Source/Graphics/Managers/SwapchainManager.cpp` (comment only)
- `Engine/Source/Graphics/Graphics.cpp` (read-only context; destroy-tier ladder if `gDebugTexture` needs adding)

## Out of scope
- Restructuring the submission chain or present worker — verified correct; only the documentation gap is
  addressed.
- The bindless mint/evict/restore consolidation — `Architecture_BindlessSlotLifecycle.md`.
- The upload-thread handshake — verified correct (`TextureUploadManager.cpp:404` release /
  `TextureManager.cpp:618,653-658` acquire), no change.

## Acceptance criteria
- A recorded verdict for the `CrcToIndex` overlap reachability, with either the guard landed or the invariant
  documented + ASSERTed.
- The `ResizeDynamicBuffer` protection chain is documented at the destruction site (or the per-framebuffer
  stash lands); the `WaitIdle` window is closed.
- `gDebugTexture` destroy-tier coverage confirmed or fixed.

## Notes
- No determinism/CRC exposure — all client render-side. Risk concentrates in adding a mutex on a path touched
  by parallel `Spawn` (keep the critical section to the map/counter mutation only) and in the upload-thread
  park handshake (keep `UploadThread`'s steady-state path lock-shape unchanged).
- Two grill decisions staged: mutex-vs-ASSERT for `CrcToIndex`; comment-vs-stash for `ResizeDynamicBuffer`.
