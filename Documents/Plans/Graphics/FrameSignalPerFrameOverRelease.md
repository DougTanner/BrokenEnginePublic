# Guard the Per-Frame mFrameSignal Release Against binary_semaphore Over-Release

## Context

`Graphics.cpp:273` signals the texture-upload thread once per rendered frame with a **bare** release:

```cpp
// Signal upload thread to process one upload iteration
gpTextureUploadManager->mFrameSignal.release();
```

`mFrameSignal` is a `std::binary_semaphore {0}` (`TextureUploadManager.h`), whose max count is 1. `release()` when the counter is already at its max is a **precondition violation → UB** (`counting_semaphore::release`: `update + counter <= max()`).

The upload thread (`TextureUploadManager::UploadThread`) consumes exactly one permit per iteration at its `mFrameSignal.acquire()`, then runs a full upload iteration that can block for more than one frame interval — notably the final-chunk `vkWaitForFences` GPU wait inside `SubmitChunkUpload`, and the "wait for previous submission" fence at the top of the next iteration. While the thread is inside a >1-frame iteration, the permit posted for the previous frame is still pending (count 1); the next frame's bare `release()` then pushes the count to 2 — over-release UB. This becomes easier to hit at high frame rates (short frame interval) or under GPU/transfer-queue contention.

The codebase already treats this exact hazard as UB and guards **every other** release of `mFrameSignal` with a preceding drain (drain-then-release):

- `TextureUploadManager::WaitIdle` — `std::ignore = mFrameSignal.try_acquire(); mFrameSignal.release();`
- `TextureUploadManager::DestroyTransferResources` — same idiom (landed alongside the teardown-race fix).
- `TextureManager::WaitForTextures` — same idiom, with the comment naming "binary_semaphore double-release UB".

`Graphics.cpp:273` is the lone unguarded release. It was surfaced by the `TextureUploadTeardownRaces` review/session-audit passes as a pre-existing hazard, out of scope for that plan.

## Design

Match the established drain-then-release idiom so the counter is capped at 1 regardless of whether the upload thread has fallen a frame behind. Two shapes to choose between (grill):

- **A (call-site drain-then-release):** at `Graphics.cpp:273`, `std::ignore = gpTextureUploadManager->mFrameSignal.try_acquire(); gpTextureUploadManager->mFrameSignal.release();`. Minimal, matches the three existing sites verbatim, but duplicates the idiom at a fourth site and pokes the raw `mFrameSignal` member from `Graphics`.
- **B (recommended — encapsulate):** add a `TextureUploadManager::SignalFrame()` method that performs the drain-then-release, and call it from `Graphics.cpp:273`. Consolidates the idiom behind the owner's API; the raw `mFrameSignal` member could then stop being public. (Note: `WaitIdle`/`DestroyTransferResources` already own their drains internally, so this only replaces the one external poke.)

Either way the semantic change is: when the upload thread is ≥1 frame behind, a redundant wake is dropped instead of over-releasing. This loses no work — the queue persists and the thread processes on the next frame it catches up; the drop only collapses a duplicate wake the thread did not need.

## Critical files

- `Engine/Source/Graphics/Graphics.cpp` — the per-frame `mFrameSignal.release()` at ~`:273` (in the present path).
- `Engine/Source/Graphics/Managers/TextureUploadManager.h` — `mFrameSignal` member; the optional `SignalFrame()` helper (Option B).

## Out of scope

- The teardown/device-loss drain handshake (`WaitIdle`/`DestroyTransferResources`) — already guarded (landed).
- Any redesign of the per-frame pacing mechanism itself (still one chunk per frame signal); this only caps the release.
- Other semaphores (Vulkan render-submission semaphores are a different primitive/class).

## Acceptance criteria

- No path can call `mFrameSignal.release()` while the counter is already at 1.

## Notes

- Client/graphics-only; teardown-independent steady-state signaling. No determinism/CRC/`kiVersion`/wire exposure.
- Reachability is timing-dependent (needs an upload iteration to outlast a frame interval) — plausible but not measured; the fix is trivial and matches an established idiom regardless.
- Grill: Option A (call-site) vs Option B (`SignalFrame()` helper, recommended).
