# Architecture: Invariant Hardening

## Context

Source: /external-architecture-review on `Engine/Source/Profile`. Several load-bearing contracts are convention-only — comment-enforced, documented in prose, or silently failable.

## Design

### Engine/Source/Profile/ProfileManagerBase.cpp
- After the `"Profile/ProfileManager.h"` include (`:5`), add engine-side `static_assert`s pinning the engine↔game enum contract the engine compiles against by name (`GetCpuTimer(game::kCpuTimerFrameUpdate)` at `:416`; phase brackets in `GameBase.cpp`): `static_assert(game::kCpuTimerFrameUpdate == kEngineCpuTimerCount, ...)` and `static_assert(game::kCpuCounterPlayers == kEngineCpuCounterCount, ...)`. This pins the contiguous index space — the first game enumerator must start at the engine count (`ProfileManager.h:8`, `:22`; currently `kCpuCounterPlayers` / `kCpuTimerFrameUpdate`). Today the contract is documented only in the game-side CLAUDE.md; an omitted enum initializer would restart the game enums at 0 and silently misroute every game index into the engine arrays via `ProfileManager::GetCpuTimer`'s `iIndex < kEngineCpuTimerCount` branch (`ProfileManager.cpp:57`) with no diagnostic. [~5m]
- `Create()` (`:27-66`): document + assert the hidden manager-ordering precondition (`gpInstanceManager` `:38`, `gpSwapchainManager` `:45`, `gpDeviceManager` `:57`, `OneShotCommandBuffer` `:61` — works only because `Graphics::Create()` calls it after SwapchainManager exists, `Graphics.cpp:285` → `:290`): one precondition comment plus `ASSERT(gpSwapchainManager != nullptr)`, inside the existing `BT_CLIENT` span (the managers are client-only and the server body is empty). Asserting the latest-created manager implies the earlier ones (strict creation order Instance → Device → Swapchain). [~5m]

### Engine/Source/Profile/ProfileScreens.cpp
- `FormatCpuTimersText` (`:14-58`) reads `smoothedMicroseconds.Get()` / `iThreads` / `smoothedAllocations.Get()` without `mCpuTimerMutex` while submit workers concurrently write under the lock (`CommandBufferManager.cpp:105/109/170`, run on the `kbRenderThread` submit workers); the sibling `LogTimers` locks for the identical reads (`ProfileManagerBase.cpp:336`). Take the mutex — `mCpuTimerMutex` is `protected` (`ProfileManagerBase.h:337`), so expose a public `std::unique_lock<std::mutex> LockCpuTimers()` accessor (or equivalent) for the free function. Cold path (~2 Hz re-evaluation + per-frame text), no contention concern; no caller holds the mutex on the path into the formatter (both `UpdateProfileText` and the server paint acquire/release it inside earlier calls). [~10m]

## Critical files
- Engine/Source/Profile/ProfileManagerBase.h
- Engine/Source/Profile/ProfileManagerBase.cpp
- Engine/Source/Profile/ProfileScreens.cpp

## Out of scope
- Asserting the `CpuStop` cross-thread not-found case (`ProfileManagerBase.cpp:151-162`) — rejected during verification: the silent skip is load-bearing (see Verification Notes).
- `FormatCpuCountersText` locking — counters are written via `SetCount` on the display thread only; no concurrent writer.
- Replacing the named-enum dependency with a virtual hook (e.g. `TotalFrameTimerIndex()`) — cleaner layering but a larger change; the `static_assert` is the KISS fix.
- The contention model itself (single global mutex for all CPU timers) — documented instead by `Architecture_DocAccuracy.md`.

## Notes
- No determinism/CRC/network/`kiVersion` exposure; the `ASSERT`/`static_assert`s are fail-loud additions on profiling paths only.
- Grill decision (pre-staged): mutex-exposure shape for `FormatCpuTimersText` — public `LockCpuTimers()` accessor (recommended) vs `friend` declaration vs documenting the lock-free read as intentional (de-recommended: formal data race, inconsistent with `LogTimers`).
- Coordinate with `Common/StaleDocClaimsSweep.md` item 6: it rewords the Profile CLAUDE.md "required position of that timer" sentence to name-existence-only; this plan's `static_assert` makes the first-game-timer position compiler-enforced, so whichever lands second should keep the doc and the assert telling the same story.

## Verification Notes
Verified against source (2026-06-10 pass); one item dropped, one rationale corrected:
- **Dropped**: "`CpuStop` cross-thread branch: add `ASSERT(pState != nullptr)`". The claimed "guaranteed prior start" is false on the first frame of every run: the sole cross-thread stop (`CommandBufferManager.cpp:105`, in `SubmitGlobalToQueue`) executes during the first global submit, while the matching `CpuStart(kCpuTimerAcquireToGlobal)` only runs at the *end* of `RenderMainPresentAcquire` (`Graphics.cpp:261`) — i.e. the first stop always precedes the first start, the scan legitimately finds nothing, and the `[[likely]]`-guarded skip (`:179`) absorbs it. `ASSERT` = `DEBUG_BREAK` + throw, and under `kbRenderThread` the throw would forward through the submit `PersistentWorker` — a guaranteed boot crash with profiling enabled.
- **Corrected**: the static_assert item's failure-mode wording — an omitted initializer cannot "underflow" `iIndex - kEngineCpuTimerCount` (the `iIndex < kEngineCpuTimerCount` ternary guard at `ProfileManager.cpp:57` makes the subtraction unreachable for small indices); the real failure is silent misrouting of game indices into the engine arrays. Both asserted equalities verified true today (`ProfileManager.h:8`, `:22`).
- Remaining items verified: `Create()` ordering citations exact (`:38`/`:45`/`:57`/`:61`; called from `Graphics.cpp:290` after swapchain creation at `:285`); `FormatCpuTimersText` lock-free reads (`:24`, `:39`, `:45-48`) vs `LogTimers`' locked reads (`:336`) confirmed; `mCpuTimerMutex` is `protected` (`:337`) so the accessor is required; no deadlock path (no caller holds the mutex across the formatter).
- Caveat: the `Create()` `ASSERT` is a contract assert on an internal boot-order invariant — consistent with existing codebase contract asserts (`PersistentWorker`, `CpuStart`'s start-state `ASSERT`) but bordering the "no defensive validation between our own functions" directive; the grill may keep only the comment.