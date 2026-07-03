# Refactor: Collapse SessionBase Layer into Game Sessions

## Context

`engine::ClientSessionBase` and `engine::ServerSessionBase` each have exactly one subclass (`game::ClientSession` / `game::ServerSession`) and are welded to game state. The engine→game reads are **sanctioned** (root CLAUDE.md) — this plan is **not** about layering purity. The argument is narrower: these bases are not reusable engine machinery, they are the bottom half of one game policy, split across an inheritance seam that adds a layer with zero capability and tears single mechanisms in two.

Evidence (verified current source):

- **Welded to game state**: `ClientSessionBase.cpp` reads `game::gpGame->mCoordFrames` in `DisconnectFromServerBase` (`:64`), `ApplyReceivedUpdatesBase` (`:257`), `GetConfirmedTick` (`:402`), `GetClientConfirmedTick` (`:414`), `GetServerUpdateBufferSize` (`:425`); `ServerSessionBase.cpp` reads `game::gpGame->mCoordFrames`/`TickCounter()` in `SendNewSubscriptionFullStates` (`:81-85`). It also includes `"Game.h"` directly.
- **Single mechanisms split across the seam**:
  - *Clock correction*: `ClientSessionBase::ComputeClockCorrectionNs` (`:295-395`) computes the correction, but the snap threshold + snap execution is a game-layer function-local (`ClientSession::Reconcile`, `ClientSession.cpp:213` `kiClockSnapThreshold = 28`, snap block `:214-228`). One mechanism, two files.
  - *Subscription policy ping-pong*: `ClientSession::UpdateSubscriptions` (`ClientSessionSubscriptions.cpp:146-178`, game) calls `UnsubscribeStaleCoords` / `BuildSubscriptionQueue` / `TrySubscribeNext` (engine base) around game-side sticky-timestamp logic — a single per-frame policy that bounces game→engine→game.
- **`ServerSessionBase` bundles three unrelated concerns** in ~97 lines with public raw members (`mTimerHandle`, `mpDiscoveryResponder`): tick-timing (`WaitForTick` waitable timer + spin, `:26-58`; timer create/close in ctor/dtor `:13-24`), discovery-responder ownership (`PollNetworkBase` `:60-64`), and `SendNewSubscriptionFullStates` (`:66-93`, operates purely on `gpServer` + `gpGame`).
- **`ClientDataReceiver`** (`ClientDataReceiver.h`) has zero data members and three methods, one of which (`ApplyReceivedUpdates`) is a one-line forwarder to `ClientSessionBase::ApplyReceivedUpdatesBase` (`ClientDataReceiver.cpp:139-142`). Its other two methods already reach everything through `gpClientSession` / `gpGame`. No external code depends on the `mpDataReceiver` handle — only `ClientSession::PollNetwork` (`:114-117`) calls it; two other hits are comments (`TextureManager.cpp:221`, `Game.cpp:887`).

Engine call sites already go through game globals, so collapse requires **no engine call-site edits**:
- `GameBase.cpp:64` `game::gpClientSession->GetSimTickCeiling()` and `:130` `game::gpServerSession->WaitForTick(mTimeStep)` — `GameBase.cpp` already `#include`s `Network/Client/ClientSession.h` / `Network/Server/ServerSession.h` under `BT_CLIENT`/`BT_SERVER` (`:4-9`). Methods are inherited today; they become direct members after collapse.
- `ProfileManager.cpp:268`/`:284` call `gpClientSession->GetConfirmedTick()`/`GetServerUpdateBufferSize()` — same, game global, inherited method.

The **only** engine references to the base *type names* are `Engine.h:77` (`Network/Client/ClientSessionBase.h`) and `Engine.h:90` (`Network/Server/ServerSessionBase.h`).

## Design

Delete `ClientSessionBase.{h,cpp}`, `ServerSessionBase.{h,cpp}`, and `ClientDataReceiver.{h,cpp}`; fold their contents into the game sessions. Pure move — no logic change, no wire/CRC/determinism exposure. `ClientSession` / `ServerSession` stop inheriting the bases (they inherit nothing after).

### Client side

`ClientSession.h` absorbs all base members and the `SessionStateFlags` enum: `mpClientNetwork`, `mpDiscoveryScanner`, `mSessionFlags`, `mcDiscoveredAddress`, the `mi*` clock fields (`miLatestServerTick`, `miClockError`, `miClockOffset`, `miClockTargetBehind`, `miCurrentTargetBehind`, `miLast*`, `miConsecutiveClockErrorFrames`, `miCoordSlots`), and `mSubscriptionQueue`. Remove `mpDataReceiver` (the class is gone); `ClientDataReceiver`'s three methods become `ClientSession` members (`ApplyReceivedStaticData`, `ApplyReceivedFullStates`, `ApplyReceivedUpdates`). Keep `mpDesyncManager` / `mpReconciler` unique_ptrs (real owned subsystems, unchanged).

Distribute the folded bodies across sibling TUs to respect the 500-1000-line guideline (`ClientSession.cpp` is already 506 lines; the base cpp is 437):

- **`ClientSession.cpp`** — connection lifecycle: absorb base `ConnectToServer` (`:46-54`), `DisconnectFromServerBase` body merged into `DisconnectFromServer` (`ClientSession.cpp:249-259`), `StartServerDiscovery` (`:81-87`), `PollLANDiscovery` (`:89-112`), the anonymous-namespace GUID helpers `LoadClientGuidFromDisk` / `PersistClientGuidToDisk` (`:17-42`). Keep the existing `Poll`/`Reconcile`/`PollNetwork`, `ResetForServerLoad`, `ClearSubscriptionState`, and game-packet sends here.
- **`ClientSessionSubscriptions.cpp`** — absorb base subscription mechanics `TrySubscribeNext` (`:122-165`), `UnsubscribeStaleCoords` (`:167-196`), `BuildSubscriptionQueue` (`:198-228`), plus the file-local `IsSlotActive` helper (`:116-120`). Lands next to the existing `UpdateSubscriptions` / `UpdateDesiredCoords` that already drive them — closing the game↔engine ping-pong into one TU. (~182 + ~115 lines.)
- **`ClientSessionReceive.cpp`** (new, matching the existing `ClientSessionSubscriptions.cpp` sibling pattern) — absorb the three former `ClientDataReceiver` bodies (`ClientDataReceiver.cpp` in full), base `ApplyReceivedUpdatesBase` (`:232-291`, becomes `ApplyReceivedUpdates`'s body — the one-line forwarder disappears), and the query helpers `GetConfirmedTick` (`:399-410`), `GetClientConfirmedTick` (`:412-420`), `GetServerUpdateBufferSize` (`:422-433`). (`GetSimTickCeiling` stays inline in the header.)
- **Clock correction** — fold base `ComputeClockCorrectionNs` (`:295-395`) into the same TU as the snap logic it feeds so the split mechanism is reunited. Recommended: keep the snap in `ClientSession::Reconcile` (`ClientSession.cpp`) and move `ComputeClockCorrectionNs` there too (the game wrapper at `ClientSession.cpp:51-57` merges into it), or place both in a new `ClientSessionClock.cpp` if `ClientSession.cpp` would exceed ~800 lines. See Notes (grill).

### Server side

`ServerSession.h` absorbs `mpDiscoveryResponder` and `mTimerHandle` (drop `public` raw exposure — they become plain members of the owning class; no external reader exists). `ServerSession` ctor/dtor absorb the timer create/`timeBeginPeriod`/`CloseHandle`/`timeEndPeriod` (`ServerSessionBase.cpp:13-24`).

- **`ServerSession.cpp`** — `SendNewSubscriptionFullStates` (`:66-93`) lands beside `SubscriptionUpdates`/`HandleResyncRequests` (its post-tick siblings, already here) and `PollNetworkBase`'s body merges into `WaitForTick`'s caller context; `PollNetworkBase` (`:60-64`) becomes a `ServerSession` member (or inlines into the existing `PollNetwork` path). `WaitForTick` (`ServerSession.cpp:104-108`) already wraps the base — merge the base body (`:26-58`) into it directly.
- Split option: if `ServerSession.cpp` (600 lines + ~97) reads cleanly, keep it one file (~680, within guideline). Otherwise move tick-timing (`WaitForTick` spin + timer ctor/dtor + discovery-responder poll) into a new `ServerSessionTiming.cpp` sibling. Recommended: keep in `ServerSession.cpp` unless it crosses ~700. See Notes.

### Engine / project wiring

- **`Engine.h`** — remove the `#include "Network/Client/ClientSessionBase.h"` (`:77`) and `#include "Network/Server/ServerSessionBase.h"` (`:90`).
- **vcxproj + filters** — client build (`BrokenEngineSandbox.vcxproj`/`.filters`): remove the `ClientSessionBase.{h,cpp}` and `ClientDataReceiver.{h,cpp}` items; add `ClientSessionReceive.cpp` (and `ClientSessionClock.cpp` if chosen). Server build (`BrokenEngineSandboxServer.vcxproj`/`.filters`): remove the `ServerSessionBase.{h,cpp}` items; add `ServerSessionTiming.cpp` if chosen. New TUs are whole-file `#if defined(BT_CLIENT)` / `BT_SERVER` wrapped and appear only in the respective side's vcxproj (per the Server/CLAUDE.md build-config rule).
- **Comment refresh** — update the two comment mentions `ClientDataReceiver::ApplyReceivedStaticData` → `ClientSession::ApplyReceivedStaticData` (`TextureManager.cpp:221`, `Game.cpp:887`).
- **CLAUDE.md** — `Engine/Source/Network/Client/CLAUDE.md` and `Server/CLAUDE.md` (remove the "engine-generic `ClientSessionBase`" / "`ServerSessionBase` game sessions inherit" framing; the session policy now lives wholly in the game layer, transport `Client`/`Server` stays engine); game `Network/Client/CLAUDE.md` (drop `ClientDataReceiver` from Key Classes, fold its description into `ClientSession`).

## Critical files

- `Engine/Source/Network/Client/ClientSessionBase.{h,cpp}` — deleted; bodies distributed as above.
- `Engine/Source/Network/Server/ServerSessionBase.{h,cpp}` — deleted; bodies folded into `ServerSession`.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientDataReceiver.{h,cpp}` — deleted; three methods become `ClientSession` members.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.{h,cpp}` — absorbs base members/enum + connection lifecycle + clock.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionSubscriptions.cpp` — absorbs base subscription mechanics.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionReceive.cpp` — new; receive + queries.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.{h,cpp}` — absorbs base members + tick timing + `SendNewSubscriptionFullStates`.
- `Engine/Source/Engine.h` — remove two base-header includes (`:77`, `:90`).
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj`(`.filters`) and `…Server.vcxproj`(`.filters`) — membership edits.

## Out of scope

- **`Client` / `Server` transport peers stay in the engine, untouched.** They own the wire (receive dispatch, per-slot subscription state, ACK/RTT/bandwidth, ring buffers) and are engine-generic; this plan only removes the *session-policy* base layer, not transport.
- No wire-format, `kuiProtocolVersion`, or `Frame::kiVersion` change; no CRC / determinism / reconciliation-logic change. Pure symbol relocation.
- No change to the subscription state machine, clock-correction math, tick-timing algorithm, or receive semantics — bodies move verbatim.
- Not the reset-path consolidation (`Refactor_ClientResetUnification.md`) — that is a follow-up enabled by this collapse.
- Not the `Drain*` accessor renaming (`Refactor_DrainContractUnification.md`).

## Acceptance criteria

- `ClientSessionBase`, `ServerSessionBase`, `ClientDataReceiver` no longer exist; `git grep` finds no references outside deleted files and refreshed CLAUDE.md.
- Client and server builds compile with no base-class references; `ClientSession` / `ServerSession` inherit nothing.
- Every folded method keeps its body byte-for-byte (aside from `this->`-qualification of former `gpClientSession->` self-calls and the removed one-line forwarder).
- No new file exceeds the 1000-line guideline; `ClientSession.cpp` split keeps each TU under it.

## Notes

- **Sequence**: this is the **first** of three interacting Network refactors. Land it before `Refactor_ClientResetUnification.md` (which needs the reset fields owned by a single class) and before/coordinated-with `Refactor_DrainContractUnification.md` (which renames the `Drain*` accessors this plan's receive code calls — landing collapse first shrinks that plan's call-site list, since `ClientDataReceiver.cpp` folds away). See Dependencies in `Order.md`.
- **Sibling-plan citation drift**: `Network/SubscriptionLifecycleRaceHardening.md` cites its watchdog tick site as "`ClientSessionBase` (`ClientSessionBase.cpp`), alongside the existing queue rebuild" — this collapse moves that code to `ClientSessionSubscriptions.cpp`. `Network/ResyncFullStateRepair.md` and the reset-unification plan both touch the resync reset region. Refresh their citations if collapse lands first.
- **Invariant exposure**: touches **client/server `BT_*` guard scope** (whole-file guards on the new TUs; membership must match, per Server/CLAUDE.md) but **no** determinism/CRC/wire/`kiVersion`/replay/allocation-tracked-path exposure — move-only, compile-checked.
- **Grill decision to pre-stage**: (1) clock-correction landing — fold `ComputeClockCorrectionNs` into `ClientSession.cpp` beside the snap (recommended, reunites the split mechanism) vs a dedicated `ClientSessionClock.cpp`; decided by whether `ClientSession.cpp` crosses ~800 lines. (2) server tick-timing — keep in `ServerSession.cpp` (recommended, ~680 lines) vs a `ServerSessionTiming.cpp` sibling.
