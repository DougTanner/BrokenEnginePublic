# Architecture: Screenshot Fence Deadlock + Allocation Discipline

## Context

Source: /external-architecture-review on `Engine/Source/Graphics` (non-recursive). `SaveScreenshot` waits a
fence that cannot signal until after the waiter returns — a latent deadlock-then-timeout, currently masked
only because `kbScreenshots = false` (game `Pch.h:15`).

The chain (verify each link at execution before editing, per Diagnosis Discipline):
1. `CommandBufferManager::SubmitMainToQueue` **resets** the per-framebuffer fence
   (`Managers/CommandBufferManager.cpp:168`) and submits Main with `VK_NULL_HANDLE` — the fence is signaled
   only by the *subsequent UI submit* (pairing documented at `CommandBufferManager.cpp:163-167`).
2. It then calls `SaveScreenshot(iFramebufferIndex)` (`CommandBufferManager.cpp:177-184`), which immediately
   waits that just-reset fence (`Screenshot.cpp:13-14`).
3. With `kbRenderThread = true` (game `Pch.h:9`), `SubmitMainToQueue` runs on the `mSubmitMain` worker while
   the main thread blocks on `mSubmitMain.Wait()` (`CommandBufferManager.cpp:205`) *before* the UI submit
   that would signal the fence. Worker waits fence ← fence waits UI submit ← UI submit waits worker:
   deadlock until `kFenceTimeoutNanoseconds`, then `CHECK_VK(VK_TIMEOUT)` → `DEBUG_BREAK` + throw. With
   `kbRenderThread = false` it times out the same way (inline wait before the UI submit can run).

Separately, `SaveScreenshot`'s synchronous (pre-async) portion heap-allocates in the main loop with no
suppression: `std::vector<std::byte> data` (`Screenshot.cpp:19`, resized inside
`TextureCache::CopyImageToHostMemory` at `TextureCache.cpp:33`), the `std::async` shared state (`:30`), and
the previous future's teardown (`:26-29`) — all would trip the allocation tracker the moment `kbScreenshots`
is enabled. Suppression is thread-local (`Memory/CLAUDE.md`) while tracking is global, so this trips
regardless of which thread executes it: the `mSubmitMain` worker under `kbRenderThread = true`, the main
thread otherwise. (The conversions inside the async lambda run on the screenshot thread with its own
`ThreadLocal` and are not the concern.)

## Design

### Engine/Source/Graphics/Screenshot.cpp + Engine/Source/Graphics/Managers/CommandBufferManager.cpp
- Fix the wait ordering so the capture never waits a fence whose signal depends on the caller returning.
  Three candidate shapes (grill decision):
  - **(a) Capture before the reset** — move the `SaveScreenshot` call (or just its fence wait) ahead of the
    `vkResetFences` at `CommandBufferManager.cpp:168`, waiting the fence while it still holds the *previous*
    use's signal for that framebuffer image. Caveat: that signal belongs to the frame that last used this
    swapchain image — the capture is one full swapchain cycle stale (~2-3 displayed frames old with triple
    buffering), not the frame whose submit triggered it.
  - **(b) Fence the Main submit on screenshot frames** — on frames where a screenshot is requested, pass the
    framebuffer fence to the Main `vkQueueSubmit` instead of `VK_NULL_HANDLE` (the `bSignalFence` parameter
    already plumbs this) and keep the wait where it is; the UI-submit signal pairing must then skip those
    frames. Captures the current frame's Main render but *without* the ImGui overlay.
  - **(c) Capture after the UI submit, on the main thread** — move the `SaveScreenshot` call out of
    `SubmitMainToQueue` into `Graphics::RenderMainPresentAcquire` between `SubmitUiCommandBuffer`
    (`Graphics.cpp:239`) and `Present` (`Graphics.cpp:241`). At that point the UI submit has been enqueued
    with the fence, so the existing wait at `Screenshot.cpp:13-14` blocks until the GPU finishes the full
    frame (including UI) while the image is still application-owned pre-present — the only shape that
    preserves the original capture intent. Costs a full GPU sync on screenshot frames (fine for a dev
    toggle); also keeps the capture (and its internal `OneShotCommandBuffer` graphics-queue submit,
    `TextureCache.cpp:42`) on the main thread.
  Recommendation: (c) — correct pixels, no change to the submit/signal protocol, no cross-thread one-shot
  submit. [~1h]
- Document the fence precondition at the wait site (`Screenshot.cpp:13-14`): the fence must have a pending
  or completed signal for the content being captured, never a fresh reset whose signal depends on the
  caller returning. [~5m]

### Engine/Source/Graphics/Screenshot.cpp (allocation discipline)
- Wrap the synchronous portion of `SaveScreenshot` (the `data` vector, the previous-future drain, the
  `std::async` launch) in `ScopedSuppressAllocationTracking` with a `// Heap:` justification comment —
  dev-only feature, the pixel buffer must outlive the function into the async thread, so the workbuffer
  cannot hold it. The suppression counter is thread-local, so placing the scope inside `SaveScreenshot`
  covers whichever thread ends up executing it under the chosen fix shape. [~10m]

## Critical files
- `Engine/Source/Graphics/Managers/CommandBufferManager.cpp` (`SubmitMainToQueue`, the fence reset/signal
  pairing, the `mSubmitMain` worker dispatch)
- `Engine/Source/Graphics/Screenshot.cpp`

## Out of scope
- Any change to the fence/submit protocol on non-screenshot frames.
- The `OneShotCommandBuffer` thread contract (`Graphics/Architecture_InvariantHardening.md`) and its dead
  `bWait` parameter (`Graphics/Refactor_ApiAndHotPathCleanups.md`).
- `GetTempPath` ANSI→W conversion in the async saver — same class as the items in
  `File/Refactor_ClientGuardAndWideApi.md`; fold there if wanted, not here.
- Making screenshots a shipped feature — `kbScreenshots` stays a dev toggle.

## Notes
- Client-only, render-submission path; no determinism/CRC, replay, or network exposure. The fix must be
  validated with `kbScreenshots = true` under both `kbRenderThread` settings.
- Root cause was established by code inspection of the reset/signal/wait ordering; the executor should
  re-confirm the three numbered links (especially the UI-submit-signals-the-fence pairing) before editing.
- One grill decision: fix shape (a) vs (b) vs (c) above.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source; the deadlock chain holds.
- Link 1: `vkResetFences` at `CommandBufferManager.cpp:168`; Main `vkQueueSubmit` at `:169` with
  `bSignalFence ? fence : VK_NULL_HANDLE` and the sole caller passes `false` (`Graphics.cpp:237`);
  reset/signal pairing comment at `:163-167`; the UI submit signals the fence at `ImGuiManager.cpp:376`.
- Link 2: `SaveScreenshot(iFramebufferIndex)` inside the `kbScreenshots` block at
  `CommandBufferManager.cpp:177-184`; the wait on the just-reset fence at `Screenshot.cpp:13-14`.
- Link 3: `kbRenderThread = true` (game `Pch.h:9`), `kbScreenshots = false` (game `Pch.h:15`);
  `SubmitMainCommandBuffer` wakes the `mSubmitMain` worker (`CommandBufferManager.cpp:189-196`);
  `SubmitUiCommandBuffer` blocks on `mSubmitMain.Wait()` (`:205`) before the UI submit — worker waits fence
  ← fence waits UI submit ← UI submit waits worker. `CHECK_VK` treats any non-`VK_SUCCESS` (including
  `VK_TIMEOUT`) as failure → `CheckVkFailed` → `DEBUG_BREAK` + throw (`GraphicsUtils.h:46`,
  `GraphicsUtils.cpp:40-41`).
- Allocation claims: `data` vector `Screenshot.cpp:19` resized in `TextureCache.cpp:33`; future drain
  `:26-29`; `std::async` `:30`. Corrected "main-thread side" wording — under `kbRenderThread = true` the
  synchronous portion runs on the `mSubmitMain` worker; tracking is global, suppression thread-local
  (`Memory/CLAUDE.md`), so the tracker trips either way and the in-function suppression scope fixes both.
- Corrections made: Design heading pointed at `CommandBufferRecordMain.cpp` — the fence logic lives in
  `CommandBufferManager.cpp`. Option (a)'s claim that the pre-reset signal is "exactly the present whose
  pixels are being captured" was wrong — it is the framebuffer image's *previous* cycle; caveat added.
  Added shape (c) (main-thread capture between UI submit and Present), which is the only shape preserving
  the original UI-inclusive capture intent, and switched the recommendation to it.
- New finding folded in: `CopyImageToHostMemory` internally constructs a `OneShotCommandBuffer`
  (`TextureCache.cpp:42`), so under (a)/(b) the one-shot graphics-queue submit happens on the `mSubmitMain`
  worker, serialized only by the main thread being parked in `mSubmitMain.Wait()` — see the thread-contract
  assert in `Graphics/Architecture_InvariantHardening.md`; shape (c) sidesteps this entirely.
