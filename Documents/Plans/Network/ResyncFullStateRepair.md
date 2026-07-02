# Resync Full-State Repair

## Context

Desync recovery is dead end-to-end whenever `kbDesyncRecovery` is enabled (game `Pch.h`). The recovery loop resends full states to slots that are still `kActive` on the client, but the client's full-state classifier silently rejects full states for `kActive` slots — so every resync full state is discarded, the client keeps its stale `CoordFrames`, desyncs again, and "recovers" only by disconnecting after `kiMaxDesyncsBeforeDisconnect`.

Path, verified current source:

- `Client::ClassifyFullState` (`Engine/Source/Network/Client/ClientReceive.cpp:68-96`) — for `kActive` (and `kUnsubscribing`) the final `return {};` (line 95) drops unconditionally. Comment: `// kActive or kUnsubscribing — silent reject`.
- `ClientDesyncManager::RecoverFromDesync` → `SendResyncRequest` (`Projects/BrokenEngineSandbox/Source/Network/Client/ClientDesyncManager.cpp:76-104`) then `ResetCoordStatesForResync` (`:106-115`) resets `CoordFrames` (`ResetClientState`), the reconciler, and sticky subscriptions — **but never touches engine client slot states** (`Client::mCoordSlots`), which stay `kActive`.
- Server side: `engine::Server::ClientResyncRequest` queues the id (`Engine/Source/Network/Server/ServerReceive.cpp:415-428`); `game::ServerSession::HandleResyncRequests` (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp:443-485`) re-sends `SendCoordFullState` for every slot still flagged `kActive` on the server (`:464-479`), at the server's current tick.

Because the resend arrives on the still-`kActive` client slot, `ClassifyFullState` returns drop and the recovery never lands.

## Design

In `ClassifyFullState`, commit on `kActive` when the incoming full state's coord **and** epoch match the slot (`rSlot.coord == coord && uiEpoch == rSlot.ackState.uiEpoch`); otherwise keep the existing silent-reject.

- The existing commit path in `ServerCoordFullState` (`ClientReceive.cpp:187-216`) already performs exactly the resync semantic: it resets `ackState.iAckFloor` to the new tick, clears both received-bitfield words, sets the epoch, and re-sets `kActive`. Re-committing an already-`kActive` slot at the resync tick is the desired reset.
- Coord+epoch match is required so a genuinely stale/ghost full state (recycled slot, different coord, or a superseded epoch) is still rejected. Server slot epoch is unchanged across a resync (no `FreeSlot`/resubscribe), so the legitimate resend matches.
- Stale-tick protection is already downstream: `ApplyReceivedFullStates` (`Projects/BrokenEngineSandbox/Source/Network/Client/ClientDataReceiver.cpp:121-135`) rejects any full state whose tick `<= iConfirmedTick`, so a passed-tick resync full state is discarded there rather than corrupting the ring.

`kUnsubscribing` stays a silent reject — a slot the client is tearing down should not be reactivated by a resync.

No wire change. Affects desync-recovery behavior only.

## Critical files

- `Engine/Source/Network/Client/ClientReceive.cpp` — `Client::ClassifyFullState` (the `kActive` branch). Confirm the commit block in `ServerCoordFullState` still resets floor/bitfields/epoch as relied on above.

## Out of scope

- No change to `ResetCoordStatesForResync`, `RecoverFromDesync`, or the server `HandleResyncRequests` resend loop — the fix is entirely in the client classifier.
- No wire-format, protocol-version, or `Frame::kiVersion` change.
- No change to the `kUnsubscribing` reject, or to the subscription-lifecycle ghost/handshake races (those are `Network/SubscriptionLifecycleRaceHardening.md`).
- Not reworking the desync-escalation counter or `kiMaxDesyncsBeforeDisconnect`.

## Acceptance criteria

- A resync full state arriving on a still-`kActive` slot with matching coord+epoch replaces the slot's `CoordFrames` state (new confirmed frame from the resend tick) instead of being dropped.
- A full state with mismatched coord or superseded epoch on a `kActive` slot is still rejected.
- With `kbDesyncRecovery` enabled, a single injected desync recovers on the next resync round-trip rather than only via disconnect after `kiMaxDesyncsBeforeDisconnect`.

## Notes

- **Invariant exposure**: touches network protocol handling and cross-frame client subscription/CRC state (resync path), but changes classifier state selection only — no sim math, no wire format. Hard to verify without exercising `kbDesyncRecovery`.
- **Co-schedule with `Network/SubscriptionLifecycleRaceHardening.md`** — both edit the same function `Client::ClassifyFullState`. Land together or refresh citations; that plan reworks the `kUnsubscribed` branch while this adds the `kActive` commit.
- No open architectural decision to pre-stage for grill; the single decision (match on coord+epoch) is settled above.
