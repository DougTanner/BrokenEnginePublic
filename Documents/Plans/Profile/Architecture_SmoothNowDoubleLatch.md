# Architecture: SmoothNow Double-Latch

## Context

Source: /external-architecture-review on `Engine/Source/Profile`. `common::Smoothed::operator=` pushes a ring sample (`Common/Smoothed.h:29-38`). A `CpuStop(..., bSmoothNow = true)` latches the real value and zeroes the accumulator (`ProfileManagerBase.cpp:186-192`), but `SmoothCpuTimers()` then latches *every* timer except index 0 (`if (i > kCpuTimerAcquireToGlobal)`, `ProfileManagerBase.cpp:375-381`), pushing the now-zero accumulator as a second sample. The ring alternates V, 0, V, 0 → `Average()` ≈ V/2 → displayed values are roughly half of reality, and the `iValue < 50` visibility cull (`ProfileScreens.cpp:27`) can hide marginal timers it shouldn't.

Affected timers (each verified to stop once per frame/tick interleaved with a `SmoothCpuTimers()` call):

- Client: `kCpuTimerNetworkSend` (`ClientSession.cpp:182`) and `kCpuTimerNetworkPollReconcile` (`ClientSession.cpp:228`) vs the per-render-frame `SmoothCpuTimers()` in `UpdateProfileText` (`Graphics.cpp:249` → `ProfileManagerBase.cpp:395`).
- Server: `game::kCpuTimerFrameUpdate` (`GameBase.cpp:149`, `bSmoothNow = true`) vs the per-tick `SmoothCpuTimers()` at `ServerDisplay.cpp:80` — the server GDI display's headline frame-time row is halved.
- Only `kCpuTimerAcquireToGlobal` (`CommandBufferManager.cpp:105`) is protected, because the exclusion is hard-coded to index 0.

Root cause: the exclusion set ("timers that latch at stop") is encoded as a magic index comparison, and the set of `bSmoothNow` users has grown past it.

## Design

### Engine/Source/Profile/ProfileManagerBase.h
- Add `bool bSmoothAtStop = false;` to the `CpuTimer` struct (`ProfileManagerBase.h:31-44`) [~5m]

### Engine/Source/Profile/ProfileManagerBase.cpp
- `CpuStop`: in the `if (bSmoothNow)` block (`:186-192`), set `rCpuTimer.bSmoothAtStop = true` [~5m]
- `SmoothCpuTimers` (`:366-386`): replace `if (i > kCpuTimerAcquireToGlobal)` with `if (!rCpuTimer.bSmoothAtStop)`; the `Update()` calls stay unconditional so flagged timers keep drifting toward their stop-latched samples [~10m]

The flag is sticky by design: every current call site uses a consistent `bSmoothNow` mode per timer per build (grep-verified across `Main.cpp:282`, `GameBase.cpp:84/149/207-208/388-389`, `Graphics.cpp`, `SwapchainManager.cpp`, `CommandBufferManager.cpp`, `ClientSession.cpp` — `kCpuTimerFrameUpdate` is `false` on the client and `true` on the server, which never conflict within one build).

After the fix, `Engine/Source/Profile/CLAUDE.md:25` ("timers whose scope completes out of phase … pass `bSmoothNow` to latch at stop") describes the actual behavior — no doc edit needed.

## Critical files
- Engine/Source/Profile/ProfileManagerBase.h
- Engine/Source/Profile/ProfileManagerBase.cpp

## Acceptance criteria
- With profiling enabled, the client overlay's "Network send" / "Network poll+reconcile" values roughly double vs current builds (no longer averaged with interleaved zeros).
- The server GDI Profile tab's "Frame update" row shows the full per-tick cost.
- Per-frame timers (stopped with `bSmoothNow = false`) display unchanged.

## Out of scope
- The visibility special-case `i > kCpuTimerAcquireToGlobal && iValue < 50` in `FormatCpuTimersText` (`ProfileScreens.cpp:27`) — separate display-policy semantics ("always show timer 0"), not the latch bug.
- `CpuStop`'s two positional bools → `common::Flags` (`Refactor_InFunctionCleanups.md` — same function; co-schedule).
- The `UpdateProfileText` client/server guard restructure (`Architecture_ClientServerGuardScope.md`).

## Notes
- No determinism/CRC/network/`kiVersion` exposure — profiling display state only.
- Grill decision: none expected; the per-timer flag is the KISS fix. The alternative (an explicit exclusion list in `SmoothCpuTimers`) reproduces the same magic-set problem.

## Verification Notes
All items verified against source (2026-06-10 pass):
- Mechanism re-derived independently: `CpuStop(bSmoothNow=true)` pushes the real value and zeroes the accumulator (`ProfileManagerBase.cpp:186-192`); `SmoothCpuTimers` then pushes the zeroed accumulator for every `i > kCpuTimerAcquireToGlobal` (`:375-381`); `Smoothed::operator=` (`Common/Smoothed.h:29-38`) ring-pushes, `Update()` drifts toward `Average()`, `Get()` returns the drifted value — so the V,0,V,0 ring → ≈V/2 display is correct.
- 1:1 interleave confirmed for all three affected timers: `ClientUpdate` (NetworkSend/PollReconcile stops) precedes `Render` → `UpdateProfileText` each main-loop pass (`Main.cpp:285`/`:293`); `ServerUpdateDisplayStats` (`Main.cpp:309`) runs after `ServerUpdate` each tick. Server headline row reads `.Average()` directly (`ServerDisplay.cpp:335`) — also healed.
- Per-build `bSmoothNow` consistency grep-verified across all 17 `CpuStop` call sites: `kCpuTimerFrameUpdate` is `false` only in `BT_CLIENT` code (`GameBase.cpp:84`, `:389`) and `true` only in `BT_SERVER` code (`:149`); no timer mixes modes within one build, so the sticky flag is safe.
- Unconditional `Update()` retention confirmed correct — identical to today's index-0 (`kCpuTimerAcquireToGlobal`) handling, which already follows the latch-at-stop + drift pattern this plan generalizes.
- No caveats; all cited file:line locations exact at verification time.
