# Fix TextureUploadManager WaitIdle Teardown Deadlock and Stray-Permit Race

## Context

The drain-probe handshake added to `TextureUploadManager::WaitIdle` (`Engine/Source/Graphics/Managers/TextureUploadManager.cpp:151-182`) to close the upload thread's acquire→lock TOCTOU window has two verified defects:

1. **Deadlock on device-loss recovery when the upload thread observed the loss.** `UploadThread` exits its loop via the `DeviceLostException` catch's `break` (`:304`) with `mbShutdown` still **false** (only `DestroyTransferResources` sets it). Device-loss recovery then runs `Graphics::Destroy()`, which calls `WaitIdle()` (`Graphics.cpp:595`) *before* `DestroyTransferResources` (`Graphics.cpp:677`). `WaitIdle` sees `mbShutdown == false`, posts a drain probe, and blocks forever on `mIdleConditionVariable.wait(lock, [this]{ return mbDrained; })` — no thread is alive to ack. The pre-change code (bare `std::unique_lock`) returned immediately for a dead thread; this is a regression.

2. **Stray permit re-opens the external-synchronization race one level deeper.** Verified interleaving: the thread consumes its frame permit and is in the acquire→lock window; `WaitIdle` sets the drain flags under `mWorkMutex`, then unconditionally `try_acquire` + `release`s `mFrameSignal` (`:176-177`), leaving one pending permit; the thread takes the lock, acks the probe (`mbDrainRequested = false; mbDrained = true`), `continue`s, and its next `acquire` consumes the *probe's* permit — `mbDrainRequested` is now false, so it dequeues queued work and calls `vkQueueSubmit` on the transfer queue while the main thread (whose wait predicate `mbDrained` was already satisfied) has proceeded into `vkDeviceWaitIdle` (`Graphics.cpp:600`). That is exactly the Vulkan external-synchronization violation the handshake was built to close. Needs queued uploads at teardown time — plausible during device loss.

## Design

- **Exit-ack:** on both `UploadThread` loop exits (the `mbShutdown` break and the `DeviceLostException` break), set an `mbThreadExited` flag under `mWorkMutex` and `notify_all`; widen `WaitIdle`'s wait predicate to `mbDrained || mbThreadExited` and early-return when the thread has already exited (alongside the existing `mbShutdown` early-return).
- **Permit hygiene:** in the drain-ack branch of `UploadThread` (`:204-209`), swallow any pending probe permit (`std::ignore = mFrameSignal.try_acquire();`) before `continue`. Probes exist only during teardown; a swallowed *frame* signal re-posts next frame by design (the main thread signals once per frame).

## Critical files

- `Engine/Source/Graphics/Managers/TextureUploadManager.h`, `TextureUploadManager.cpp` — `WaitIdle`, `UploadThread`
- `Engine/Source/Graphics/Graphics.cpp` — `Destroy` call order (read-only context)

## Out of scope

- The corrupt-chunk soft-fail lifecycle (companion plan `CorruptTextureChunkLifecycleHardening.md`).
- Redesigning the handshake (e.g. condition-variable-only) — fix the two holes, keep the shape.

## Acceptance criteria

- Device-loss recovery completes when the upload thread exited via its own `DeviceLostException` catch (previously: hang in `WaitIdle`).
- After `WaitIdle` returns, no path lets `UploadThread` submit before the next frame signal.

## Notes

- Client/graphics-only; teardown/device-loss threading. No determinism/CRC/`kiVersion`/wire exposure.
- Co-schedule with `CorruptTextureChunkLifecycleHardening.md` (same file); never interleave.
- No grill decisions beyond the Design as written.
