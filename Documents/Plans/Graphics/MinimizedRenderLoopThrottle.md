# Minimized Render-Loop Throttle

## Summary

**What this plan does:** Adds a bounded wait to the swapchain-recreate-deferred skip branch in `GameBase::Render` (`Engine/Source/GameBase.cpp:502-513`) so a minimized (or fully off-screen) client yields the core between recreate retries instead of free-running the main loop. The wait applies only while the recreate stays deferred; the restored/normal render path is untouched and stays vsync-throttled by `vkQueuePresentKHR`.

**Why it's good for the codebase:** `vkQueuePresentKHR` is the client loop's only frame-rate throttle, and the landed minimized-wedge fix skips past it — a minimized client currently busy-spins ~100% of one core for the entire minimized duration and issues one `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` driver round-trip per spin (via `Graphics::Create()` → `SurfaceExtentZeroArea`, `Graphics.cpp:317`). The throttle caps both costs in one place without regressing restore latency (~271 ms measured), agent `Drain` responsiveness, or network/sim liveness.

## Context

- Source: `Documents/Plans/Graphics/MinimizedRenderLoopThrottle.md` (claimed; removed with its Order.md row after execution completes)
- Order.md row: Tier Small / Effort 2 / Impact 2 / Risks 2 / Score 2 (marked `[CLAIMED]`)
- Notes: The `GameBase::Render` deferred/minimized skip early-returns past `vkQueuePresentKHR` (the client loop's only vsync throttle), so a minimized client busy-spins ~100% of one core and re-issues a `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` per iteration via `Graphics::Create()`. Throttle the still-deferred skip branch (A fixed wait mirroring the server `WaitForTick` waitable-timer precedent sans precision spin, rec — vs B loop-top `MsgWaitForMultipleObjects`, vs C retry-cadence gate) without regressing agent `Drain` latency, network/sim liveness (`ClientUpdate` runs pre-skip), or restore latency (≤ few hundred ms; ~271 ms today). Recreate-retry correctness untouched. Client-only, no CRC/wire/`kiVersion`
- Relevance: Fully — the skip branch exists exactly at the cited lines with no throttle in it; the busy-spin problem is live in current source
- Dependency resolution: none (File-Group coordination only: `GameBase.{h,cpp}`/`Main.cpp` shared with `Engine/Architecture_GameBaseDeadVirtuals.md`, `Engine/Refactor_RootFilesQuickWins.md`, `Network/ServerPauseAndResetSemantics.md` — distinct region, line-drift only)
- Changes since the plan was written:
  - `Graphics::Create()` moved :324 → `Graphics.cpp:328`; `SurfaceExtentZeroArea` moved :313 → `Graphics.cpp:317`. All other cites current (`GameBase.cpp:502-513`; `Main.cpp` :341/:346/:357/:371/:381/:389; `ServerSessionBase.cpp:26-59`).
  - The plan's open verify-item on `timeBeginPeriod` resolves: engine code calls `timeBeginPeriod(1)` only in the server-only `ServerSessionBase` ctor (`ServerSessionBase.cpp:16`), **but** ENet calls `timeBeginPeriod(1)` at init (`ThirdParty/enet/win32.c:32`), which the client build also runs — so client `Sleep` granularity is incidentally 1 ms today. Relying on a ThirdParty side effect is fragile; the `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` waitable-timer form is self-contained and remains the recommended sub-choice.

## Design

**Decided (grill, 2026-07-10): Option A — waitable-timer remainder wait, tick-matched.** A high-resolution waitable timer (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, mirroring the server `WaitForTick` precedent minus the precision spin) in the still-deferred skip branch, sleeping the *remainder* of one sim tick (`game::kTickNs` minus elapsed time since the previous skip-branch iteration, floor ~1 ms) so the minimized loop holds ~32 Hz — at, not below, tick rate. Rationale: `Network/JitterMeasurementLowFpsSkew.md` documents that the client jitter estimator over-reports when the loop runs *below* tick rate (acks lag broadcasts → inflated `mSmoothedJitterUs` → unwarranted sim latency); a flat `wait(kTickNs)` on top of loop work would park the minimized client slightly below 32 Hz for the whole minimized duration. The remainder form holds cadence at tick rate, keeps acks pacing broadcasts, and still collapses the busy-spin (~thousands of iterations/sec → 32).

Add a bounded wait to the skip path so the minimized loop yields the core between retries instead of spinning.

**Engine precedent — the server's tick wait.** The headless server has no vsync either and paces its 32 Hz loop with `ServerSessionBase::WaitForTick` (`Engine/Source/Network/Server/ServerSessionBase.cpp:26-59`): a high-resolution waitable timer (`CreateWaitableTimerExW` + `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, `timeBeginPeriod(1)` in the ctor, `SetWaitableTimerEx` + `WaitForSingleObject`) sleeps the bulk of the interval, then a ~500 µs `YieldProcessor` spin closes the gap for sub-ms tick precision. The minimized-client throttle should mirror this mechanism *minus the precision spin* (nothing minimized needs sub-ms accuracy — the spin margin and overshoot verification are tick-determinism machinery, not needed here). Note the client cannot call the symbol directly: `ServerSessionBase` is class-level `BT_SERVER` (`ServerSessionBase.h:8`) and `Network/Refactor_SessionBaseCollapse.md` deletes it — mirror the waitable-timer pattern at the client site (or a plain `Sleep`, see A), do not extract a shared helper through that seam.

Candidate mechanisms:

**Option A — fixed wait in the skip branch (recommended starting point).** After `gpGraphics->Create()` returns still-deferred (`mbSwapchainRecreateDeferred` set) in `GameBase::Render`'s early-return branch, wait a small fixed interval (e.g. ~16-33 ms) before `return` — either a waitable timer mirroring the server precedent above, or plain `Sleep(ms)` (1 ms scheduler granularity exists today only via ENet's `timeBeginPeriod(1)` at init — a ThirdParty side effect; the waitable-timer form self-carries `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` and does not depend on it, a point in its favor). Simplest; caps both the core spin and the `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` cadence in one place. Cost: adds up to that interval of latency to restore detection, and — critically — the wait sits *below* `ProcessMessages`/agent `Drain` in the loop, so it delays the next `Drain` by the interval too. Keep it at a few tens of ms so agent commands to a minimized client stay responsive and restore resumes within a few hundred ms.

**Option B — `MsgWaitForMultipleObjects` / `MsgWaitForMultipleObjectsEx` wait.** Block until a window message arrives or a timeout elapses, so a restore (`WM_SIZE`/`WM_ACTIVATE`) wakes the loop immediately rather than after a fixed sleep. Better restore latency than A, but the wait must carry a timeout so the recreate-retry and agent-`Drain` cadence still tick even absent messages (a minimized-but-restorable extent can appear without a fresh message), and the natural home is the top of the loop / `ProcessMessages`, not inside `GameBase::Render` — larger blast radius into `Main.cpp`.

**Option C — capped retry cadence (throttle only the `Create()` retry, not the loop).** Keep the loop free but only re-issue `gpGraphics->Create()` every N ms (timestamp-gated), `return`-ing cheaply in between. Cuts the driver round-trip cost but does *not* stop the core busy-spin — insufficient alone; only meaningful combined with A/B.

Recommendation: Option A as the baseline (smallest change, single site, immediately caps both spin and driver-query cost); escalate to B only if measured restore latency under A is unacceptable. Either way the wait/sleep applies **only** on the still-deferred path (recreate has not proceeded) — once `!mbSwapchainRecreateDeferred`, the branch acquires and the normal vsync-throttled render resumes, so no wait is added to the restored/normal path.

Liveness constraints the mechanism must preserve:
- **Network / sim liveness.** `ClientUpdate()` runs *before* `Render()` every iteration (`Main.cpp:381` then `:389`) and is not gated by the skip — the client sim keeps consuming server updates while minimized. A per-iteration sleep in `Render`'s skip branch throttles the whole loop including `ClientUpdate`; keep the interval short enough that server-update draining / clock-servo does not starve or fall behind while minimized. (Confirm during the grill whether the client sim tolerates a ~30 ms/iteration loop cadence, or whether the throttle must live only around the render retry and leave `ClientUpdate` full-rate — an argument for gating the retry cadence rather than sleeping the loop.)
- **Agent `Drain` responsiveness.** `gpAgentCommandServer->Drain()` runs at loop top (`Main.cpp:357`); it is how a harness talks to a *minimized* client. A per-iteration sleep below `Drain` delays the next `Drain` by the interval — keep it small (tens of ms) so harness commands to a minimized client are not noticeably laggy.
- **Restore latency.** Restore currently resumes in ~271 ms measured (wedge session). The throttle must not push restore-to-first-frame noticeably past that — bound the wait interval accordingly, or wake on window messages (Option B).
- **Recreate-retry correctness unchanged.** The `Graphics::Create()` defer/retry logic (pre- and post-`Destroy` zero-area gates, `mbSwapchainRecreateDeferred` latch) is untouched — this plan only changes *how often* the still-deferred branch loops, never the retry semantics or the proceeded/acquire test.

## Execution steps

1. Implement the decided throttle in the `GameBase::Render` skip branch, `Engine/Source/GameBase.cpp:502-513` — after `gpGraphics->Create()` (`:504`) returns still-deferred (`gpGraphics->mbSwapchainRecreateDeferred` still set at `:508`), wait before the `return` at `:512`:
   - **Mechanism**: high-resolution waitable timer mirroring `ServerSessionBase::WaitForTick` (`ServerSessionBase.cpp:26-59`) minus the ~500 µs precision spin — `CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS)` + `SetWaitableTimerEx` + `WaitForSingleObject`. Do not share code through the `BT_SERVER` seam (`ServerSessionBase` is class-level `BT_SERVER` and `Refactor_SessionBaseCollapse` deletes it); no `timeBeginPeriod` dependency (the high-resolution timer self-carries granularity).
   - **Interval**: remainder of one sim tick — `game::kTickNs` minus elapsed time since the previous skip-branch iteration, floored at ~1 ms (skip the wait entirely if the remainder is ≤ 0), so the minimized loop holds ~32 Hz cadence exactly. Track the last-iteration timestamp with a local high-resolution clock (`QueryPerformanceCounter` or `std::chrono::steady_clock`) private to the branch — do **not** read or advance `mTimeStep.mRealTime`, whose delta bookkeeping belongs to `ClientUpdate`.
   - **Ownership**: timer `HANDLE` as a client-only `GameBase` member (or equivalent at the site), lazily created on first use in the branch, `CloseHandle` in the destructor — mirroring `ServerSessionBase`'s handle ownership (`ServerSessionBase.cpp:13-24`). The last-iteration timestamp resets/invalidates whenever the branch is not taken so a fresh minimize never inherits a stale remainder.
2. Ensure the wait executes only on the still-deferred path — the proceeded path (`!mbSwapchainRecreateDeferred` → `AcquireNextImage()` at `:510`) and the normal render path below `:515` must not gain any wait.
3. Verify per the acceptance criteria below via the agent harness, using the in-band `window_state` command (`AgentCommandsClient.cpp:265`, dispatch `:771`, landed this tree): launch client `--agent-port`, `window_state {"minimized": true}`, measure process CPU while minimized, confirm `ping`/`get_logs` respond timely, `window_state {"minimized": false}` and confirm render resumes at the restored extent (deferred response resolves via `ExtentSettled()`).

## Additional candidate locations

- `Engine/Source/Main.cpp:387-396` — the `DeviceLostException` catch (reset + reconstruct `Graphics`, `continue` with no wait). **Oversight**, **Related**, medium confidence — **Rejected by the extension-review gate as a false positive**: under persistent device loss the `Graphics` ctor re-throws *inside* the catch handler, so the exception escapes to `wWinMain`'s handler (`Main.cpp:820-831` → `engine::HandleException`) rather than looping; a flapping-device re-iteration self-paces via full Graphics reconstruction (tens-to-hundreds of ms of inherent work), so a tens-of-ms wait is immaterial; and a robustness edit there would need its own retry-cap/escalation design the plan never scoped. Not folded, no user decision required.

No other candidates: the sweep examined every main-loop/render-path early-return and retry loop; all other sites are paced (server `WaitForTick`, ENet timeouts, condition-variable worker threads) or feed back into this plan's own target branch.

## Critical files

- `Engine/Source/GameBase.cpp` — client `GameBase::Render`, the `mbSwapchainRecreateDeferred || meDestroyType >= kSwapchain` skip branch (`:502-513`). Primary edit site for Option A/C.
- `Engine/Source/Main.cpp` — client main loop (`while (true)` `:341`; `ProcessMessages` `:346`, agent `Drain` `:357`, `ProcessInput` `:371`, `ClientUpdate` `:381`, `Render` `:389`). Edit site for Option B (loop-top wait); otherwise read-only context for the liveness constraints.
- `Engine/Source/Graphics/Graphics.cpp` — `Graphics::Create()` (`:328`) and `SurfaceExtentZeroArea` (`:317`, the `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` per-retry). Read-only: establishes the per-spin driver cost; not modified.

## Acceptance criteria

- A minimized (or dragged fully off-screen) client no longer pins ~100% of a core — CPU for the process drops substantially while minimized.
- Restore from minimized resumes rendering within a few hundred ms (target: not noticeably worse than the current ~271 ms).
- Agent commands (`ping`, `get_logs`, `resize`, etc.) sent to a minimized client still receive timely responses via `Drain`.
- The client sim continues consuming server updates while minimized (no growing clock-behind / starvation on restore).
- No change to recreate-retry behavior: the deferred→proceeded transition and re-acquire still occur exactly once the surface extent becomes valid.

## Out of scope

- Any change to the swapchain recreate/defer logic itself (`Graphics::Create()` gates, `mbSwapchainRecreateDeferred` latch, TOCTOU re-check) — owned by the landed wedge work; this plan only throttles the skip loop.
- The `Main.cpp` `DeviceLostException` catch path — vetted and rejected above (self-pacing / escapes to crash handler; any robustness work there is a separate retry-cap/escalation design).
- Server-side loop throttling (the server has no swapchain; its `WaitForTick` cadence is the mirrored precedent, not an edit target).
- Suspending audio / lowering process priority while minimized (agent-mode already suspends audio at boot; a general minimize-suspend is a separate concern).
- Throttling a *visible* but idle client, or any general frame-rate cap on the normal render path — this plan touches only the deferred/minimized skip branch.
- Pausing or slowing the client sim while minimized — sim liveness must be preserved, not reduced.

## Notes

- **Invariant exposure:** client-only, `#if defined(BT_CLIENT)` region (`GameBase::Render` is already inside the file's `BT_CLIENT` span). No determinism/CRC sim-path exposure (render-loop cadence only; `ClientUpdate`/PostRender state untouched), no `kiVersion`/`.pack` layout, no replay, no wire change. Not an allocation-tracked concern beyond the existing branch. Runtime-observable → verifiable live via the agent harness (`resize` to minimize/off-screen, measure CPU + restore latency, confirm `ping`/`get_logs` still respond).
- **Grill decisions (resolved 2026-07-10):** (a) mechanism — **Option A, waitable timer** (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, self-contained granularity; `Sleep`'s 1 ms granularity today rides only on ENet's `timeBeginPeriod(1)` at `ThirdParty/enet/win32.c:32` — rejected as fragile); (b) interval — **match the sim tick** via a remainder wait (`game::kTickNs` minus elapsed loop time, floor ~1 ms) holding the minimized loop at ~32 Hz — at, not below, tick rate, avoiding the below-tick-rate jitter-estimator inflation regime documented by `Network/JitterMeasurementLowFpsSkew.md`; (c) loop-wide throttle accepted (no `ClientUpdate` scoping needed — at tick-rate cadence the sim consumes one tick per iteration by construction). Options B/C rejected. Verification is in-band via the landed `window_state` agent command.
