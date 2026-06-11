# Architecture: Invariant Hardening (comments + asserts)

## Context

Source: /external-architecture-review on `Engine/Source/Graphics` (non-recursive). Two invariants in this
directory hold today only by convention established elsewhere; each gets a one-line guard (comment or
assert) at the site where a future change would silently break it.

## Design

### Engine/Source/Graphics/AnimationData.h
- Add a guard comment at the `gAnimationDataMap` declaration (`AnimationData.h:41`):
  `mpAnimations[...].fDuration` feeds the per-entity animation clock in *sim-phase* code
  (`Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp:549-558`,
  `.../Frame/Collections/Spaceships/Spaceships.cpp:307-313`, both inside `BT_CLIENT` guards). That clock is
  deliberately excluded from `SharedCrcMembers` (documented in `Players/CLAUDE.md`) — warn that promoting
  the animation-time field into the shared CRC would couple determinism to client-only pack data. [~5m]

### Engine/Source/Graphics/OneShotCommandBuffer.cpp
- Pin the single-thread contract with an assert: `OneShotCommandBuffer` shares
  `gpDeviceManager->mOneShotVkCommandPool` and `mOneShotVkFence` (`OneShotCommandBuffer.cpp:12,41,56,60`),
  documented "single-threaded, graphics queue only" at `Managers/DeviceManager.cpp:217`. Queue/pool safety
  versus the `kbRenderThread` submit workers currently holds only through worker drains (the frame-tail
  present drain `Graphics.cpp:251-256` and `SubmitUiCommandBuffer`'s `mSubmitMain.Wait()`,
  `CommandBufferManager.cpp:205`) — the kind of invariant that breaks silently if submit scheduling changes.
  Add a thread-identity `ASSERT` in `Execute()` (mirror an existing main-thread assert mechanism if one
  exists; otherwise capture the owning `std::thread::id` at first use of the shared pool). Caveat: a strict
  "always the boot thread" assert would fire on the dormant screenshot path — `SaveScreenshot` →
  `TextureCache::CopyImageToHostMemory` constructs a `OneShotCommandBuffer` (`TextureCache.cpp:42`) on the
  `mSubmitMain` worker when `kbRenderThread && kbScreenshots` (safe today only because the main thread is
  parked in `mSubmitMain.Wait()`). The real contract is non-concurrent use; either scope the assert to that
  (e.g. an in-use flag set in the ctor, cleared in the dtor, `ASSERT` on re-entry), or land it alongside
  `Graphics/Architecture_ScreenshotFenceDeadlock.md`'s fix shape (c), which moves the screenshot capture to
  the main thread. [~10m]

## Critical files
- `Engine/Source/Graphics/AnimationData.h`
- `Engine/Source/Graphics/OneShotCommandBuffer.cpp`

## Out of scope
- The `SaveScreenshot` fence-wait precondition — documented as part of
  `Graphics/Architecture_ScreenshotFenceDeadlock.md`.
- The `gAnimationDataMap` boot-order happens-before (populated on FileManager's async parse thread) — the
  hazard disappears when `File/Architecture_AnimationDataLoadPlacement.md` moves the parse to a post-drain
  Graphics-side consumer; not duplicated here.
- Removing `Execute`'s dead `bWait` parameter — `Graphics/Refactor_ApiAndHotPathCleanups.md`.

## Notes
- Comment + debug-`ASSERT` only; zero behavior change, no determinism/CRC/network exposure. No grill
  decisions.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source.
- Item 1: `gAnimationDataMap` declared `AnimationData.h:41`; sim-phase reads at
  `Frame/Collections/Players/Players.cpp:549-558` (inside the `BT_CLIENT` span `:533-634`) and
  `Frame/Collections/Spaceships/Spaceships.cpp:307-313` (its own `#if defined(BT_CLIENT)` block);
  `Players/CLAUDE.md` "Shared-CRC exclusions" documents the animation clock's deliberate omission from
  `SharedCrcMembers`. Paths made explicit (the collections live under `Frame/Collections/`, not
  `Players/`/`Spaceships/` directly).
- Item 2: pool/fence shares at `OneShotCommandBuffer.cpp:12,41,56,60` (dtor also frees from the pool,
  `:32`); "single-threaded, graphics queue only" comment at `DeviceManager.cpp:217`; `kbRenderThread`
  drains confirmed (`Graphics.cpp:251-256` present drain; `CommandBufferManager.cpp:205` submit drain —
  the latter added as a citation). Caveat added: the screenshot path constructs a `OneShotCommandBuffer` on
  the `mSubmitMain` worker (`TextureCache.cpp:42` inside `SaveScreenshot`'s copy), so the assert must
  encode non-concurrency (or main-thread-ness only after the screenshot fix moves capture to the main
  thread), not a hard-wired boot-thread identity.
- Out-of-scope cross-references exist: `File/Architecture_AnimationDataLoadPlacement.md`,
  `Graphics/Refactor_ApiAndHotPathCleanups.md`, `Graphics/Architecture_ScreenshotFenceDeadlock.md`.
