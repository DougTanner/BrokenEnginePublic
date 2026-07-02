# Subscription Lifecycle Race Hardening

## Context

Three related subscription-lifecycle races in engine client/server networking. Each turns a transient handshake hiccup into a **permanently stuck slot / frozen cell** with no timeout to break it. The shared root cause is the placeholder-in-slot design (client-chosen slot placeholder vs server-assigned index), whose full redesign (per-subscription generation ids) was audited and deferred as YAGNI — see Out of scope.

Subscription state machine: `kUnsubscribed -> kSubscribing -> kWaitingFullState -> kActive -> kUnsubscribing`. Full state and subscribe-accept can arrive in either order across ENet channels; ENet orders per-channel only (control acks on channel 0, coord full state on the slot channel).

### (a) Ghost full-state adoption on a freed slot

`Client::ClassifyFullState` (`Engine/Source/Network/Client/ClientReceive.cpp:68-96`) `kUnsubscribed` branch (`:73-76`) commits unconditionally:

```
if (rSlot.eState == CoordSubscriptionState::kUnsubscribed)
    return { FullStateFlags::kClearPlaceholder, FullStateFlags::kCommit };
```

with no requirement that a `kSubscribing` placeholder for that coord exists. Race: subscribe → accept → quick unsubscribe → `kServerUnsubscribeAck` (channel 0) overtakes an in-flight full state (slot channel) → slot goes `kUnsubscribed` → the late full state arrives → the slot reactivates `kActive` for a coord the server has already freed. If that coord is still desired, `BuildSubscriptionQueue` sees it "active" and never re-subscribes → permanently frozen cell, no timeout.

The comment "Full state arrived before SubscribeAccept" describes the *legitimate* use of this branch (per `Network/Client/CLAUDE.md` out-of-order full state), but the branch cannot distinguish it from the ghost case.

### (b) Unsubscribe handshake gaps

`Server::ClientUnsubscribe` (`Engine/Source/Network/Server/ServerReceive.cpp:384-413`) silently drops an unsubscribe for an inactive slot without acking:

```
if (uiSlotIndex >= ssize(...) || !(...at(uiSlotIndex).flags & SubscriptionFlags::kActive))
    return;   // no ack
```

A client slot sitting in `kUnsubscribing` then waits forever for `kServerUnsubscribeAck` — healed today only incidentally because `SendSubscribe` treats `kUnsubscribing` as reusable.

Also, `kClientUnsubscribe` carries **only the slot index, no epoch** (`Client::SendUnsubscribe`, `Engine/Source/Network/Client/ClientSend.cpp:136-140`; also the four `SendSimplePacket(PacketType::kClientUnsubscribe, ...)` ghost-reject sites in `ClientReceive.cpp:177,196,463,473`). Ghost-reject paths fire unsubscribes keyed by slot alone; in a slot-reuse race a ghost unsubscribe can free a **newly established** subscription on the same server slot, leaving the client stuck `kWaitingFullState` with no timeout.

### (c) No watchdog on transitional states

`kSubscribing` / `kWaitingFullState` / `kUnsubscribing` have no timeout anywhere. Every logic gap (a, b, or any future one) becomes a permanently stuck slot instead of a hiccup.

## Design

### (a) Ghost guard in `ClassifyFullState`

Commit from `kUnsubscribed` only when a `kSubscribing` placeholder for the incoming coord exists somewhere in `mCoordSlots` (the legitimate full-state-before-accept case). Otherwise classify as ghost: send `kClientUnsubscribe` for the slot and drop, mirroring the existing `kRejectAsGhost` handling in `ServerCoordFullState` (`ClientReceive.cpp:174-180`). Reuse / factor the existing placeholder scan (`ClearSubscribingPlaceholder`, `:56-66`) for the existence check.

### (b) Unsubscribe handshake

- Server: in `ClientUnsubscribe`, **ack unconditionally** (idempotent, cheap) — send `kServerUnsubscribeAck` even when the slot is already inactive, so a client in `kUnsubscribing` always resolves. Keep the `FreeSlot` only when the slot was active.
- Add an epoch to `kClientUnsubscribe` (payload grows 1 → 2 bytes: `uint8 slot` + `uint16 epoch`). Client stamps the slot's current `ackState.uiEpoch`; server verifies epoch against the slot before freeing, so a ghost unsubscribe cannot free a newer subscription that reused the slot. Thread the epoch through `SendUnsubscribe` and every `SendSimplePacket(kClientUnsubscribe, ...)` ghost-reject site (the ghost sites send the epoch of the slot they observed).
- **Requires `kuiProtocolVersion` bump** (`Engine/Source/Network/NetworkProtocol.h:64`, currently `5`). Verify server-side parse in `ClientUnsubscribe`.

### (c) Transitional watchdog

Coarse per-slot age counter on `ClientCoordSlot`: while a slot sits in `kSubscribing` / `kWaitingFullState` / `kUnsubscribing`, increment; past a few-seconds threshold, reset the slot to `{}` (freeing it) and let `BuildSubscriptionQueue` re-queue the coord. Reset the counter on any state transition. This is a safety net that complements (a)/(b), not a replacement — the goal is that no logic gap can strand a slot indefinitely.

## Critical files

- `Engine/Source/Network/Client/ClientReceive.cpp` — `Client::ClassifyFullState` `kUnsubscribed` branch (a); the `kClientUnsubscribe` ghost-reject send sites (b).
- `Engine/Source/Network/Client/ClientSend.cpp` — `Client::SendUnsubscribe` epoch payload (b).
- `Engine/Source/Network/Server/ServerReceive.cpp` — `Server::ClientUnsubscribe` unconditional ack + epoch verify (b).
- `Engine/Source/Network/Client/Client.h` — `ClientCoordSlot` watchdog counter field + `SendUnsubscribe` signature (c, b); `CoordSubscriptionState` unchanged.
- `Engine/Source/Network/NetworkProtocol.h` — `kuiProtocolVersion` bump (b).
- Watchdog tick site — the client per-frame subscription policy in `ClientSessionBase` (`Engine/Source/Network/Client/ClientSessionBase.cpp`), alongside the existing queue rebuild (c).

## Out of scope

- **Placeholder-in-slot redesign (per-subscription generation ids).** The deeper root cause is that the client picks a placeholder slot before the server assigns an index; a generation-id scheme was audited and **deliberately deferred as YAGNI**. This plan hardens the existing design in place; it does not replace it.
- The `kActive` resync commit — that is `Network/ResyncFullStateRepair.md` (same function, co-scheduled).
- No change to the ACK/resend protocol, epoch semantics on the ACK stream, or `Frame::kiVersion`.
- No new packet types; `kClientUnsubscribe` payload extension only.

## Acceptance criteria

- A late full state on a `kUnsubscribed` slot with no matching `kSubscribing` placeholder is rejected (unsubscribe sent, no reactivation); a genuine out-of-order full state before accept still adopts.
- An unsubscribe for an already-inactive server slot is acked, so the client never hangs in `kUnsubscribing`.
- An epoch-mismatched `kClientUnsubscribe` does not free a reused slot's newer subscription.
- A slot stuck in any transitional state is auto-reset after the watchdog threshold and the coord re-queued.

## Notes

- **Invariant exposure**: **wire change + `kuiProtocolVersion` bump** (`kClientUnsubscribe` 1→2 payload bytes); touches cross-frame client/server subscription state and network protocol handling (Risk: hard to verify). No sim/CRC/`Frame::kiVersion` change.
- **Version-bump coordination**: batch the `kuiProtocolVersion` bump with `Network/PackIntegrityHandshake.md` (the queue's other pending wire change / version bump) — one bump for both. **Never interleave with `Network/Architecture_WireFormatPairing.md`**, which restructures every send/receive site including these; land one, refresh the other's citations.
- **Co-schedule with `Network/ResyncFullStateRepair.md`** — both edit `Client::ClassifyFullState`.
- **Grill decision to pre-stage**: watchdog threshold duration and whether the counter is wall-clock or frame-count (lean wall-clock, few seconds, matching `kDesyncDebugTimeout`-style constants). The (a) and (b) fixes have no open decision.
