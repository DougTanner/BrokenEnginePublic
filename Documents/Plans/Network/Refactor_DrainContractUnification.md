# Refactor: Unify the `Drain*` Receive-Buffer Contract

## Context

The engine peers (`engine::Client`, `engine::Server`) expose receive-buffer accessors under one `Drain*` naming convention that hides **three incompatible lifetime contracts**. The name promises "consume"; two of the three do not. A caller that misreads the convention gets a dropped-event or double-processing bug, and the compiler cannot catch it — the only signal is a comment.

Verified current source:

### (1) Cleared-next-poll mutable refs (both sides)

`Client::DrainReceivedCoordUpdates` / `DrainReceivedFullStates` / `DrainReceivedStaticData` (`Client.h:110-112`) and `DrainReceivedGamePackets` (`Client.h:121`); `Server::DrainReceivedGamePackets` (`Server.h:186`). These return a live reference; the buffer is **not** cleared on call — it clears at the **next `Poll()` entry** (`Client::Poll` clears at `Client.cpp:107-113`; `Server::Poll` clears `mReceivedGamePackets` at `Server.cpp:76`). Consumers must finish within the tick or the data is silently discarded. `ClientSession::PollNetwork` calls `DrainReceivedGamePackets()` **three times in one frame** (`ClientSession.cpp:76, 84, 104`) relying on this — each call re-reads the same undrained buffer.

### (2) Caller-clears persist-until-served (server only)

`Server::DrainPendingNewSubscriptions` (`Server.h:184`) and `DrainPendingResyncClientIds` (`Server.h:185`) deliberately **survive `Poll`** while paused — `Server::Poll` intentionally does **not** clear them (`Server.cpp:70-76`, documented `:72-75`) because their consumers run only post-tick and are skipped while `iFullTicks == 0`. Consumers clear once serviced: `SendNewSubscriptionFullStates` (`ServerSessionBase.cpp:92` `rNewSubs.clear()`), `HandleResyncRequests` (`ServerSession.cpp:537`). And `ServerSession::ResetClientsForLoad` must know to hand-clear them (`ServerSession.cpp:535-537` `.clear()` on all three pending queues). Client-side, `ClientSession::ResetForServerLoad` similarly hand-clears via the `Drain*` refs (`ClientSession.cpp:382-386`: `DrainReceivedFullStates().clear()` + per-slot `DrainReceivedCoordUpdates()` clear).

### (3) True move-out

`Client::DrainReceivedDebugFrame` (`Client.h:113`, `return std::move(mpReceivedDebugFrame)`) and `Client::DrainLoadNotification` (`Client.h:124`, reads-and-clears a flag) genuinely consume on call.

### Also: pending-spawn/disconnect (server)

`Server::DrainPendingSpawnRequests` / `DrainPendingDisconnects` (`Server.h:182-183`) are contract (1) (cleared next `Poll`, `Server.cpp:70-71`).

`Server.h:182-186` lines up five identical-looking `Drain*` accessors spanning **two** of these contracts (spawn/disconnect/gamepackets are cleared-next-poll; new-subscriptions/resync persist-until-served), distinguished only by the `Server.cpp:72-75` comment. One missed convention = dropped-event or double-processing.

## Design

**Decision (2026-07-03): chose Option A (name-by-contract, no behavior change).** The rejected alternative — unifying every accessor to consume-on-call `Take*()` — was a runtime behavior change hiding in a rename plan: it removes `Poll`'s entry-clear self-heal (`Client.cpp:107-113`, `Server.cpp:70-76`), forces restructuring the three-reads-per-frame consumption in `ClientSession::PollNetwork` (`ClientSession.cpp:76,84,104`), and turns a skipped drain from self-healing into unbounded accumulation — with no independent justification for any of that. Option A is a compile-checked mechanical rename plus two explicit clear methods, zero behavior change, and matches the documented drain-per-poll convention (`Engine/Source/Network/AGENTS.md` "Drain-per-poll").

Name each accessor by its contract; the executor implements exactly these three steps:

1. **Contract (1) — cleared-next-poll**: rename `Drain*` → `Received*()` / `Pending*()` with no other change: `ReceivedGamePackets()` (client and server), `ReceivedCoordUpdates()`, `ReceivedFullStates()`, `ReceivedStaticData()`, `PendingSpawnRequests()`, `PendingDisconnects()`. The name signals "a live view of what arrived; do not assume calling consumes it." Multiple-reads-per-frame (`ClientSession.cpp:76,84,104`) stay correct and now *read* correctly. Reset-path hand-clears on these live refs (`ServerSession.cpp:535`, `ClientSession.cpp:382-386`) stay as bare `.clear()` — they are legitimate uses of the mutable view.
2. **Contract (2) — persist-until-served**: rename `DrainPendingNewSubscriptions` → `PendingNewSubscriptions()` and `DrainPendingResyncClientIds` → `PendingResyncClientIds()`, and add explicit `ClearPendingNewSubscriptions()` / `ClearPendingResyncClientIds()` members on `Server`. Consumers call the accessor to read, then the explicit clear when serviced — replacing the in-place `.clear()` on the returned ref (`ServerSessionBase.cpp:93`, `ServerSession.cpp:537`, and the reset-path pending clears `ServerSession.cpp:536-537`). The clear becomes a named, greppable act instead of a `.clear()` easily mistaken for contract (1).
3. **Contract (3) — true consumers**: rename `DrainReceivedDebugFrame` → `TakeReceivedDebugFrame()`, `DrainLoadNotification` → `TakeLoadNotification()`. `Take*` is the one verb that means "consumes on call."

Net: three verbs, three contracts. `Received*`/`Pending*` (mutable ref) = live-view-cleared-next-poll, `Pending*` + `ClearPending*` = persist-until-served, `Take*` = consume-on-call. Mechanical, compile-checked (every rename breaks the build if a site is missed), no wire/behavior change. Also update the comments that name the old convention: `ClientSession.cpp:61` (`// Heap: ... DrainReceived*`) and any `Drain`-naming prose that becomes false in `Engine/Source/Network/` AGENTS.md files; synchronize those durable documentation changes during **Apply conditional hygiene**.

## Critical files

Accessor definitions:
- `Engine/Source/Network/Client/Client.h:110-113, 121, 124` — the six client accessors.
- `Engine/Source/Network/Server/Server.h:182-186` — the five server accessors.
- `Engine/Source/Network/Client/Client.cpp:107-113`, `Engine/Source/Network/Server/Server.cpp:70-76` — the `Poll` entry-clears (unchanged — cited for context only).

Call sites (verified 2026-07-03 by grepping `Drain` under `Engine/Source/Network` and `Projects/.../Source/Network`; re-grep at execution — convert all):
- `ClientSession.cpp:69,76,84,104` (`DrainLoadNotification`, `DrainReceivedGamePackets` ×3), `:382-386` (reset hand-clears), `:61` (`// Heap:` comment naming `DrainReceived*`).
- `ClientDataReceiver.cpp:19` (`DrainReceivedStaticData`), `:41` (`DrainReceivedFullStates`) — **note**: these fold into `ClientSession` if `Refactor_SessionBaseCollapse.md` lands first.
- `ClientSessionBase.cpp:239` (`DrainReceivedCoordUpdates`) — likewise folds.
- `ClientDesyncManager.cpp:28` (`DrainReceivedDebugFrame`).
- `ServerSessionBase.cpp:69,93` (`DrainPendingNewSubscriptions` + its clear) — folds into `ServerSession`.
- `ServerSession.cpp:112` (`DrainReceivedGamePackets`), `:445,537` (`DrainPendingResyncClientIds` + clear), `:535-537` (reset-path clears: spawn requests stay a bare `.clear()` on the live ref; new-subs/resync become `ClearPending*` calls).
- `ServerClientManager.cpp:29` (`DrainPendingSpawnRequests`), `:236` (`DrainPendingDisconnects`).

## Out of scope

- No wire-format, protocol-version, `Frame::kiVersion`, CRC, or determinism change — this is a rename + two added `ClearPending*` methods.
- The `Poll`-entry drain-per-poll **semantics** stay exactly as-is — no accessor becomes consuming, no entry-clear is removed.
- Not the session-layer collapse (`Refactor_SessionBaseCollapse.md`) — though the two share call sites; see Notes.
- **NOTE, not fixed here**: the torn same-frame invariant where a full state activates the slot at receive time (`ClientReceive.cpp:211-215`: `ackState.iAckFloor`/bitfields/`uiEpoch`/`kActive` set immediately) while its frame payload is only adopted later at the game-layer drain (`ClientReceive.cpp:209` pushes to `mReceivedFullStates`; adoption in `ApplyReceivedFullStates`). A skipped drain keeps the activated slot but loses the frame — held together today only by the same-frame convention (`Network/Client/AGENTS.md` "Activation and adoption are same-frame"). This renaming does not repair it; it is addressed by `Network/ClientFullStateEdgeFixes.md` (sibling audit plan). Renaming to `Received*` at least makes the "live view, consume this frame" contract legible at the drain site.

## Acceptance criteria

- Every receive-buffer accessor on `Client` and `Server` is named by contract: `Received*` (cleared-next-poll), `Pending*` + `ClearPending*` (persist-until-served), `Take*` (consume-on-call). No `Drain*` remains on a non-consuming accessor.
- Persist-until-served queues (new-subscriptions, resync ids) are cleared only through the named `ClearPending*` methods — no bare `.clear()` on those two refs. (Reset-path `.clear()` on contract-(1) live refs remains legitimate.)
- Client + server builds compile; behavior unchanged (drain semantics identical — pure rename plus two added clear methods).

## Coordination

- Never interleave with `Documents/Plans/Network/Refactor_SessionBaseCollapse.md`, `Documents/Plans/Network/Refactor_ClientResetUnification.md`. The structured chain is SessionBaseCollapse → DrainContractUnification → ClientResetUnification; refresh relocated citations between landings.

## Notes

- **Scheme decided** (2026-07-03, see `## Design`): name-by-contract, zero behavior change. No open decisions remain; the executor implements the three steps as written. If implementation uncovers a `Drain*` site whose contract matches none of the three, stop and report rather than guess.
- **Invariant exposure**: none — mechanical rename + two added clear methods, compile-checked; drain-per-poll runtime semantics untouched.
- **Sequence vs `Refactor_SessionBaseCollapse.md`**: several call sites (`ClientDataReceiver.cpp`, `ClientSessionBase.cpp`, `ServerSessionBase.cpp`) relocate when the base layer collapses. Land collapse **first** to shrink and stabilize this plan's call-site list, or co-schedule and refresh citations. Do not interleave.
- **Overlap with `Network/Architecture_WireFormatPairing.md`**: that plan restructures send/receive sites but does not rename these buffer accessors; no direct conflict, but co-schedule if both touch `Client.h`/`Server.h` in one session.
