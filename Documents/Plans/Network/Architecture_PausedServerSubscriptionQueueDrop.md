# Architecture: Paused-Server Subscription Queue Drop

## Context

Source: /external-architecture-review on `Engine/Source/Network` (recursive). `Server::Poll` clears its pending-work queues at entry, but their consumers run only inside the per-tick broadcast path — which does not execute while the server is paused. A subscription accepted and ACKed during pause is silently wiped before its full state is ever sent, and the client has no timeout to recover. This is the highest-impact finding of the review.

## Design

### Engine/Source/Network/Server/Server.cpp
- `Server::Poll` clears `mPendingNewSubscriptions` and `mPendingResyncClientIds` at entry (`Server.cpp:64-65`). Their consumers (`ServerSessionBase::SendNewSubscriptionFullStates`, resync handling) run only from the game `ServerSession::BroadcastTick` path, which executes only when `iFullTicks > 0` (`GameBase.cpp:128-144`). When the server is paused (`GameFlags::kPaused` → `iFullTicks = 0`, `GameBase.cpp:112-116`), a `kClientSubscribe` is still accepted and ACKed immediately (`ServerReceive.cpp:358-376` sends `SubscribeAccept`), but the pending entry is wiped by the *next* `Poll` before any full state is sent. The client then sits in `kWaitingFullState` forever — no timeout exists in `Client`/`ClientSessionBase`. The same drop applies to resync requests during pause (partially mitigated client-side by `PollDesyncTimeout`). [~1h]
- Fix shapes (grill decision, see Notes):
  - (a) Move queue consumption to where the other `Poll` output queues are drained — `ServerSessionBase::PollNetworkBase` / game `PreTickNetwork` — so consumption is tied to polling, not ticking; or
  - (b) Change queue lifetime to persist-until-consumed: stop clearing in `Poll`, have the consumer (`SendNewSubscriptionFullStates` / resync broadcast) erase entries as it services them.
- Either shape must keep the existing behavior that a full state is sent exactly once per accepted subscription (epoch semantics unchanged).

### Engine/Source/Network/Server/ServerSessionBase.cpp
- `SendNewSubscriptionFullStates` (`ServerSessionBase.cpp:49`) is the consumer — adjust its call site or drain semantics to match the chosen shape.

## Critical files

- `Engine/Source/Network/Server/Server.cpp` (queue clear in `Poll`, `DrainPendingNewSubscriptions`)
- `Engine/Source/Network/Server/Server.h` (`mPendingNewSubscriptions`, `mPendingResyncClientIds`)
- `Engine/Source/Network/Server/ServerSessionBase.cpp` (`SendNewSubscriptionFullStates`)
- Read-only: `Engine/Source/GameBase.cpp` (`ServerUpdate` pause gating), `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` (`BroadcastTick`)

## Out of scope

- Adding a client-side `kWaitingFullState` timeout (a second, belt-and-braces fix — file separately if wanted; the server-side fix removes the wedge cause).
- The quickload early-return path (`GameBase.cpp:100-104`) also drops the queues, but `BroadcastLoadNotification` makes that benign — verified, no change.
- The subscribe-reject sentinel / slot-negotiation issue (`Architecture_SubscribeRejectSlotNegotiation.md`).
- Epoch/wire-format changes of any kind.

## Acceptance criteria

- Subscribing (or requesting resync) while the server is paused results in the full state / resync being sent when consumption next runs; the pending entry survives `Poll` until consumed.
- Unpaused behavior unchanged: one full state per accepted subscription, sent on the next broadcast.

## Notes

- **Invariant exposure**: touches server cross-frame queue lifetime feeding the subscription protocol — no wire-format or CRC change, but the send-once-per-subscription guarantee must be preserved (Risks 3 territory).
- Grill decision: consumption shape (a) drain-on-poll vs (b) persist-until-consumed. (b) is the smaller diff; (a) matches how the other `Poll` queues already work.

## Verification Notes

All items verified against source (2026-06-10):
- `Server::Poll` clears `mPendingNewSubscriptions`/`mPendingResyncClientIds` at entry — `Server.cpp:64-65` exact.
- Pause gating exact: `GameFlags::kPaused` → `iFullTicks = 0` (`GameBase.cpp:112-116`); per-tick loop `GameBase.cpp:128-144`; `BroadcastTick` called only from `FinalizeFrameTick` (`GameBase.cpp:233`).
- Consumer chain confirmed: `ServerSession::BroadcastTick` (`ServerSession.cpp:65-78`) → `HandleResyncRequests` (`:70`, drains `DrainPendingResyncClientIds` at `:431`) and `SubscriptionUpdates` (`:76`) → `SendNewSubscriptionFullStates(iTick)` (`ServerSession.cpp:411` → `ServerSessionBase.cpp:49-71`). Both consumers run only inside the broadcast path.
- Immediate ACK confirmed: `Server::ClientSubscribe` sends `SubscribeAccept` (`ServerReceive.cpp:372`) and pushes the pending entry (`:376`) in the same handler; `PreTickNetwork` (which polls) runs every `ServerUpdate` regardless of pause (`GameBase.cpp:98`, `ServerSession.cpp:247-262`), so the next `Poll` wipes the entry before any consumer ran.
- No `kWaitingFullState` timeout exists anywhere client-side (repo-wide grep: only state checks in `ClientReceive.cpp`/`Client.h`). `PollDesyncTimeout` exists (`ClientDesyncManager.cpp:53`) but covers only desync debug mode, partially mitigating the resync drop as stated.
- Fix shape (a) is coherent: the other `Poll` drains (spawn requests, disconnects, game packets) are all consumed from `PreTickNetwork`, which runs while paused.
