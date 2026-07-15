# Refactor: Collapse SessionBase Layer into Game Sessions

## Summary

**What this plan does:** Deletes three welded-to-game "session base" classes — engine `ClientSessionBase.{h,cpp}`, engine `ServerSessionBase.{h,cpp}`, and game `ClientDataReceiver.{h,cpp}` — and folds their members and method bodies verbatim into their single game subclasses (`game::ClientSession` / `game::ServerSession`). `ClientSession`/`ServerSession` stop inheriting anything; the folded client bodies distribute across `ClientSession.cpp`, `ClientSessionSubscriptions.cpp`, and one new `ClientSessionReceive.cpp`, and the server bodies land in the existing `ServerSession.cpp`. Only `Engine.h` (two include removals) and the two vcxproj/.filters pairs flip. Pure symbol relocation — no logic, wire, CRC, or determinism change.

**Why it's good for the codebase:** Removes an inheritance layer with **zero engine capability** — three classes that are the bottom half of one game policy split across a seam, not reusable engine machinery — and reunites two single mechanisms currently torn in two: clock correction (`ComputeClockCorrectionNs` computes in the base, the snap threshold+execution lives game-side in `Reconcile`) and the per-frame subscription policy that bounces game→engine→game. It is the **prerequisite that unblocks two queued Network refactors** (`Refactor_ClientResetUnification.md`, which needs all reset fields owned by one class, and `Refactor_DrainContractUnification.md`, whose call-site list shrinks once `ClientDataReceiver` folds away).

## Context

- **Source:** `Documents/Plans/Network/Refactor_SessionBaseCollapse.md` (removed with its Order.md row after execution completes).
- **Order.md row:** Tier Medium / Effort 3 / Impact 2 / Risks 1 / Score 2.
- **Notes (verbatim):** Delete `ClientSessionBase`/`ServerSessionBase`/`ClientDataReceiver` (each one subclass, welded to `gpGame`), fold bodies into game `ClientSession`/`ServerSession` across sibling TUs; removes a zero-capability layer and reunites the clock + subscription mechanisms split across the seam. Engine calls already go through game globals (no call-site edits); only `Engine.h` + 2 vcxproj pairs flip. Move-only, no wire/CRC/determinism. Decided: clock correction folds into `ClientSession.cpp`; server timing stays in `ServerSession.cpp`; add one client receive TU and no dedicated clock/server timing TUs.
- **Relevance:** Partially — all files/symbols exist, with current citations and scope facts listed below.
- **Dependency resolution:** this plan is the prerequisite for `Network/Refactor_DrainContractUnification.md` and has no unmet prerequisites.
- **Current-source refinements:**
  - The member-absorption list uses the 12 confirmed clock fields named in the Client-side step; `miConsecutiveClockErrorFrames` is absent from source.
  - Server-side uses `ServerSession::PreTickNetwork` (`ServerSession.cpp:286`), which calls `PollNetworkBase()` at `:292`.
  - The snap block consumes engine constant `engine::kiClockSnapThreshold` (`NetworkProtocol.h:70`, value 28).
  - Current citations: `TextureManager.cpp:232`, `Game.cpp:889`, `GameBase.cpp` `GetSimTickCeiling` at `:62`, `WaitForTick` at `:131`, `Engine.h` at `:77`/`:90`, and `ProfileManager.cpp` at `:268`/`:284`.
  - Current TU sizes: `ClientSession.cpp` 501 lines, `ClientSessionSubscriptions.cpp` 183 lines, `ServerSession.cpp` 627 lines — line projections in Notes refreshed accordingly.

Evidence (verified current source):

- **Welded to game state**: `ClientSessionBase.cpp` reads `game::gpGame->mCoordFrames` in `DisconnectFromServerBase` (`:63`), `ApplyReceivedUpdatesBase` (`:254`), `GetConfirmedTick` (`:382`), `GetClientConfirmedTick` (`:394`), `GetServerUpdateBufferSize` (`:405`); `ServerSessionBase.cpp` reads `game::gpGame->mCoordFrames` (`:82-83`) / `TickCounter()` (`:86`) in `SendNewSubscriptionFullStates`. Both base `.cpp`s `#include "Game.h"` directly (`ClientSessionBase.cpp:7`, `ServerSessionBase.cpp:7`).
- **Single mechanisms split across the seam**:
  - *Clock correction*: `ClientSessionBase::ComputeClockCorrectionNs` (`ClientSessionBase.cpp:292-375`) computes the correction, but the snap threshold (`engine::kiClockSnapThreshold`, `NetworkProtocol.h:70`) + snap execution is a game-layer function-local block in `ClientSession::Reconcile` (`ClientSession.cpp:213-228`). One mechanism, two files.
  - *Subscription policy ping-pong*: `ClientSession::UpdateSubscriptions` (`ClientSessionSubscriptions.cpp:146-178`, game) calls `UnsubscribeStaleCoords` (`:175`) / `BuildSubscriptionQueue` (`:176`) / `TrySubscribeNext` (`:177`) — engine-base methods defined in `ClientSessionBase.cpp` (`:164` / `:195` / `:119`) — around game-side sticky-timestamp logic: a single per-frame policy that bounces game→engine→game.
- **`ServerSessionBase` bundles three unrelated concerns** with public raw members (`mpDiscoveryResponder` `ServerSessionBase.h:25`, `mTimerHandle` `:26`): tick-timing (`WaitForTick` waitable timer + spin `ServerSessionBase.cpp:26-59`; timer create/close in ctor/dtor `:13-24`), discovery-responder ownership (`PollNetworkBase` `:61-65`), and `SendNewSubscriptionFullStates` (`:67-94`, operates purely on `gpServer` + `gpGame`).
- **`ClientDataReceiver`** (`ClientDataReceiver.h`) has zero data members and three methods, one of which (`ApplyReceivedUpdates`) is a one-line forwarder to `ClientSessionBase::ApplyReceivedUpdatesBase` (`ClientDataReceiver.cpp:139-142`, body `:141`). Its other two methods (`ApplyReceivedStaticData` `:14`, `ApplyReceivedFullStates` `:36`) already reach everything through `gpClientSession` / `gpGame`. No external code depends on the `mpDataReceiver` handle — only `ClientSession::PollNetwork` (`ClientSession.cpp:114-117`) calls it; two other hits are comments (`TextureManager.cpp:232`, `Game.cpp:889`).

Engine call sites already go through game globals, so collapse requires **no engine call-site edits**:
- `GameBase.cpp:62` `game::gpClientSession->GetSimTickCeiling()` and `:131` `game::gpServerSession->WaitForTick(mTimeStep)` — `GameBase.cpp` already `#include`s `Network/Client/ClientSession.h` (`:5`, under `BT_CLIENT` `:4`) / `Network/Server/ServerSession.h` (`:8`, under `BT_SERVER` `:7`). Methods are inherited today; they become direct members after collapse.
- `ProfileManager.cpp:268`/`:284` call `gpClientSession->GetConfirmedTick()`/`GetServerUpdateBufferSize()` — same, game global, inherited method.

The **only** engine references to the base *type names* are `Engine.h:77` (`Network/Client/ClientSessionBase.h`) and `Engine.h:90` (`Network/Server/ServerSessionBase.h`). `Engine.h` does not reference `ClientDataReceiver`.

## Design

Delete `ClientSessionBase.{h,cpp}`, `ServerSessionBase.{h,cpp}`, and `ClientDataReceiver.{h,cpp}`; fold their contents into the game sessions. Pure move — no logic change, no wire/CRC/determinism exposure. `ClientSession` (`ClientSession.h:31`) / `ServerSession` (`ServerSession.h:24`) stop inheriting the bases (they inherit nothing after).

### Client side

`ClientSession.h` absorbs all base members and the `SessionStateFlags` enum (`ClientSessionBase.h:13`): `mpClientNetwork`, `mpDiscoveryScanner`, `mSessionFlags`, `mcDiscoveredAddress`, and the clock fields `miLatestServerTick`, `miClockError`, `miClockOffset`, `miClockTargetBehind`, `miCurrentTargetBehind`, `miLastLoggedClockTargetBehind`, `miLastPeriodicClockLogTick`, `miLastClockErrorLogTick`, `miCoordSlots`, and `mSubscriptionQueue` (base fields `ClientSessionBase.h:50-65`). Remove `mpDataReceiver` (`ClientSession.h:73`; the class is gone); `ClientDataReceiver`'s three methods become `ClientSession` members (`ApplyReceivedStaticData`, `ApplyReceivedFullStates`, `ApplyReceivedUpdates`). Keep `mpDesyncManager` (`ClientSession.h:74`) / `mpReconciler` (`:75`) unique_ptrs (real owned subsystems, unchanged).

Distribute the folded bodies across sibling TUs to respect the 500–1000-line guideline (`ClientSession.cpp` is 501 lines; the base cpp bodies below net ~+195):

- **`ClientSession.cpp`** — connection lifecycle: absorb base `ConnectToServer` (`ClientSessionBase.cpp:45-53`), `DisconnectFromServerBase` body merged into `DisconnectFromServer` (`ClientSession.cpp:245-255`), `StartServerDiscovery` (`ClientSessionBase.cpp:78-84`), `PollLANDiscovery` (`:86-109`), the anonymous-namespace GUID helpers `LoadClientGuidFromDisk` (`:17`) / `PersistClientGuidToDisk` (`:31`). Keep the existing `Poll`/`Reconcile`/`PollNetwork`, `ResetForServerLoad` (`ClientSession.cpp:332`), `ClearSubscriptionState` (`:383`), and game-packet sends here.
- **`ClientSessionSubscriptions.cpp`** — absorb base subscription mechanics `TrySubscribeNext` (`ClientSessionBase.cpp:119-163`), `UnsubscribeStaleCoords` (`:164-194`), `BuildSubscriptionQueue` (`:195-228`), plus the file-local `static` free helper `IsSlotActive` (`:113`) — which those three call, so it moves into the same TU to stay accessible. Lands next to the existing `UpdateSubscriptions` (`:146`) / `UpdateDesiredCoords` (`:10`) that already drive them — closing the game↔engine ping-pong into one TU. (Current TU 183 lines + ~130 net.)
- **`ClientSessionReceive.cpp`** (new, matching the existing `ClientSessionSubscriptions.cpp` sibling pattern) — absorb the three former `ClientDataReceiver` bodies (`ClientDataReceiver.cpp` in full: `:14`, `:36`, `:139`), base `ApplyReceivedUpdatesBase` (`ClientSessionBase.cpp:229-288`, becomes `ApplyReceivedUpdates`'s body — the one-line forwarder disappears), and the query helpers `GetConfirmedTick` (`:379-390`), `GetClientConfirmedTick` (`:392-400`), `GetServerUpdateBufferSize` (`:402-413`). (`GetSimTickCeiling` stays inline in the header — `ClientSessionBase.h:48`.)
- **Clock correction** — fold base `ComputeClockCorrectionNs` (`ClientSessionBase.cpp:292-375`) into `ClientSession.cpp`, beside the snap logic in `ClientSession::Reconcile` (`:213-228`) that consumes it, reuniting the split mechanism. The game wrapper `ClientSession::ComputeClockCorrectionNs` (`ClientSession.cpp:51-57`, calls base at `:53`, `gpProfileManager->SetClockCorrection(...)` at `:54`) merges into the folded body — keep its trailing `SetClockCorrection` call as the tail of the merged function. **No `ClientSessionClock.cpp` is created.** (Decision rationale in Notes.)

### Server side

`ServerSession.h` absorbs `mpDiscoveryResponder` and `mTimerHandle` (drop `public` raw exposure — they become plain members of the owning class; no external reader exists). `ServerSession` ctor/dtor absorb the timer create/`timeBeginPeriod`/`CloseHandle`/`timeEndPeriod` (`ServerSessionBase.cpp:13-24`).

- **`ServerSession.cpp`** — `SendNewSubscriptionFullStates` (`ServerSessionBase.cpp:67-94`) lands beside `SubscriptionUpdates` (`ServerSession.cpp:449`, already calls it at `:451`) / `HandleResyncRequests` (`:469`), its post-tick siblings already here; `PollNetworkBase` (`ServerSessionBase.cpp:61-65`) becomes a `ServerSession` member (or inlines into the existing `ServerSession::PreTickNetwork` path — `ServerSession.cpp:286`, which calls `PollNetworkBase()` at `:292`). `WaitForTick` (`ServerSession.cpp:104-108`, already wraps the base at `:107`) — merge the base body (`ServerSessionBase.cpp:26-59`) into it directly.
- All server-side folds land in the single existing `ServerSession.cpp` — **no `ServerSessionTiming.cpp` is created**. Projected size ~700 lines (627 current + ~73 net after the `WaitForTick` wrapper merge and ctor/dtor absorption), within the 500–1000-line guideline. (Decision rationale in Notes.)

### Engine / project wiring

- **`Engine.h`** — remove the `#include "Network/Client/ClientSessionBase.h"` (`:77`) and `#include "Network/Server/ServerSessionBase.h"` (`:90`).
- **vcxproj + filters** — client build (`BrokenEngineSandbox.vcxproj`/`.filters`): remove the `ClientSessionBase.{h,cpp}` and `ClientDataReceiver.{h,cpp}` items; add `ClientSessionReceive.cpp` (the only new TU). Server build (`BrokenEngineSandboxServer.vcxproj`/`.filters`): remove the `ServerSessionBase.{h,cpp}` items; no new server TU. New TUs are whole-file `#if defined(BT_CLIENT)` / `BT_SERVER` wrapped and appear only in the respective side's vcxproj (per the Server/AGENTS.md build-config rule).
- **Comment refresh** — update the two comment mentions `ClientDataReceiver::ApplyReceivedStaticData` → `ClientSession::ApplyReceivedStaticData` (`TextureManager.cpp:232`, `Game.cpp:889`).
- **AGENTS.md** — `Engine/Source/Network/Client/AGENTS.md` and `Server/AGENTS.md` document session policy in the game layer and transport `Client`/`Server` in the engine; game `Network/Client/AGENTS.md` describes the folded `ClientDataReceiver` responsibilities under `ClientSession`. Parent `Network/AGENTS.md` "See Also" bullets name the concrete game sessions.

## Critical files

- `Engine/Source/Network/Client/ClientSessionBase.{h,cpp}` — deleted; bodies distributed as above.
- `Engine/Source/Network/Server/ServerSessionBase.{h,cpp}` — deleted; bodies folded into `ServerSession`.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientDataReceiver.{h,cpp}` — deleted; three methods become `ClientSession` members.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.{h,cpp}` — absorbs base members/enum + connection lifecycle + clock.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionSubscriptions.cpp` — absorbs base subscription mechanics + `IsSlotActive`.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionReceive.cpp` — new; receive bodies + `ApplyReceivedUpdatesBase` + queries.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.{h,cpp}` — absorbs base members + tick timing + `SendNewSubscriptionFullStates`.
- `Engine/Source/Engine.h` — remove two base-header includes (`:77`, `:90`).
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj`(`.filters`) and `…Server.vcxproj`(`.filters`) — membership edits.

## Out of scope

- **`Client` / `Server` transport peers stay in the engine, untouched.** They own the wire (receive dispatch, per-slot subscription state, ACK/RTT/bandwidth, ring buffers) and are engine-generic (confirmed: zero reads of session-policy state — only the sanctioned engine→game type/constant/timescale reads and the one `SendTimespeedToNewClient` hook-out); this plan only removes the *session-policy* base layer, not transport.
- No wire-format, `kuiProtocolVersion`, or `Frame::kiVersion` change; no CRC / determinism / reconciliation-logic change. Pure symbol relocation.
- No change to the subscription state machine, clock-correction math, tick-timing algorithm, or receive semantics — bodies move verbatim.
- Not the reset-path consolidation (`Refactor_ClientResetUnification.md`) — that is a follow-up enabled by this collapse.
- Not the `Drain*` accessor renaming (`Refactor_DrainContractUnification.md`).

## Acceptance criteria

- `ClientSessionBase`, `ServerSessionBase`, `ClientDataReceiver` no longer exist; `git grep` finds no references outside deleted files and refreshed AGENTS.md.
- Client and server builds compile with no base-class references; `ClientSession` / `ServerSession` inherit nothing.
- Every folded method keeps its body byte-for-byte (aside from `this->`-qualification of former `gpClientSession->` self-calls and the removed one-line forwarder).
- No new file exceeds the 1000-line guideline; the `ClientSession.cpp` fold split keeps each TU under it.

## Coordination

- Never interleave with `Documents/Plans/Network/Refactor_DrainContractUnification.md`, `Documents/Plans/Network/Refactor_ClientResetUnification.md`. The structured chain is SessionBaseCollapse → DrainContractUnification → ClientResetUnification; refresh relocated citations between landings.

## Notes

- **Sequence**: this is the **first** of three interacting Network refactors. Land it before `Refactor_ClientResetUnification.md` (which needs the reset fields owned by a single class) and before `Refactor_DrainContractUnification.md` (which renames the `Drain*` accessors this plan's receive code calls and has fewer call sites once `ClientDataReceiver.cpp` is removed). Execute the collapse alone; do not co-schedule `Refactor_DrainContractUnification.md`. Sibling plans refresh their `path:line` citations when selected. See Dependencies in `Order.md`.
- **Sibling-plan citation drift**: `Network/SubscriptionLifecycleRaceHardening.md` cites its watchdog tick site as "`ClientSessionBase` (`ClientSessionBase.cpp`), alongside the existing queue rebuild" — this collapse moves that code to `ClientSessionSubscriptions.cpp`. The reset-unification plan touches the resync reset region. Refresh their citations if collapse lands first.
- **Invariant exposure**: touches **client/server `BT_*` guard scope** (whole-file guards on the new TUs; membership must match, per Server/AGENTS.md) but **no** determinism/CRC/wire/`kiVersion`/replay/allocation-tracked-path exposure — move-only, compile-checked.
- **Decision (2026-07-03, projections refreshed against current source)** — both pre-staged options resolved by measured line projections; no open decisions remain:
  1. *Clock-correction landing*: `ComputeClockCorrectionNs` folds into `ClientSession.cpp` beside the snap in `Reconcile`; no `ClientSessionClock.cpp`. Projected `ClientSession.cpp` after all its folds ≈ 690 lines (501 current + GUID helpers ~31 + `ConnectToServer` 9 + `DisconnectFromServerBase` merge ~20 + `StartServerDiscovery` 7 + `PollLANDiscovery` 24 + `ComputeClockCorrectionNs` ~84 − wrapper merge ~7, plus separators) — the receive/query bodies land in the new `ClientSessionReceive.cpp` instead, so this stays comfortably under the 1000-line guideline. A single clock function does not justify a dedicated TU (YAGNI).
  2. *Server tick-timing*: stays in `ServerSession.cpp`; no `ServerSessionTiming.cpp`. Projected ≈ 700 lines (627 current + ctor/dtor timer bodies ~12 + `WaitForTick` body ~34 − existing 5-line wrapper + `PollNetworkBase` 5 + `SendNewSubscriptionFullStates` 28) — the file grew from 600→627 since the plan was written, so the projection is ~700 rather than the original ~680, still well under the 1000-line guideline. `ServerSession.cpp` is already the single orchestrator TU; a ~30-line spin loop is not a subsystem. Decision unchanged.

## Additional candidate locations

No additional candidates found. A dedicated Step-6 sweep inventoried the entire network session layer and confirmed the three-class scope is complete:
- **Client helpers** `ClientDesyncManager` and `ClientReconciler` own real state (desync-debug frame + escalation counters; confirmed-state worklist + generation/hysteresis) — genuine subsystems the plan correctly keeps as `mpDesyncManager`/`mpReconciler`, not zero-capability layers.
- **Server managers** `ServerFleetManager`, `ServerTransferManager`, `ServerBroadcaster`, `ServerClientManager`, `FleetNavigationController` each own substantial state — they are the decomposition of `ServerSession`, the opposite of an indirection layer (folding them in would create a god-class).
- **Transport peers** `engine::Client` / `engine::Server` are genuinely engine-generic (own the wire, hold zero reads of session-policy state — only sanctioned engine→game type/constant/timescale reads and one hook-out call) — the plan correctly excludes them.
- No transitive base beyond the three named (both bases inherit nothing; both game sessions inherit only their single base). No Oversight and no Drift candidates.
