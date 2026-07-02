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

Pick and enforce one legible naming scheme so the contract is visible at every call site. Two options; **recommended = A**.

### Option A (recommended): name-by-contract, no behavior change

- **Contract (1) — cleared-next-poll**: rename `Drain*` → `Received*()` (e.g. `ReceivedGamePackets()`, `ReceivedCoordUpdates()`, `ReceivedFullStates()`, `ReceivedStaticData()`, `PendingSpawnRequests()`, `PendingDisconnects()`). The name signals "a live view of what arrived; do not assume calling consumes it." Multiple-reads-per-frame (`ClientSession.cpp:76,84,104`) stay correct and now *read* correctly.
- **Contract (2) — persist-until-served**: rename `DrainPendingNewSubscriptions` → `PendingNewSubscriptions()` and `DrainPendingResyncClientIds` → `PendingResyncClientIds()`, and add explicit `ClearPendingNewSubscriptions()` / `ClearPendingResyncClientIds()` members. Consumers call the accessor to read, then the explicit clear when serviced — replacing the in-place `.clear()` on the returned ref (`ServerSessionBase.cpp:92`, `ServerSession.cpp:537`, and the reset-path clears `:535-536`). The clear becomes a named, greppable act instead of a `.clear()` easily mistaken for contract (1).
- **Contract (3) — true consumers**: keep `Take*` / `Drain*` for genuine move-out — rename `DrainReceivedDebugFrame` → `TakeReceivedDebugFrame()`, `DrainLoadNotification` → `TakeLoadNotification()`. `Take*` is the one verb that means "consumes on call."

Net: three verbs, three contracts. `Received*` = live-view-cleared-next-poll, `Pending*` + `ClearPending*` = persist-until-served, `Take*` = consume-on-call. Mechanical, compile-checked (every rename breaks the build if a site is missed), no wire/behavior change.

### Option B (alternative, bigger): unify everything to consumer-cleared `Take*()`

Make every accessor consume on call (move-out or clear-on-return). This also removes `Poll`'s silent discard of unconsumed contract-(1) data (the `Client.cpp:107-113` / `Server.cpp:70-76` entry-clears go away). But it is a **behavior change**: any path that reads a buffer twice per frame (`ClientSession.cpp:76,84,104`) must be restructured to read once, and a skipped drain that today self-heals at next `Poll` would instead accumulate. Higher risk; deferred unless the grill prefers it.

## Critical files

Accessor definitions:
- `Engine/Source/Network/Client/Client.h:110-113, 121, 124` — the six client accessors.
- `Engine/Source/Network/Server/Server.h:182-186` — the five server accessors.
- `Engine/Source/Network/Client/Client.cpp:107-113`, `Engine/Source/Network/Server/Server.cpp:70-76` — the `Poll` entry-clears (unchanged in A; removed for contract-(1) in B).

Call sites (enumerate + convert all; grep `Drain` under both Network trees):
- `ClientSession.cpp:69,76,84,104` (`DrainLoadNotification`, `DrainReceivedGamePackets` ×3), `:382-386` (reset hand-clears).
- `ClientDataReceiver.cpp:19` (`DrainReceivedStaticData`), `:41` (`DrainReceivedFullStates`) — **note**: these fold into `ClientSession` if `Refactor_SessionBaseCollapse.md` lands first.
- `ClientSessionBase.cpp:239` (`DrainReceivedCoordUpdates`) — likewise folds.
- `ServerSessionBase.cpp:68,92` (`DrainPendingNewSubscriptions` + its clear) — folds into `ServerSession`.
- `ServerSession.cpp:445,537` (`DrainPendingResyncClientIds` + clear), `:535-536` (reset-path pending clears).
- `ClientDesyncManager.cpp` / debug-frame path (`DrainReceivedDebugFrame`) — grep to confirm site.

## Out of scope

- No wire-format, protocol-version, `Frame::kiVersion`, CRC, or determinism change — this is a rename + (option A) one added clear method.
- The `Poll`-entry drain-per-poll **semantics** stay as-is under the recommended Option A; only Option B alters them (deferred).
- Not the session-layer collapse (`Refactor_SessionBaseCollapse.md`) — though the two share call sites; see Notes.
- **NOTE, not fixed here**: the torn same-frame invariant where a full state activates the slot at receive time (`ClientReceive.cpp:211-215`: `ackState.iAckFloor`/bitfields/`uiEpoch`/`kActive` set immediately) while its frame payload is only adopted later at the game-layer drain (`ClientReceive.cpp:209` pushes to `mReceivedFullStates`; adoption in `ApplyReceivedFullStates`). A skipped drain keeps the activated slot but loses the frame — held together today only by the same-frame convention (`Network/Client/CLAUDE.md` "Activation and adoption are same-frame"). This renaming does not repair it; it is addressed by `Network/ClientFullStateEdgeFixes.md` (sibling audit plan). Renaming to `Received*` at least makes the "live view, consume this frame" contract legible at the drain site.

## Acceptance criteria

- Every receive-buffer accessor on `Client` and `Server` is named by contract: `Received*` (cleared-next-poll), `Pending*` + `ClearPending*` (persist-until-served), `Take*` (consume-on-call). No `Drain*` remains on a non-consuming accessor.
- Persist-until-served queues are cleared only through the named `ClearPending*` methods; no bare `.clear()` on a returned buffer ref.
- Client + server builds compile; behavior unchanged under Option A (verified by the drain semantics being identical).

## Notes

- **Scheme choice is the grill decision** — pre-staged above: **Option A** (name-by-contract, zero behavior change, recommended) vs **Option B** (unify to consume-on-call `Take*`, removes `Poll`'s silent discard, but restructures the multi-read-per-frame sites and changes skipped-drain self-heal behavior — bigger, higher risk). Decide before implementation.
- **Invariant exposure**: Option A — none (mechanical rename + one added method, compile-checked). Option B — touches the drain-per-poll runtime semantics and cross-frame receive state (Risk up one tier); deferred unless chosen.
- **Sequence vs `Refactor_SessionBaseCollapse.md`**: several call sites (`ClientDataReceiver.cpp`, `ClientSessionBase.cpp`, `ServerSessionBase.cpp`) relocate when the base layer collapses. Land collapse **first** to shrink and stabilize this plan's call-site list, or co-schedule and refresh citations. Do not interleave.
- **Overlap with `Network/Architecture_WireFormatPairing.md`**: that plan restructures send/receive sites but does not rename these buffer accessors; no direct conflict, but co-schedule if both touch `Client.h`/`Server.h` in one session.
